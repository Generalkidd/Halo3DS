#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "engine_files.h"

enum { FILE_SLOTS=64, SEARCH_DEPTH=8, NATIVE_PATH=512 };
struct native_open_file { FILE *stream; unsigned int generation, permissions; char path[NATIVE_PATH]; };
static struct native_open_file files[FILE_SLOTS];
struct search_level { DIR *directory; char relative[256]; };
static struct { struct search_level levels[SEARCH_DEPTH]; char root[NATIVE_PATH];
    unsigned int flags,owner; int depth; } search={.depth=-1};

/* One recursive service lock protects slots, stream positions and enumeration.
 * Engine-side compound operations may hold it across native calls. */
static RecursiveLock file_lock;
void n3ds_files_lock(void) { RecursiveLock_Lock(&file_lock); }
void n3ds_files_unlock(void) { RecursiveLock_Unlock(&file_lock); }
static unsigned int current_thread_id(void)
{
    u32 id=0;
    if(R_FAILED(svcGetThreadId(&id,CUR_THREAD_HANDLE))) svcBreak(USERBREAK_PANIC);
    return id;
}

int n3ds_file_resolve(const char *path, char *out, unsigned int capacity, int write)
{
    const char *root, *p, *component;
    unsigned int used, length;
    char drive;
    if (!path || !out || !capacity) return 0;
    out[0]=0;
    /* Only explicit Xbox volumes are accepted. Root confinement is independent
     * of the working directory; traversal and native SD paths are rejected. */
    if (!path[0] || path[1]!=':' || (path[2]!='\\' && path[2]!='/')) return 0;
    drive=path[0]; if (drive>='A' && drive<='Z') drive+='a'-'A';
    switch (drive) {
    case 'd': if (write) return 0; root="sdmc:/halo-source"; break;
    case 'z': root="sdmc:/halo-source/state/cache"; break;
    case 'u': root="sdmc:/halo-source/state/saves"; break;
    case 't': root="sdmc:/halo-source/state/title"; break;
    default: return 0;
    }
    used=strlen(root);
    if (used>=capacity) return 0;
    memcpy(out,root,used+1);
    p=path+3;
    /* The prepared, relocated maps already live in the SD game-data root.
     * Mount the Xbox English maps directory there without duplicating assets. */
    if(drive=='d') {
        while(*p=='/' || *p=='\\') ++p;
        if(!strncasecmp(p,"maps",4) && (!p[4] || p[4]=='/' || p[4]=='\\')) p+=4;
    }
    while (*p) {
        while (*p=='/' || *p=='\\') ++p;
        if (!*p) break;
        component=p;
        while (*p && *p!='/' && *p!='\\') {
            unsigned char c=(unsigned char)*p++;
            if (c<32 || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') goto invalid;
        }
        length=p-component;
        if ((length==1 && component[0]=='.') || (length==2 && component[0]=='.' && component[1]=='.') ||
            component[length-1]==' ' || component[length-1]=='.' || used+1+length>=capacity) goto invalid;
        out[used++]='/'; memcpy(out+used,component,length); used+=length; out[used]=0;
    }
    return 1;
invalid:
    out[0]=0; return 0;
}
static int make_roots(void)
{
    static const char *roots[]={"sdmc:/halo-source","sdmc:/halo-source/state",
        "sdmc:/halo-source/state/cache","sdmc:/halo-source/state/saves","sdmc:/halo-source/state/title"};
    unsigned int i;
    for (i=0;i<sizeof(roots)/sizeof(*roots);++i) {
        struct stat st;
        if (mkdir(roots[i],0777) && errno!=EEXIST) return 0;
        if (stat(roots[i],&st) || !S_ISDIR(st.st_mode)) return 0;
    }
    return 1;
}
static struct native_open_file *lookup(unsigned int handle)
{
    struct native_open_file *entry=&files[handle&63];
    return handle && entry->stream && entry->generation==(handle>>6) ? entry : NULL;
}
static int n3ds_file_valid_locked(unsigned int handle) { return lookup(handle)!=NULL; }
static int path_open(const char *path)
{
    unsigned int i;
    for(i=0;i<FILE_SLOTS;++i) if(files[i].stream && !strcasecmp(files[i].path,path)) return 1;
    return 0;
}
static unsigned int n3ds_files_open_count_locked(void)
{
    unsigned int i,n=0; for(i=0;i<FILE_SLOTS;++i) n+=files[i].stream!=NULL; return n;
}
static unsigned int n3ds_file_open_locked(const char *path, unsigned int permissions)
{
    char native[NATIVE_PATH]; unsigned int i;
    FILE *stream;
    if ((permissions&~7u) || !(permissions&3) || ((permissions&4) && !(permissions&2)) ||
        !n3ds_file_resolve(path,native,sizeof(native),(permissions&2)!=0)) return 0;
    if(path_open(native)) return 0; /* Original file_open uses sharing mode zero. */
    for(i=0;i<FILE_SLOTS;++i) if(!files[i].stream && files[i].generation<0x03ffffff) break;
    if(i==FILE_SLOTS) return 0;
    /* OPEN_EXISTING semantics, with append meaning an initial seek only. */
    stream=fopen(native,(permissions&2) ? "r+b" : "rb");
    if(!stream) return 0;
    if((permissions&4) && fseek(stream,0,SEEK_END)) { fclose(stream); return 0; }
    files[i].stream=stream; files[i].permissions=permissions; ++files[i].generation;
    strcpy(files[i].path,native);
    return (files[i].generation<<6)|i;
}
static int n3ds_file_close_locked(unsigned int handle)
{
    struct native_open_file *entry=lookup(handle); int result;
    if(!entry) return 0;
    result=fclose(entry->stream)==0; entry->stream=NULL; entry->permissions=0;
    return result;
}
static int n3ds_file_read_locked(unsigned int handle, void *buffer, unsigned int size)
{
    struct native_open_file *entry=lookup(handle);
    if(!entry || !(entry->permissions&1) || (!buffer && size) || size>INT_MAX) return 0;
    /* Read-only streams are already sequential. Seeking discards their read
     * buffer on SD; only mixed read/write streams need synchronization. */
    return (!(entry->permissions&2) || !fseek(entry->stream,0,SEEK_CUR)) && (!size || fread(buffer,1,size,entry->stream)==size);
}
static int n3ds_file_write_locked(unsigned int handle, const void *buffer, unsigned int size)
{
    struct native_open_file *entry=lookup(handle);
    if(!entry || !(entry->permissions&2) || (!buffer && size) || size>INT_MAX) return 0;
    return !fseek(entry->stream,0,SEEK_CUR) && (!size || fwrite(buffer,1,size,entry->stream)==size) && !fflush(entry->stream);
}
static unsigned int n3ds_file_position_locked(unsigned int handle)
{
    struct native_open_file *entry=lookup(handle); long position;
    if(!entry || (position=ftell(entry->stream))<0) return UINT_MAX;
    return (unsigned int)position;
}
static unsigned int n3ds_file_size_locked(unsigned int handle)
{
    struct native_open_file *entry=lookup(handle); struct stat st;
    if(!entry || fstat(fileno(entry->stream),&st) || st.st_size<0 || st.st_size>INT_MAX) return UINT_MAX;
    return (unsigned int)st.st_size;
}
static int n3ds_file_seek_locked(unsigned int handle, unsigned int position)
{
    struct native_open_file *entry=lookup(handle);
    return entry && position<=INT_MAX && !fseek(entry->stream,(long)position,SEEK_SET);
}
static int n3ds_file_resize_locked(unsigned int handle, unsigned int size)
{
    struct native_open_file *entry=lookup(handle);
    return entry && (entry->permissions&2) && size<=INT_MAX && !fflush(entry->stream) &&
        !fseek(entry->stream,(long)size,SEEK_SET) && !ftruncate(fileno(entry->stream),(off_t)size);
}
static int metadata(const char *path, struct native_file_info *info)
{
    struct stat st; uint64_t date;
    memset(info,0,sizeof(*info));
    if(stat(path,&st) || st.st_size<0 || st.st_size>INT_MAX) return 0;
    info->size=S_ISDIR(st.st_mode) ? 0 : (unsigned int)st.st_size;
    info->directory=S_ISDIR(st.st_mode); info->readonly=!(st.st_mode&S_IWUSR);
    /* Retain the original FILETIME byte layout for stored comparison records. */
    date=((int64_t)st.st_mtime+11644473600LL)*10000000ULL;
    info->date[0]=(unsigned int)date; info->date[1]=(unsigned int)(date>>32);
    return 1;
}
static int n3ds_file_stat_locked(const char *path, struct native_file_info *info)
{
    char native[NATIVE_PATH];
    if(!info) return 0;
    memset(info,0,sizeof(*info));
    if(!n3ds_file_resolve(path,native,sizeof(native),0) || !metadata(native,info)) return 0;
    if(path[0]=='d' || path[0]=='D') info->readonly=1;
    return 1;
}
static int n3ds_file_create_locked(const char *path, int directory)
{
    char native[NATIVE_PATH]; FILE *stream;
    if(!n3ds_file_resolve(path,native,sizeof(native),1) || path_open(native) || !make_roots()) return 0;
    if(directory) return mkdir(native,0777)==0;
    stream=fopen(native,"wb"); return stream && fclose(stream)==0;
}
static int n3ds_file_delete_locked(const char *path, int directory)
{
    char native[NATIVE_PATH];
    if(!n3ds_file_resolve(path,native,sizeof(native),1) || path_open(native)) return 0;
    return directory ? rmdir(native)==0 : unlink(native)==0;
}
static int n3ds_file_rename_locked(const char *old_path, const char *new_path)
{
    char old_native[NATIVE_PATH],new_native[NATIVE_PATH]; struct stat st;
    if(!n3ds_file_resolve(old_path,old_native,sizeof(old_native),1) ||
       !n3ds_file_resolve(new_path,new_native,sizeof(new_native),1)) return 0;
    if(path_open(old_native) || path_open(new_native)) return 0;
    /* Xbox MoveFile does not overwrite an existing destination. */
    if(!stat(new_native,&st) || errno!=ENOENT) return 0;
    return rename(old_native,new_native)==0;
}
static void n3ds_files_find_end_locked(void)
{
    if(search.owner && search.owner!=current_thread_id()) return;
    while(search.depth>=0) {
        if(search.levels[search.depth].directory) closedir(search.levels[search.depth].directory);
        search.levels[search.depth].directory=NULL; --search.depth;
    }
    search.owner=0;
}
static unsigned int n3ds_files_directory_count_locked(void) { return search.depth<0 ? 0 : search.depth+1; }
static int n3ds_files_find_start_locked(const char *path, unsigned int flags)
{
    if(search.owner && search.owner!=current_thread_id()) return 0;
    n3ds_files_find_end();
    if((flags&~3u) || !n3ds_file_resolve(path,search.root,sizeof(search.root),0)) return 0;
    search.levels[0].directory=opendir(search.root);
    if(!search.levels[0].directory) return 0;
    search.flags=flags; search.owner=current_thread_id(); search.depth=0; search.levels[0].relative[0]=0; return 1;
}
static int n3ds_files_find_next_locked(char *relative, unsigned int capacity, struct native_file_info *info)
{
    if(!relative || !capacity || !info || (search.owner && search.owner!=current_thread_id())) return -1;
    while(search.depth>=0) {
        struct search_level *level=&search.levels[search.depth];
        struct dirent *entry; char name[256],full[NATIVE_PATH]; int length;
        errno=0; entry=readdir(level->directory);
        if(!entry) {
            if(errno) goto failure;
            closedir(level->directory); level->directory=NULL; --search.depth; continue;
        }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        length=snprintf(name,sizeof(name),"%s%s%s",level->relative,*level->relative ? "/" : "",entry->d_name);
        if(length<0 || (unsigned int)length>=sizeof(name) || (unsigned int)length>=capacity) goto failure;
        length=snprintf(full,sizeof(full),"%s/%s",search.root,name);
        if(length<0 || (unsigned int)length>=sizeof(full) || !metadata(full,info)) goto failure;
        if(info->directory && (search.flags&1)) {
            struct search_level *child;
            if(search.depth+1==SEARCH_DEPTH) goto failure;
            child=&search.levels[search.depth+1]; child->directory=opendir(full);
            if(!child->directory) goto failure;
            strcpy(child->relative,name); ++search.depth;
        }
        if(!!info->directory==!!(search.flags&2)) {
            char *p;
            strcpy(relative,name); for(p=relative;*p;++p) if(*p=='/') *p='\\';
            return 1;
        }
    }
    search.owner=0;
    return 0;
failure:
    n3ds_files_find_end(); relative[0]=0; return -1;
}

/* Public synchronized service boundary. */
int n3ds_file_valid(unsigned int handle)
{ n3ds_files_lock(); int result=n3ds_file_valid_locked(handle); n3ds_files_unlock(); return result; }
unsigned int n3ds_files_open_count(void)
{ n3ds_files_lock(); unsigned int result=n3ds_files_open_count_locked(); n3ds_files_unlock(); return result; }
unsigned int n3ds_file_open(const char *path, unsigned int permissions)
{ n3ds_files_lock(); unsigned int result=n3ds_file_open_locked(path,permissions); n3ds_files_unlock(); return result; }
int n3ds_file_close(unsigned int handle)
{ n3ds_files_lock(); int result=n3ds_file_close_locked(handle); n3ds_files_unlock(); return result; }
int n3ds_file_read(unsigned int handle, void *buffer, unsigned int size)
{ n3ds_files_lock(); int result=n3ds_file_read_locked(handle,buffer,size); n3ds_files_unlock(); return result; }
int n3ds_file_write(unsigned int handle, const void *buffer, unsigned int size)
{ n3ds_files_lock(); int result=n3ds_file_write_locked(handle,buffer,size); n3ds_files_unlock(); return result; }
int n3ds_file_write_buffered(unsigned int handle,const void *buffer,unsigned int size)
{
    n3ds_files_lock();struct native_open_file *entry=lookup(handle);
    int result=entry && (entry->permissions&2) && (buffer || !size) && size<=INT_MAX &&
        (!size || fwrite(buffer,1,size,entry->stream)==size);
    n3ds_files_unlock();return result;
}
unsigned int n3ds_file_position(unsigned int handle)
{ n3ds_files_lock(); unsigned int result=n3ds_file_position_locked(handle); n3ds_files_unlock(); return result; }
unsigned int n3ds_file_size(unsigned int handle)
{ n3ds_files_lock(); unsigned int result=n3ds_file_size_locked(handle); n3ds_files_unlock(); return result; }
int n3ds_file_seek(unsigned int handle, unsigned int position)
{ n3ds_files_lock(); int result=n3ds_file_seek_locked(handle,position); n3ds_files_unlock(); return result; }
int n3ds_file_resize(unsigned int handle, unsigned int size)
{ n3ds_files_lock(); int result=n3ds_file_resize_locked(handle,size); n3ds_files_unlock(); return result; }
int n3ds_file_stat(const char *path, struct native_file_info *info)
{ n3ds_files_lock(); int result=n3ds_file_stat_locked(path,info); n3ds_files_unlock(); return result; }
int n3ds_file_create(const char *path, int directory)
{ n3ds_files_lock(); int result=n3ds_file_create_locked(path,directory); n3ds_files_unlock(); return result; }
int n3ds_file_delete(const char *path, int directory)
{ n3ds_files_lock(); int result=n3ds_file_delete_locked(path,directory); n3ds_files_unlock(); return result; }
int n3ds_file_rename(const char *old_path, const char *new_path)
{ n3ds_files_lock(); int result=n3ds_file_rename_locked(old_path,new_path); n3ds_files_unlock(); return result; }
void n3ds_files_find_end(void)
{ n3ds_files_lock(); n3ds_files_find_end_locked(); n3ds_files_unlock(); }
unsigned int n3ds_files_directory_count(void)
{ n3ds_files_lock(); unsigned int result=n3ds_files_directory_count_locked(); n3ds_files_unlock(); return result; }
int n3ds_files_find_start(const char *path, unsigned int flags)
{ n3ds_files_lock(); int result=n3ds_files_find_start_locked(path,flags); n3ds_files_unlock(); return result; }
int n3ds_files_find_next(char *relative, unsigned int capacity, struct native_file_info *info)
{ n3ds_files_lock(); int result=n3ds_files_find_next_locked(relative,capacity,info); n3ds_files_unlock(); return result; }

int n3ds_file_read_at(unsigned int handle,unsigned int position,void *buffer,unsigned int size)
{
    n3ds_files_lock();
    int result=n3ds_file_seek_locked(handle,position) && n3ds_file_read_locked(handle,buffer,size);
    n3ds_files_unlock(); return result;
}
int n3ds_file_write_at(unsigned int handle,unsigned int position,const void *buffer,unsigned int size)
{
    n3ds_files_lock();
    int result=n3ds_file_seek_locked(handle,position) && n3ds_file_write_locked(handle,buffer,size);
    n3ds_files_unlock(); return result;
}
void n3ds_files_thread_exit(void)
{
    /* A worker may stop enumeration early. Never reclaim another owner's walk. */
    n3ds_files_find_end();
}
int n3ds_files_path_in_use(const char *path)
{
    char native[NATIVE_PATH],directory[NATIVE_PATH]; int busy=1;
    n3ds_files_lock();
    if(n3ds_file_resolve(path,native,sizeof(native),0)) {
        size_t n=strlen(native); busy=0;
        for(unsigned int i=0;i<FILE_SLOTS;++i)
            if(files[i].stream && !strncasecmp(native,files[i].path,n) && (!files[i].path[n] || files[i].path[n]=='/')) busy=1;
        for(int i=0;i<=search.depth;++i) if(search.levels[i].directory) {
            snprintf(directory,sizeof(directory),"%s%s%s",search.root,*search.levels[i].relative?"/":"",search.levels[i].relative);
            if(!strncasecmp(native,directory,n) && (!directory[n] || directory[n]=='/')) busy=1;
        }
    }
    n3ds_files_unlock(); return busy;
}

int n3ds_file_copy(const char *old_path,const char *new_path,int fail_if_exists)
{
    char source[NATIVE_PATH],destination[NATIVE_PATH];
    unsigned char buffer[4096];
    struct stat input_info,output_info;
    int input=-1,output=-1,result=0,created=0,error=0;
    n3ds_files_lock();
    if(!n3ds_file_resolve(old_path,source,sizeof(source),0) ||
       !n3ds_file_resolve(new_path,destination,sizeof(destination),1) ||
       !strcasecmp(source,destination) || path_open(source) || path_open(destination)) goto done;
    input=open(source,O_RDONLY);
    if(input<0 || fstat(input,&input_info) || !S_ISREG(input_info.st_mode)) goto done;
    if(!stat(destination,&output_info)) {
        if(fail_if_exists || !S_ISREG(output_info.st_mode) || !(output_info.st_mode&S_IWUSR)) goto done;
        /* Also protect a same-file alias on filesystems exposing inode IDs. */
        if(input_info.st_ino && input_info.st_ino==output_info.st_ino && input_info.st_dev==output_info.st_dev) goto done;
    } else {
        if(errno!=ENOENT) goto done;
        created=1;
    }
    /* Exclusive creation closes the missing-destination race. The service lock
     * also excludes all native file handles during this compound operation. */
    output=open(destination,O_WRONLY|O_CREAT|(created?O_EXCL:O_TRUNC),0666);
    if(output<0) { created=0; goto done; }
    for(;;) {
        ssize_t count=read(input,buffer,sizeof(buffer));
        if(count<0) { if(errno==EINTR) continue; goto done; }
        if(!count) break;
        for(ssize_t offset=0;offset<count;) {
            ssize_t written=write(output,buffer+offset,(size_t)(count-offset));
            if(written<0 && errno==EINTR) continue;
            if(written<=0) goto done;
            offset+=written;
        }
    }
    result=1;
done:
    error=errno;
    if(input>=0 && close(input)) { result=0; error=errno; }
    if(output>=0 && close(output)) { result=0; error=errno; }
    /* An unsuccessful new copy must not masquerade as a completed save file.
     * As with CopyFile, overwrite failure may leave a partial existing target. */
    if(!result && created) unlink(destination);
    errno=error;
    n3ds_files_unlock(); return result;
}
