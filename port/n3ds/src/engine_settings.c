#include "cseries.h"
#include <xtl.h>
#include "engine_settings.h"
int n3ds_engine_read_cfg_language(void);
void n3ds_log(const char *message);
int n3ds_engine_read_cfg_model(void);
static wchar_t machine_name[32];
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
{
    if(!machine_name[0])
    {
        static const wchar_t *const models[]={L"Nintendo 3DS",L"Nintendo 3DS XL",
            L"New Nintendo 3DS",L"Nintendo 2DS",L"New Nintendo 3DS XL",L"New Nintendo 2DS XL"};
        int model=n3ds_engine_read_cfg_model();
        const wchar_t *label=model>=0 && model<6?models[model]:L"Nintendo 3DS family";
        unsigned int i=0;
        while(label[i] && i<31) {machine_name[i]=label[i];++i;}
        machine_name[i]=0;
    }
    memcpy(name,machine_name,sizeof(machine_name));
}
