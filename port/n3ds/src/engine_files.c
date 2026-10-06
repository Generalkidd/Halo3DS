#include "cseries.h"
#define BUILDING_FILES_WINDOWS
#include "tag_files/files.h"
#include "engine_files.h"
void n3ds_log(const char *message);
_Static_assert(sizeof(struct file_reference_info)==FILE_REFERENCE_SIZE,"File reference handle ABI");
_Static_assert(offsetof(struct file_reference_info,file_handle)==264,"File reference handle offset");

static char find_root[256];
static short find_location;
static const struct file_reference_info *info_for(const struct file_reference *file)
{ return file_reference_get_info((struct file_reference *)file); }
static unsigned int handle_for(const struct file_reference *file)
{ return (unsigned int)(unsigned long)info_for(file)->file_handle; }
static void path_for(const struct file_reference *file, char *path)
{
    const struct file_reference_info *info=info_for(file);
    /* Retain the original Xbox path syntax at the engine boundary. */
    file_location_get_full_path(info->location,info->path,path);
}
static boolean result_for(const struct file_reference *file, const char *operation, int success)
{
    if(!success) {
        char message[384];
        snprintf(message,sizeof(message),"Native file %s failed: %s",operation,info_for(file)->path);
        n3ds_log(message);
    }
    return success!=0;
}
boolean file_create(struct file_reference *file)
{
    char path[256]; path_for(file,path);
    return result_for(file,"create",n3ds_file_create(path,!(info_for(file)->flags&1)));
}
boolean file_delete(struct file_reference *file)
{
    char path[256]; path_for(file,path);
    return result_for(file,"delete",n3ds_file_delete(path,!(info_for(file)->flags&1)));
}
boolean file_exists(const struct file_reference *file)
{
    char path[256]; struct native_file_info info; path_for(file,path);
    return n3ds_file_stat(path,&info);
}
boolean file_read_only(struct file_reference *file)
{
    char path[256]; struct native_file_info info; path_for(file,path);
    return n3ds_file_stat(path,&info) && info.readonly;
}
boolean file_open(struct file_reference *file, unsigned long permissions)
{
    struct file_reference_info *info=file_reference_get_info(file);
    char path[256]; unsigned int handle;
    assert(VALID_FLAGS(permissions,NUMBER_OF_PERMISSION_FLAGS) && (permissions&3));
    assert(!(permissions&4) || (permissions&2));
    if(!(info->flags&1) || n3ds_file_valid(handle_for(file))) return FALSE;
    path_for(file,path); handle=n3ds_file_open(path,permissions);
    info->file_handle=(void *)(unsigned long)handle;
    return result_for(file,"open",handle!=0);
}
boolean file_close(struct file_reference *file)
{
    struct file_reference_info *info=file_reference_get_info(file);
    int result=n3ds_file_close(handle_for(file));
    /* fclose consumes the handle even when flushing reports an error. */
    info->file_handle=NULL;
    return result_for(file,"close",result);
}
unsigned long file_get_position(const struct file_reference *file) { return n3ds_file_position(handle_for(file)); }
unsigned long file_get_eof(const struct file_reference *file) { return n3ds_file_size(handle_for(file)); }
boolean file_set_position(const struct file_reference *file, unsigned long position)
{ return result_for(file,"seek",n3ds_file_seek(handle_for(file),position)); }
boolean file_set_eof(const struct file_reference *file, unsigned long position)
{ return result_for(file,"resize",n3ds_file_resize(handle_for(file),position)); }
boolean file_read(const struct file_reference *file, unsigned long count, void *buffer)
{ assert(buffer); return result_for(file,"read",n3ds_file_read(handle_for(file),buffer,count)); }
boolean file_write(const struct file_reference *file, unsigned long count, const void *buffer)
{ assert(buffer); return result_for(file,"write",n3ds_file_write(handle_for(file),buffer,count)); }
boolean file_read_from_position(const struct file_reference *file, unsigned long position, unsigned long count, void *buffer)
{ assert(buffer); return result_for(file,"read at position",n3ds_file_read_at(handle_for(file),position,buffer,count)); }
boolean file_write_to_position(const struct file_reference *file, unsigned long position, unsigned long count, const void *buffer)
{ assert(buffer); return result_for(file,"write at position",n3ds_file_write_at(handle_for(file),position,buffer,count)); }
boolean file_get_size(const struct file_reference *file, unsigned long *size)
{
    char path[256]; struct native_file_info info; assert(size); path_for(file,path);
    if(!result_for(file,"stat",n3ds_file_stat(path,&info))) return FALSE;
    *size=info.size; return TRUE;
}
boolean file_get_last_modification_date(struct file_reference *file, struct file_last_modification_date *date)
{
    char path[256]; struct native_file_info info; assert(date); path_for(file,path);
    memset(date,0,sizeof(*date));
    if(!result_for(file,"date",n3ds_file_stat(path,&info))) return FALSE;
    memcpy(date,info.date,sizeof(*date)); return TRUE;
}
boolean file_rename(struct file_reference *file, const char *name)
{
    char old_path[256],new_path[256]; struct file_reference_info *info=file_reference_get_info(file);
    assert(name);
    if(!*name || strchr(name,'\\') || strchr(name,'/') || strchr(name,':') || n3ds_file_valid(handle_for(file))) return FALSE;
    path_for(file,old_path); strcpy(new_path,old_path); file_path_remove_name(new_path);
    if(strlen(new_path)+strlen(name)+1>MAXIMUM_FILENAME_LENGTH) return FALSE;
    file_path_add_name(new_path,name);
    if(!result_for(file,"rename",n3ds_file_rename(old_path,new_path))) return FALSE;
    file_path_remove_name(info->path); file_path_add_name(info->path,name); return TRUE;
}
static void find_files_start_locked(unsigned long flags, const struct file_reference *directory)
{
    const struct file_reference_info *info=info_for(directory);
    char path[256]; assert(VALID_FLAGS(flags,NUMBER_OF_FIND_FILES_FLAGS) && !(info->flags&1));
    path_for(directory,path);
    if(result_for(directory,"enumerate",n3ds_files_find_start(path,flags))) {
        strcpy(find_root,info->path); find_location=info->location;
    }
}
static boolean find_files_next_locked(struct file_reference *file, struct file_last_modification_date *date)
{
    char relative[256]; struct native_file_info info;
    int result=n3ds_files_find_next(relative,sizeof(relative),&info);
    if(result<=0) { if(result<0) n3ds_log("Native file enumeration failed"); return FALSE; }
    if(strlen(find_root)+strlen(relative)+1>MAXIMUM_FILENAME_LENGTH) {
        n3ds_log("Native enumeration exceeds original file path limit"); n3ds_files_find_end(); return FALSE;
    }
    file_reference_create(file,find_location); file_reference_add_directory(file,find_root);
    if(info.directory) file_reference_add_directory(file,relative);
    else file_reference_set_name(file,relative);
    if(date) memcpy(date,info.date,sizeof(*date));
    return TRUE;
}
void find_files_start(unsigned long flags, const struct file_reference *directory)
{
    n3ds_files_lock(); find_files_start_locked(flags,directory); n3ds_files_unlock();
}
boolean find_files_next(struct file_reference *file, struct file_last_modification_date *date)
{
    n3ds_files_lock(); boolean result=find_files_next_locked(file,date); n3ds_files_unlock(); return result;
}
