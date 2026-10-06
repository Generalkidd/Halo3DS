#include "cseries.h"
#include <xtl.h>
#include "engine_settings.h"
int n3ds_engine_read_cfg_language(void);
void n3ds_log(const char *message);
static wchar_t machine_name[32]=L"New Nintendo 3DS";
unsigned int n3ds_engine_language_from_cfg(int language)
{
    switch(language) {
    case 0:return XC_LANGUAGE_JAPANESE;
    case 1:return XC_LANGUAGE_ENGLISH;
    case 2:return XC_LANGUAGE_FRENCH;
    case 3:return XC_LANGUAGE_GERMAN;
    case 4:return XC_LANGUAGE_ITALIAN;
    case 5:return XC_LANGUAGE_SPANISH;
    default:return XC_LANGUAGE_ENGLISH;
    }
}
DWORD WINAPI XGetLanguage(void)
{
    int language=n3ds_engine_read_cfg_language();
    if(language<0 || language>5)
        n3ds_log("Console language unavailable or unsupported by Halo Xbox; using English");
    return n3ds_engine_language_from_cfg(language);
}
int n3ds_engine_machine_name_set(const wchar_t *name)
{
    unsigned int length;
    if(!name || !*name) return 0;
    for(length=0;length<32 && name[length];++length) {}
    if(length==32) return 0;
    memcpy(machine_name,name,(length+1)*sizeof(*name));
    memset(machine_name+length+1,0,(31-length)*sizeof(*name));
    n3ds_log("Native machine name updated for this application session only");
    return 1;
}
void n3ds_engine_machine_name_get(wchar_t *name)
{ memcpy(name,machine_name,sizeof(machine_name)); }
