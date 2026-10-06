#include "cseries.h"
#include <xtl.h>
#include "engine_saves.h"
_Static_assert(sizeof(WCHAR)==2 && sizeof(XGAME_FIND_DATA)==836,"Xbox save metadata ABI");
_Static_assert(offsetof(XGAME_FIND_DATA,szSaveGameName)==580,"Xbox save name offset");
static void publish(const struct native_save_info *native,XGAME_FIND_DATA *out)
{
    memset(out,0,sizeof(*out));
    memcpy(out->szSaveGameName,native->name,sizeof(native->name));
    strcpy(out->szSaveGameDirectory,native->path);
    strcpy(out->wfd.cFileName,native->path+3);
    size_t length=strlen(out->wfd.cFileName);
    if(length && out->wfd.cFileName[length-1]=='\\') out->wfd.cFileName[length-1]=0;
    out->wfd.dwFileAttributes=FILE_ATTRIBUTE_DIRECTORY;
    memcpy(&out->wfd.ftLastWriteTime,native->date,sizeof(native->date));
    out->wfd.ftCreationTime=out->wfd.ftLastAccessTime=out->wfd.ftLastWriteTime;
}
DWORD WINAPI XCreateSaveGame(LPCSTR root,LPCWSTR name,DWORD disposition,DWORD flags,LPSTR path,UINT size)
{ return n3ds_save_root_valid(root)?n3ds_save_create((const unsigned short *)name,disposition,flags,path,size):ERROR_INVALID_PARAMETER; }
DWORD WINAPI XDeleteSaveGame(LPCSTR root,LPCWSTR name)
{ return n3ds_save_root_valid(root)?n3ds_save_delete((const unsigned short *)name):ERROR_INVALID_PARAMETER; }
HANDLE WINAPI XFindFirstSaveGame(LPCSTR root,PXGAME_FIND_DATA data)
{
    struct native_save_info native; unsigned int token;
    if(!data || !n3ds_save_root_valid(root) || !(token=n3ds_save_find_first(&native))) return INVALID_HANDLE_VALUE;
    publish(&native,data); return (HANDLE)(unsigned long)token;
}
BOOL WINAPI XFindNextSaveGame(HANDLE handle,PXGAME_FIND_DATA data)
{
    struct native_save_info native;
    if(!data || !n3ds_save_find_next((unsigned long)handle,&native)) return FALSE;
    publish(&native,data); return TRUE;
}
BOOL WINAPI XFindClose(HANDLE handle) { return n3ds_save_find_close((unsigned long)handle); }
BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR path,PULARGE_INTEGER available,PULARGE_INTEGER total,PULARGE_INTEGER free_bytes)
{
    unsigned long long a,t,f;
    if(!n3ds_disk_space(path,&a,&t,&f)) return FALSE;
    if(available) available->QuadPart=a;
    if(total) total->QuadPart=t;
    if(free_bytes) free_bytes->QuadPart=f;
    return TRUE;
}
