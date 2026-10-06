#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "engine_files.h"
#include "engine_saves.h"

enum { OK=0, NOT_FOUND=2, ACCESS=5, INVALID=87, EXISTS=183, IO_ERROR=1117, BUSY=32,
       ITERATORS=8, MAX_GENERATION=0x01ffffff };
static const char save_root[]="sdmc:/halo-source/state/saves";
struct save_record { uint32_t magic,version,flags,checksum; unsigned short name[128]; };
_Static_assert(sizeof(struct save_record)==272,"Persistent save metadata layout");
struct save_iterator { DIR *directory; unsigned int generation; };
static struct save_iterator iterators[ITERATORS];
static int valid_name(const unsigned short *name)
{
    if(!name || !*name) return 0;
    for(unsigned int i=0;i<128;++i) {
        unsigned int c=name[i];
        if(!c) return 1;
        if(c<32 || (c>=0xdc00 && c<=0xdfff)) return 0;
        if(c>=0xd800 && c<=0xdbff) {
            if(++i>=127 || name[i]<0xdc00 || name[i]>0xdfff) return 0;
        }
    }
    return 0;
}
static unsigned int folded(unsigned int c) { return c>='A' && c<='Z'?c+32:c; }
static int equal_name(const unsigned short *a,const unsigned short *b)
{
    for(unsigned int i=0;i<128;++i) {
        if(folded(a[i])!=folded(b[i])) return 0;
        if(!a[i]) return 1;
    }
    return 0;
}
static uint32_t checksum(struct save_record record)
{
    uint32_t hash=2166136261U; record.checksum=0;
    const unsigned char *bytes=(const unsigned char *)&record;
    for(unsigned int i=0;i<sizeof(record);++i) hash=(hash^bytes[i])*16777619U;
    return hash;
}
static unsigned int name_hash(const unsigned short *name)
{
    uint32_t hash=2166136261U;
    for(unsigned int i=0;name[i];++i) {
        unsigned int c=folded(name[i]);
        hash=(hash^(c&255))*16777619U; hash=(hash^(c>>8))*16777619U;
    }
    return hash;
}
static int root_ready(void)
{
    struct native_file_info info;
    if(n3ds_file_stat("u:\\",&info)) return info.directory;
    n3ds_file_create("u:\\",1);
    return n3ds_file_stat("u:\\",&info) && info.directory;
}
int n3ds_save_root_valid(const char *root)
{ return root && (root[0]=='u' || root[0]=='U') && root[1]==':' && (root[2]=='\\' || root[2]=='/') && !root[3]; }
static int managed_directory(const char *entry)
{
    if(strlen(entry)!=13 || strncmp(entry,"n3ds-",5)) return 0;
    for(unsigned int i=5;i<13;++i)
        if(!((entry[i]>='0' && entry[i]<='9') || (entry[i]>='a' && entry[i]<='f'))) return 0;
    return 1;
}
static int read_record(const char *entry,struct save_record *record,struct native_save_info *info)
{
    char path[512]; struct stat st; FILE *file; int success;
    if(!managed_directory(entry)) return 0;
    snprintf(path,sizeof(path),"%s/%s",save_root,entry);
    if(stat(path,&st) || !S_ISDIR(st.st_mode)) return 0;
    snprintf(path,sizeof(path),"%s/%s/name.h3s",save_root,entry);
    file=fopen(path,"rb"); if(!file) return 0;
    success=fread(record,1,sizeof(*record),file)==sizeof(*record) && fgetc(file)==EOF && !ferror(file);
    if(fclose(file)) success=0;
    if(!success || record->magic!=0x56533348U || record->version!=1 || (record->flags&~1U) ||
       !valid_name(record->name) || checksum(*record)!=record->checksum) return 0;
    if(info) {
        memset(info,0,sizeof(*info));
        snprintf(info->path,sizeof(info->path),"u:\\%s\\",entry);
        memcpy(info->name,record->name,sizeof(info->name)); info->flags=record->flags;
        uint64_t time=((int64_t)st.st_mtime+11644473600LL)*10000000ULL;
        info->date[0]=time; info->date[1]=time>>32;
    }
    return 1;
}
static int locate(const unsigned short *name,char *directory)
{
    DIR *walk=opendir(save_root); struct dirent *entry; struct save_record record; int result=0;
    if(!walk) return -1;
    for(;;) {
        errno=0; entry=readdir(walk);
        if(!entry) { if(errno) result=-1; break; }
        if(read_record(entry->d_name,&record,NULL) && equal_name(name,record.name)) {
            strcpy(directory,entry->d_name); result=1; break;
        }
    }
    if(closedir(walk)) result=-1;
    return result;
}
static unsigned int create_locked(const unsigned short *name,unsigned int disposition,unsigned int flags,char *path,unsigned int size)
{
    char directory[32],native[512],metadata[512],result_path[260]; struct stat st;
    struct save_record record={.magic=0x56533348U,.version=1,.flags=flags};
    if(!valid_name(name) || (disposition!=1 && disposition!=3 && disposition!=4) || (flags&~1U) || !path || size<18) return INVALID;
    if(!root_ready()) return ACCESS;
    int found=locate(name,directory);
    if(found<0) return IO_ERROR;
    if(found && disposition==1) return EXISTS;
    if(!found && disposition==3) return NOT_FOUND;
    if(!found) {
        unsigned int hash=name_hash(name),attempt;
        for(attempt=0;attempt<1024;++attempt) {
            snprintf(directory,sizeof(directory),"n3ds-%08x",hash+attempt);
            snprintf(native,sizeof(native),"%s/%s",save_root,directory);
            if(stat(native,&st)) { if(errno==ENOENT) break; return IO_ERROR; }
        }
        if(attempt==1024) return EXISTS;
        if(mkdir(native,0777)) return IO_ERROR;
        for(unsigned int i=0;name[i];++i) record.name[i]=name[i];
        record.checksum=checksum(record);
        snprintf(metadata,sizeof(metadata),"%s/%s/name.h3s",save_root,directory);
        FILE *file=fopen(metadata,"wb");
        int success=file && fwrite(&record,1,sizeof(record),file)==sizeof(record);
        if(file && fclose(file)) success=0;
        if(!success) { unlink(metadata); rmdir(native); return IO_ERROR; }
    }
    snprintf(result_path,sizeof(result_path),"u:\\%s\\",directory);
    memcpy(path,result_path,strlen(result_path)+1); return OK;
}
unsigned int n3ds_save_create(const unsigned short *name,unsigned int disposition,unsigned int flags,char *path,unsigned int size)
{
    n3ds_files_lock(); unsigned int result=create_locked(name,disposition,flags,path,size); n3ds_files_unlock(); return result;
}
/* Preflight the bounded tree before removing any file. All file operations use
 * the same service lock, so no game file can open between the check and removal. */
static int walk_tree(const char *path,unsigned int depth,int remove)
{
    char child[512]; struct dirent *entry; struct stat st; int result=1;
    if(depth>8) return 0;
    DIR *walk=opendir(path); if(!walk) return 0;
    for(;;) {
        errno=0; entry=readdir(walk);
        if(!entry) { if(errno) result=0; break; }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        int n=snprintf(child,sizeof(child),"%s/%s",path,entry->d_name);
        if(n<0 || (unsigned int)n>=sizeof(child) || stat(child,&st)) { result=0; break; }
        if(S_ISDIR(st.st_mode)) { if(!walk_tree(child,depth+1,remove)) { result=0; break; } }
        else if(!S_ISREG(st.st_mode) || (remove && unlink(child))) { result=0; break; }
    }
    if(closedir(walk)) result=0;
    return result && (!remove || !rmdir(path));
}
unsigned int n3ds_save_delete(const unsigned short *name)
{
    char directory[32],native[512],path[64]; unsigned int result=INVALID;
    if(!valid_name(name)) return result;
    n3ds_files_lock();
    if(!root_ready()) result=ACCESS;
    else {
        int found=locate(name,directory);
        if(found<=0) result=found?IO_ERROR:NOT_FOUND;
        else {
            snprintf(path,sizeof(path),"u:\\%s",directory);
            snprintf(native,sizeof(native),"%s/%s",save_root,directory);
            if(n3ds_files_path_in_use(path)) result=BUSY;
            else result=walk_tree(native,0,0) && walk_tree(native,0,1)?OK:IO_ERROR;
        }
    }
    n3ds_files_unlock(); return result;
}
static struct save_iterator *iterator(unsigned int token)
{
    struct save_iterator *slot=&iterators[token&7];
    return (token&0xf0000000U)==0x50000000U && slot->directory && slot->generation==((token&0x0fffffffU)>>3)?slot:NULL;
}
static int next_locked(struct save_iterator *slot,struct native_save_info *info)
{
    struct dirent *entry; struct save_record record;
    while((entry=readdir(slot->directory))) if(read_record(entry->d_name,&record,info)) return 1;
    return 0;
}
unsigned int n3ds_save_find_first(struct native_save_info *info)
{
    unsigned int token=0;
    if(!info) return 0;
    n3ds_files_lock();
    if(root_ready()) for(unsigned int i=0;i<ITERATORS;++i) {
        struct save_iterator *slot=&iterators[i];
        if(slot->directory || slot->generation==MAX_GENERATION) continue;
        slot->directory=opendir(save_root);
        if(!slot->directory) break;
        if(next_locked(slot,info)) { ++slot->generation; token=0x50000000U|(slot->generation<<3)|i; }
        else { closedir(slot->directory); slot->directory=NULL; }
        break;
    }
    n3ds_files_unlock(); return token;
}
int n3ds_save_find_next(unsigned int token,struct native_save_info *info)
{
    int result=0; n3ds_files_lock(); struct save_iterator *slot=iterator(token);
    if(slot && info) result=next_locked(slot,info);
    n3ds_files_unlock(); return result;
}
int n3ds_save_find_close(unsigned int token)
{
    int result=0; n3ds_files_lock(); struct save_iterator *slot=iterator(token);
    if(slot) { result=closedir(slot->directory)==0; slot->directory=NULL; }
    n3ds_files_unlock(); return result;
}
unsigned int n3ds_save_find_count(void)
{
    unsigned int count=0; n3ds_files_lock();
    for(unsigned int i=0;i<ITERATORS;++i) count+=iterators[i].directory!=NULL;
    n3ds_files_unlock(); return count;
}
int n3ds_disk_space(const char *path,unsigned long long *available,unsigned long long *total,unsigned long long *free_bytes)
{
    char native[512]; struct statvfs space; struct native_file_info info; int result=0;
    if(!available || !total || !free_bytes || !n3ds_file_resolve(path,native,sizeof(native),0)) return 0;
    n3ds_files_lock();
    if(n3ds_save_root_valid(path)) root_ready();
    if(n3ds_file_stat(path,&info) && info.directory && !statvfs(native,&space)) {
        *total=(unsigned long long)space.f_blocks*space.f_frsize;
        *free_bytes=(unsigned long long)space.f_bfree*space.f_frsize;
        *available=info.readonly?0:(unsigned long long)space.f_bavail*space.f_frsize; result=1;
    }
    n3ds_files_unlock(); return result;
}
