#include "cseries.h"
#include <xtl.h>
#include "engine_settings.h"
#include "networking/network_game_manager.h"
#include "interface/marketing_and_strategic_business_development.h"
#include "rasterizer/rasterizer.h"
void n3ds_log(const char *message);
void rasterizer_model_ambient_reflection_tint(real alpha,real red,real green,real blue);
#define CHECK(e) do { if(!(e)) { n3ds_log("SETTINGS FAIL: " #e); return 1; } } while(0)
int halo_engine_settings_tests(void)
{
    static const unsigned int expected[]={2,1,4,3,6,5,1,1,1,1,1,1};
    wchar_t original[32],name[32],long_name[33];
    unsigned int i;
    DWORD language;
    char message[96];
    for(i=0;i<NUMBEROF(expected);++i) CHECK(n3ds_engine_language_from_cfg(i)==expected[i]);
    CHECK(n3ds_engine_language_from_cfg(-1)==XC_LANGUAGE_ENGLISH);
    CHECK(n3ds_engine_language_from_cfg(1000)==XC_LANGUAGE_ENGLISH);
    language=XGetLanguage(); CHECK(language>=1 && language<=6);
    snprintf(message,sizeof(message),"NATIVE SETTINGS: Halo language=%lu",language); n3ds_log(message);
    network_game_generate_local_machine_name(original); CHECK(original[0] && !original[31]);
    xbox_set_machine_name("Halo3DS Test"); network_game_generate_local_machine_name(name);
    CHECK(!memcmp(name,L"Halo3DS Test",12*sizeof(wchar_t)));
    for(i=0;i<32;++i) long_name[i]='A'; long_name[32]=0;
    CHECK(!n3ds_engine_machine_name_set(long_name));
    CHECK(!n3ds_engine_machine_name_set(NULL) && !n3ds_engine_machine_name_set(L""));
    network_game_generate_local_machine_name(name); CHECK(!memcmp(name,L"Halo3DS Test",12*sizeof(wchar_t)));
    long_name[31]=0; CHECK(n3ds_engine_machine_name_set(long_name));
    network_game_generate_local_machine_name(name); CHECK(!memcmp(name,long_name,sizeof(name)));
    CHECK(n3ds_engine_machine_name_set(original));
    CHECK(!xbox_demos_available()); xbox_demos_launch(); xbox_dashboard_launch(); CHECK(!xbox_demos_available());
    CHECK(!global_rasterizer_model_ambient_reflection_tint);
    rasterizer_model_ambient_reflection_tint(1,2,3,4);
    n3ds_log("PASS: native language mapping and CFG query, bounded session-only machine names, explicit unavailable Xbox launches and null-safe tint setter");
    return 0;
}
int halo_engine_tint_state_tests(void)
{
    real_argb_color saved;
    CHECK(global_rasterizer_model_ambient_reflection_tint);
    saved=*global_rasterizer_model_ambient_reflection_tint;
    rasterizer_model_ambient_reflection_tint(-.25f,2.f,.5f,4.f);
    CHECK(global_rasterizer_model_ambient_reflection_tint->alpha==-.25f &&
        global_rasterizer_model_ambient_reflection_tint->red==2.f &&
        global_rasterizer_model_ambient_reflection_tint->green==.5f &&
        global_rasterizer_model_ambient_reflection_tint->blue==4.f);
    *global_rasterizer_model_ambient_reflection_tint=saved;
    n3ds_log("PASS: original tint setter values stored without clamping and restored; reflection rendering not tested");
    return 0;
}
