/* New3DS integration additions by Kevin Boggs. */
#include "engine_checkpoint.h"
#if defined(HALO_N3DS_EXPERIMENTAL_PC_MAP_ID) || defined(HALO_N3DS_EXPERIMENTAL_PC_MATRIX)
#include "pc_diagnostic_maps.h"
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ALL_MAPS
short n3ds_ui_pc_expansion_map_count(void);
#endif
/* Original main-menu widgets and lifecycle on the native renderer. */
#include "cseries.h"
#include "sound/sound_manager.h"
#include "cache/sound_cache.h"
#include "units/units.h"
#include "engine_game.h"
#include "engine_hud.h"
#include "engine_controls.h"
#include "engine_generic.h"
#include "engine_chicago.h"
#include "engine_transparent.h"
#include "errors.h"
#include "game_state.h"
#include "game.h"
#include "game/game_engine.h"
#include "players.h"
#if defined(HALO_N3DS_TEXTURE_CLIP_DEATH) || defined(HALO_N3DS_SCENIC_TEXTURE)
#include "game/players_runtime.h"
#include "math/real_math.h"
#endif
#include "input/input.h"
#include "input/input_abstraction.h"
#include "engine_input.h"
#include "engine_renderer.h"
#include "engine_models.h"
#include "engine_textures.h"
#include "engine_bitmaps.h"
#include "rasterizer/rasterizer.h"
#include "main/console.h"
#include "main/main.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "interface/ui_widget.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "objects/objects.h"
#include "player_control.h"
#include "cutscene/cinematics.h"
#include "render/render_sprite.h"
#include "engine_widgets.h"
#include "engine_dynamic_geometry.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "physics/collision_usage.h"
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MAP_NAME
#include "objects/objects.h"
#include "tag_files/tag_groups.h"
#include "items/weapons.h"
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
#include "units/unit_definitions.h"
#include "game/game_globals.h"
#include "effects/particles.h"
#include "effects/particle_systems.h"
#include "effects/contrails.h"
#include "effects/decals.h"
extern struct data_array *global_decal_data;
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
#include "units/unit_definitions.h"
#include "tag_files/tag_groups.h"
#include "items/weapons.h"
boolean unit_get_seat_entrance_point(long,long,short,real_point3d *,real_point3d *,real_point3d *);
#endif
#ifdef HALO_N3DS_MAP_CYCLE_TEST
#include "tag_files/tag_groups.h"
static const char *native_cycle_maps[]={"a10","a30","a50","b30","b40","c10","c20","c40","d20","d40"};
#endif
#ifdef HALO_N3DS_MAW_CUTSCENE_TEST
#include "scenario/scenario_definitions.h"
#include "hs/hs.h"
#include "hs/hs_scenario_definitions.h"
#endif
#ifdef HALO_N3DS_SNIPER_INVENTORY_TEST
#include "objects/objects.h"
#include "tag_files/tag_groups.h"
#include "items/weapons.h"
#endif
void n3ds_log(const char *);
/* Defined only by the opt-in controller fixture. Ordinary builds retain the
 * original clock-based local random seed and do not link the fixture. */
void __attribute__((weak)) n3ds_frontend_fixture_seed(void) {}
void n3ds_main_frontend_initialize(void);
void n3ds_main_frontend_services(void);
void n3ds_main_mission_save_step(boolean);
void player_control_update(real);
void event_manager_update(void);
void bink_playback_update(void);
void n3ds_mission_combat_trace(long,long);
int n3ds_engine_app_running(void);
int n3ds_engine_float_tests(void);
int n3ds_animation_alignment_tests(void);
int n3ds_hs_stack_tests(void);
long long n3ds_engine_ticks(void);
void n3ds_engine_pace_frame(long long);
int n3ds_engine_mission_render(long,long);
#ifndef HALO_N3DS_MISSION_TICKS
#define HALO_N3DS_MISSION_TICKS 180
#endif
#define CHECK(e) do {if(!(e)){n3ds_log("FRONTEND FAIL: " #e);return 1;}}while(0)
static long current_frame;
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
struct menu_key_event { long frame; unsigned int mask; };
static struct menu_key_event menu_key_events[64];
static unsigned int menu_key_event_count;
static long menu_key_capture_frame;
static int menu_kill_player;
static int menu_go_to_extra;
static int menu_go_to_b30_extra;
static int menu_survey_extras;
static int menu_enter_b30_extra;
static int menu_b30_wait_logged;
static int menu_try_b30_extra;
static long menu_b30_action_frame;
static long menu_b30_natural_vehicle;
static long menu_b30_fire_start;
static long menu_b30_vehicle;
static long menu_b30_weapon;
static long menu_b30_initial_fired;
/* Kevin Boggs: menu_keys_load - New3DS integration. */
static int menu_keys_load(void)
{
    static const struct { const char *name; unsigned int mask; } names[]={
        {"A",NATIVE_KEY_A},{"B",NATIVE_KEY_B},{"X",NATIVE_KEY_X},{"UP",NATIVE_KEY_DUP},
        {"DOWN",NATIVE_KEY_DDOWN},{"LEFT",NATIVE_KEY_DLEFT},
        {"RIGHT",NATIVE_KEY_DRIGHT},{"START",NATIVE_KEY_START},{"SELECT",NATIVE_KEY_SELECT},
        {"TAP_EXTRAS",NATIVE_KEY_TOUCH},
        {"LOAD_C20",0x80000000u},{"LOAD_C40",0x40000000u},{"LOAD_D20",0x20000000u},
        {"LOAD_B30",0x08000000u},
        {"GOTO_C20_EXTRA",0x10000000u},{"GOTO_B30_EXTRA",0x04000000u},
        {"SURVEY_EXTRAS",0x02000000u},{"ENTER_B30_EXTRA",0x01000000u},
        {"TRY_B30_EXTRA",0x00800000u},
        {"OPEN_SETTINGS",0x00400000u},{"TAP_HIGH_TEXTURES",0x00200000u},
        {"TAP_SAVE_SETTINGS",0x00080000u},{"TAP_PC_TEXTURES",0x00040000u},
        {"KILL_PLAYER",0x00020000u}
    };
    FILE *file=fopen("sdmc:/halo-source/menu-keys.txt","rb");
    char line[64],name[32],extra;
#ifdef HALO_N3DS_SCENIC_TEXTURE
    const long maximum_frame=6000;
#else
    const long maximum_frame=2400;
#endif
    if(!file) return 1;
    while(fgets(line,sizeof(line),file)) {
        long frame;unsigned int mask=0;
        if(!strchr(line,'\n') && !feof(file)) {fclose(file);return 1;}
        if(sscanf(line,"capture %ld %c",&frame,&extra)==1) {
            if(menu_key_capture_frame || frame<1 || frame>maximum_frame) {fclose(file);return 1;}
            menu_key_capture_frame=frame;
            continue;
        }
        if(sscanf(line,"%ld %31s %c",&frame,name,&extra)!=2 || frame<1 || frame>maximum_frame ||
           menu_key_event_count>=sizeof(menu_key_events)/sizeof(menu_key_events[0])) {fclose(file);return 1;}
        for(unsigned int i=0;i<sizeof(names)/sizeof(names[0]);++i)
            if(!strcmp(name,names[i].name)) {mask=names[i].mask;break;}
        if(!mask) {fclose(file);return 1;}
        menu_key_events[menu_key_event_count++]=(struct menu_key_event){frame,mask};
    }
    int failed=ferror(file) || !menu_key_capture_frame;
    fclose(file);
    return failed;
}
/* Kevin Boggs: menu_keys_sample - New3DS integration. */
static void menu_keys_sample(long frame,struct native_input_sample *sample)
{
    for(unsigned int i=0;i<menu_key_event_count;++i) if(menu_key_events[i].frame==frame) {
        char message[96];
        unsigned int mask=menu_key_events[i].mask;
        if(mask==0x80000000u || mask==0x40000000u || mask==0x20000000u || mask==0x08000000u) {
            const char *name=mask==0x80000000u?"c20":mask==0x40000000u?"c40":
                mask==0x20000000u?"d20":"b30";
            char path[40];snprintf(path,sizeof(path),"levels\\%s\\%s",name,name);
            main_set_map_name(path);
        } else if(mask==0x10000000u) menu_go_to_extra=1;
        else if(mask==0x00020000u) menu_kill_player=1;
        else if(mask==0x04000000u) menu_go_to_b30_extra=1;
        else if(mask==0x02000000u) menu_survey_extras=1;
        else if(mask==0x01000000u) menu_enter_b30_extra=1;
        else if(mask==0x00800000u) menu_try_b30_extra=1;
        else if(mask==0x00400000u) {
            extern void n3ds_controls_open(int);
            n3ds_controls_open(0);
        } else if(mask==0x00200000u || mask==0x00080000u || mask==0x00040000u) {
            sample->held|=NATIVE_KEY_TOUCH;
            sample->touch_x=mask==0x00040000u?150:280;
            sample->touch_y=mask==0x00200000u?166:mask==0x00040000u?157:214;
        }
        else sample->held|=mask;
        if(menu_key_events[i].mask==NATIVE_KEY_TOUCH) {sample->touch_x=160;sample->touch_y=120;}
        snprintf(message,sizeof(message),"MENU KEYS: frame=%ld mask=%08x",frame,menu_key_events[i].mask);
        n3ds_log(message);
    }
}
/* Kevin Boggs: menu_keys_enter_b30_extra - New3DS integration. */
static void menu_keys_enter_b30_extra(long frame,long unit)
{
    if(menu_enter_b30_extra && unit!=NONE &&
       !strcmp(main_get_map_name(),"levels\\b30\\b30")) {
        if(unit_get(unit)->object.parent_object_index!=NONE) {
            if(!menu_b30_wait_logged) {
                n3ds_log("MENU KEYS: b30 waiting for player to leave cinematic transport");
                menu_b30_wait_logged=1;
            }
            return;
        }
        menu_enter_b30_extra=0;
        long definition=tag_loaded('vehi',"vehicles\\rwarthog\\rwarthog");
        struct object_iterator iterator;
        object_iterator_new(&iterator,_object_mask_vehicle,0);
        struct object_datum *object;
        menu_b30_vehicle=NONE;
        while((object=object_iterator_next(&iterator))!=NULL) {
            if(object->definition_index==definition &&
               object->object.position.x>-18.f && object->object.position.x<-14.f &&
               object->object.position.y>41.f && object->object.position.y<44.f) {
                menu_b30_vehicle=iterator.index;break;
            }
        }
        if(menu_b30_vehicle==NONE) {n3ds_log("MENU KEYS: b30 vehicle absent at spawn");return;}
        real_point3d entry_point={-16.11f,38.75f,3.54f};
        object_set_position(unit,&entry_point,NULL,NULL);
        struct unit_definition *vehicle_definition=unit_definition_get(definition);
        if(!vehicle_definition) {n3ds_log("MENU KEYS: b30 vehicle definition absent");return;}
        short gunner=NONE;
        for(short i=0;i<vehicle_definition->unit.seats.count;++i) {
            struct unit_seat *seat=TAG_BLOCK_GET_ELEMENT(&vehicle_definition->unit.seats,i,struct unit_seat);
            if(TEST_FLAG(seat->flags,_unit_seat_gunner_bit)) {gunner=i;break;}
        }
        if(gunner==NONE || !unit_enter_seat(unit,menu_b30_vehicle,gunner)) {
            n3ds_log("MENU KEYS: b30 gunner entry rejected");return;
        }
        struct unit_datum *vehicle=unit_try_and_get(menu_b30_vehicle);
        menu_b30_weapon=NONE;
        if(vehicle)for(short i=0;i<MAXIMUM_WEAPONS_PER_UNIT;++i)
            if(vehicle->unit.weapon_object_indices[i]!=NONE) {
                menu_b30_weapon=vehicle->unit.weapon_object_indices[i];break;
            }
        struct weapon_datum *weapon=weapon_try_and_get(menu_b30_weapon);
        menu_b30_initial_fired=weapon?weapon->weapon.game_time_last_fired:NONE;
        menu_b30_fire_start=frame+20;
        char detail[175];
        snprintf(detail,sizeof(detail),"MENU KEYS: b30 gunner entered vehicle=%08lx seat=%d gun=%08lx initial_fired=%ld",
            menu_b30_vehicle,gunner,menu_b30_weapon,menu_b30_initial_fired);
        n3ds_log(detail);
    }
    if(menu_b30_fire_start && frame==menu_b30_fire_start+100) {
        struct weapon_datum *weapon=weapon_try_and_get(menu_b30_weapon);
        long fired=weapon?weapon->weapon.game_time_last_fired:NONE;
        char detail[160];
        snprintf(detail,sizeof(detail),"MENU KEYS: b30 rocket fire gun=%08lx fired=%ld initial=%ld %s",
            menu_b30_weapon,fired,menu_b30_initial_fired,
            weapon && fired>menu_b30_initial_fired?"PASS":"FAIL");
        n3ds_log(detail);
    }
}
/* Kevin Boggs: menu_keys_try_b30_extra - New3DS integration. */
static void menu_keys_try_b30_extra(long frame,long unit)
{
    if(menu_try_b30_extra && unit!=NONE &&
       !strcmp(main_get_map_name(),"levels\\b30\\b30")) {
        if(!player_input_enabled() || cinematic_in_progress())return;
        long definition=tag_loaded('vehi',"vehicles\\rwarthog\\rwarthog");
        long parent=unit_get(unit)->object.parent_object_index;
        if(parent!=NONE) {
            struct object_datum *parent_object=object_try_and_get(parent);
            if(!parent_object || parent_object->definition_index!=definition)return;
            unit_exit_seat_end(unit);
            n3ds_log("MENU KEYS: b30 exited checkpoint Rocket Warthog before X entry test");
            if(unit_get(unit)->object.parent_object_index!=NONE)return;
        }
        menu_try_b30_extra=0;
        struct object_iterator iterator;
        object_iterator_new(&iterator,_object_mask_vehicle,0);
        struct object_datum *object;
        menu_b30_natural_vehicle=NONE;
        while((object=object_iterator_next(&iterator))!=NULL)
            if(object->definition_index==definition &&
               object->object.position.x>-18.f && object->object.position.x<-14.f &&
               object->object.position.y>41.f && object->object.position.y<44.f) {
                menu_b30_natural_vehicle=iterator.index;break;
            }
        if(menu_b30_natural_vehicle==NONE) {
            n3ds_log("MENU KEYS: b30 natural entry vehicle missing");return;
        }
        struct unit_definition *vehicle_definition=unit_definition_get(definition);
        int found=0;
        if(vehicle_definition)for(short seat=0;seat<vehicle_definition->unit.seats.count;++seat) {
            real_point3d entrance,seat_point;
            if(!unit_get_seat_entrance_point(unit,menu_b30_natural_vehicle,seat,
                &entrance,&seat_point,NULL))continue;
            real_point3d points[]={entrance,seat_point};
            for(unsigned int i=0;i<sizeof(points)/sizeof(points[0]);++i) {
                struct location location;
                scenario_location_from_point(&location,&points[i]);
                if(location.cluster_index==NONE)continue;
                object_set_position(unit,&points[i],NULL,NULL);
                short nearby=NONE;
                short state=unit_find_nearby_seat(unit,menu_b30_natural_vehicle,&nearby);
                if(state!=_unit_nearby_seat_available)continue;
                char detail[150];
                snprintf(detail,sizeof(detail),"MENU KEYS: b30 natural entry marker seat=%d nearby=%d cluster=%d",
                    seat,nearby,location.cluster_index);
                n3ds_log(detail);
                found=1;break;
            }
            if(found)break;
        }
        static const real_point3d candidates[]={
            {-16.11f,40.75f,3.54f},{-18.11f,42.75f,3.54f},
            {-14.11f,42.75f,3.54f},{-16.11f,38.75f,3.54f}
        };
        for(unsigned int i=0;!found && i<sizeof(candidates)/sizeof(candidates[0]);++i) {
            struct location location;
            scenario_location_from_point(&location,&candidates[i]);
            if(location.cluster_index==NONE)continue;
            real_vector3d forward={0.f,1.f,0.f};
            object_set_position(unit,&candidates[i],&forward,NULL);
            short nearby=NONE;
            short state=unit_find_nearby_seat(unit,menu_b30_natural_vehicle,&nearby);
            if(state!=_unit_nearby_seat_available)continue;
            char detail[140];
            snprintf(detail,sizeof(detail),"MENU KEYS: b30 natural entry point=%.2f,%.2f,%.2f cluster=%d",
                candidates[i].x,candidates[i].y,candidates[i].z,location.cluster_index);
            n3ds_log(detail);
            found=1;break;
        }
        if(!found) {n3ds_log("MENU KEYS: b30 natural entry point unavailable");return;}
        menu_b30_action_frame=frame+5;
    }
    if(menu_b30_action_frame && frame==menu_b30_action_frame+90) {
        long parent=unit!=NONE?unit_get(unit)->object.parent_object_index:NONE;
        char detail[130];
        snprintf(detail,sizeof(detail),"MENU KEYS: b30 natural X entry parent=%08lx target=%08lx %s",
            parent,menu_b30_natural_vehicle,
            parent==menu_b30_natural_vehicle?"PASS":"FAIL");
        n3ds_log(detail);
    }
}
/* Kevin Boggs: menu_keys_go_to_extra - New3DS integration. */
static void menu_keys_go_to_extra(long unit)
{
    if(!menu_go_to_extra || unit==NONE || !global_scenario ||
       strcmp(main_get_map_name(),"levels\\c20\\c20"))return;
    menu_go_to_extra=0;
    static const float offsets[]={-1.5f,-1.f,-0.5f,0.f,0.5f,1.f,1.5f};
    static const float heights[]={0.15f,0.5f,1.f,1.5f,2.5f};
    real_point3d origin={24.04f,-37.17f,-4.6f};
    int found=0;
    for(short bsp=0;bsp<global_scenario->structure_bsp_references.count;++bsp) {
        if(bsp!=global_structure_bsp_index && !scenario_switch_structure_bsp(bsp))continue;
        int count=0;
        real_point3d first={0};
        short first_cluster=NONE;
        for(unsigned int z=0;z<sizeof(heights)/sizeof(heights[0]);++z)
            for(unsigned int x=0;x<sizeof(offsets)/sizeof(offsets[0]);++x)
                for(unsigned int y=0;y<sizeof(offsets)/sizeof(offsets[0]);++y) {
                    real_point3d point={origin.x+offsets[x],origin.y+offsets[y],origin.z+heights[z]};
                    struct location location;
                    scenario_location_from_point(&location,&point);
                    if(location.cluster_index==NONE)continue;
                    if(!count) {first=point;first_cluster=location.cluster_index;}
                    ++count;
                }
        char message[100];
        snprintf(message,sizeof(message),"MENU KEYS: c20 nearby BSP=%d valid=%d first=%.2f,%.2f,%.2f cluster=%d",
            bsp,count,first.x,first.y,first.z,first_cluster);
        n3ds_log(message);
        if(count) {
            object_set_position(unit,&first,NULL,NULL);
            found=1;
            break;
        }
    }
    if(!found)n3ds_log("MENU KEYS: c20 extra location missing in all BSPs");
}
/* Kevin Boggs: menu_keys_go_to_b30_extra - New3DS integration. */
static void menu_keys_go_to_b30_extra(long unit)
{
    if(!menu_go_to_b30_extra || unit==NONE || !global_scenario ||
       strcmp(main_get_map_name(),"levels\\b30\\b30"))return;
    menu_go_to_b30_extra=0;
    static const real_point3d anchors[]={
        {-21.11f,42.75f,2.54f},{53.35f,63.28f,0.99f}
    };
    static const float heights[]={0.15f,0.5f,1.f,2.f};
    int found=0;
    for(short bsp=0;bsp<global_scenario->structure_bsp_references.count;++bsp) {
        if(bsp!=global_structure_bsp_index && !scenario_switch_structure_bsp(bsp))continue;
        for(unsigned int anchor=0;anchor<sizeof(anchors)/sizeof(anchors[0]);++anchor) {
            int count=0;
            float best_score=1000000.f;
            real_point3d best={0};
            short best_cluster=NONE;
            for(int x=-6;x<=6;++x) for(int y=-6;y<=6;++y) {
                float distance2=(float)(x*x+y*y);
                if(distance2<16.f || distance2>49.f)continue;
                for(unsigned int z=0;z<sizeof(heights)/sizeof(heights[0]);++z) {
                    real_point3d point={anchors[anchor].x+x,anchors[anchor].y+y,
                        anchors[anchor].z+heights[z]};
                    struct location location;
                    scenario_location_from_point(&location,&point);
                    if(location.cluster_index==NONE)continue;
                    ++count;
                    float dx=(float)x-5.f,dy=(float)y;
                    float score=dx*dx+dy*dy+heights[z]*0.05f;
                    if(score<best_score) {best_score=score;best=point;best_cluster=location.cluster_index;}
                }
            }
            char message[135];
            snprintf(message,sizeof(message),
                "MENU KEYS: b30 nearby BSP=%d anchor=%u valid=%d best=%.2f,%.2f,%.2f cluster=%d",
                bsp,anchor,count,best.x,best.y,best.z,best_cluster);
            n3ds_log(message);
            if(count && !found) {
                real_point3d player_position={best.x,best.y-4.f,best.z+0.5f};
                struct location player_location;
                scenario_location_from_point(&player_location,&player_position);
                if(player_location.cluster_index==NONE)player_position=best;
                real_vector3d forward={0.f,1.f,0.f};
                object_set_position(unit,&player_position,&forward,NULL);
                found=1;
            }
        }
        if(found)break;
    }
    if(!found)n3ds_log("MENU KEYS: b30 Rocket Warthog location missing in all BSPs");
}
/* Kevin Boggs: menu_keys_survey_extras - New3DS integration. */
static void menu_keys_survey_extras(long unit)
{
    if(!menu_survey_extras || unit==NONE || !global_scenario)return;
    menu_survey_extras=0;
    static const struct {
        const char *map,*item;
        real_point3d original,desired;
    } targets[]={
        {"c40","fuel rod",{332.99f,-656.33f,80.31f},{333.85f,-656.33f,80.45f}},
        {"d20","flamethrower",{53.32f,-34.40f,-25.98f},{54.15f,-34.40f,-25.80f}},
        {"d20","fuel rod",{30.14f,-49.65f,-23.01f},{31.15f,-49.65f,-22.86f}}
    };
    static const float offsets[]={-2.f,-1.5f,-1.f,-0.5f,0.f,0.5f,1.f,1.5f,2.f};
    static const float heights[]={0.15f,0.5f,1.f};
    const char *full=main_get_map_name();
    const char *base=full?strrchr(full,'\\'):NULL;
    base=base?base+1:full;
    if(!base)return;
    if(!strcmp(base,"d20") && global_structure_bsp_index!=1 &&
       !scenario_switch_structure_bsp(1)) {
        n3ds_log("MENU KEYS: d20 survey could not load BSP 1");
        return;
    }
    for(unsigned int t=0;t<sizeof(targets)/sizeof(targets[0]);++t) {
        if(strcmp(base,targets[t].map))continue;
        int count=0;float best_score=1000000.f;
        real_point3d best={0};short best_bsp=NONE,best_cluster=NONE;
        /* Inspect the active mission BSP. Switching repeatedly while the
         * mission runs can churn streamed scenery and stall the fixture. */
        for(short bsp=global_structure_bsp_index;
            bsp==global_structure_bsp_index;++bsp) {
            for(unsigned int x=0;x<sizeof(offsets)/sizeof(offsets[0]);++x)
                for(unsigned int y=0;y<sizeof(offsets)/sizeof(offsets[0]);++y) {
                    float gap=offsets[x]*offsets[x]+offsets[y]*offsets[y];
                    if(gap<0.7f*0.7f || gap>2.5f*2.5f)continue;
                    for(unsigned int z=0;z<sizeof(heights)/sizeof(heights[0]);++z) {
                        real_point3d point={targets[t].original.x+offsets[x],
                            targets[t].original.y+offsets[y],
                            targets[t].original.z+heights[z]};
                        struct location location;
                        scenario_location_from_point(&location,&point);
                        if(location.cluster_index==NONE)continue;
                        ++count;
                        float dx=point.x-targets[t].desired.x;
                        float dy=point.y-targets[t].desired.y;
                        float dz=point.z-targets[t].desired.z;
                        float score=dx*dx+dy*dy+dz*dz*0.1f;
                        if(score<best_score) {
                            best_score=score;best=point;
                            best_bsp=bsp;best_cluster=location.cluster_index;
                        }
                    }
                }
            if(count)break;
        }
        char message[175];
        snprintf(message,sizeof(message),
            "MENU KEYS: %s %s valid=%d best=%.2f,%.2f,%.2f BSP=%d cluster=%d",
            base,targets[t].item,count,best.x,best.y,best.z,best_bsp,best_cluster);
        n3ds_log(message);
    }
    if(!strcmp(base,"d20") && global_structure_bsp_index==1) {
        real_point3d near_both={54.32f,-34.90f,-25.83f};
        object_set_position(unit,&near_both,NULL,NULL);
    }
}
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MATRIX
/* Test-only runtime selection keeps map/mode sweeps on one identical binary.
 * A bounded two-token file lives on the isolated emulator SD, not in Git. */
/* Kevin Boggs: pc_matrix_choice - New3DS integration. */
static int pc_matrix_choice(char map[96],const char **mode)
{
    static const char *const modes[]={"slayer","ctf","oddball","king","race"};
    char line[80],map_name[32],mode_name[16],extra;
    FILE *file=fopen("sdmc:/halo-source/pc-matrix.txt","rb");
    if(!file) return 1;
    int valid=fgets(line,sizeof(line),file)!=NULL && fgetc(file)==EOF;
    fclose(file);
    if(!valid || sscanf(line,"%31s %15s %c",map_name,mode_name,&extra)!=2) return 1;
    int found=0;
    for(unsigned i=0;i<sizeof(n3ds_pc_diagnostic_maps)/sizeof(n3ds_pc_diagnostic_maps[0]);++i)
        if(!strcmp(map_name,n3ds_pc_diagnostic_maps[i])) {found=1;break;}
    if(!found) return 1;
    found=0;
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i)
        if(!strcmp(mode_name,modes[i])) {*mode=modes[i];found=1;break;}
    if(!found) return 1;
    int length=snprintf(map,96,"levels\\test\\%s\\%s",map_name,map_name);
    return length<0 || length>=96;
}
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
/* These are the owner supplied PC Blood Gulch tags. The multiplayer Fuel Rod
 * Gun is named plasma_cannon in the cache; no substitute projectile is used. */
static const struct { const char *label,*tag; } arsenal_weapons[] = {
    {"assault-rifle","weapons\\assault rifle\\assault rifle"},
    {"pistol","weapons\\pistol\\pistol"},
    {"shotgun","weapons\\shotgun\\shotgun"},
    {"plasma-rifle","weapons\\plasma rifle\\plasma rifle"},
    {"plasma-pistol","weapons\\plasma pistol\\plasma pistol"},
    {"needler","weapons\\needler\\mp_needler"},
    {"sniper-rifle","weapons\\sniper rifle\\sniper rifle"},
    {"flamethrower","weapons\\flamethrower\\flamethrower"},
    {"fuel-rod-gun","weapons\\plasma_cannon\\plasma_cannon"},
    {"rocket-launcher","weapons\\rocket launcher\\rocket launcher"}
};
enum { ARSENAL_FIRST_FRAME=240, ARSENAL_STAGE_FRAMES=105, ARSENAL_COUNT=sizeof(arsenal_weapons)/sizeof(arsenal_weapons[0]),
#ifdef HALO_N3DS_EXPERIMENTAL_PC_VEHICLE_ONLY
       ARSENAL_VEHICLE_FRAME=ARSENAL_FIRST_FRAME
#else
       ARSENAL_VEHICLE_FRAME=ARSENAL_FIRST_FRAME+ARSENAL_STAGE_FRAMES*ARSENAL_COUNT
#endif
};
static long arsenal_weapon=NONE,arsenal_vehicle=NONE,arsenal_initial_fire=NONE;
static unsigned int arsenal_fire_passes,arsenal_failures;
/* Kevin Boggs: arsenal_physical_fire_key - New3DS integration. */
static unsigned int arsenal_physical_fire_key(int original_model)
{
    if(original_model) return NATIVE_KEY_L;
    struct native_control_settings settings;
    n3ds_controls_initialize();
    n3ds_controls_get(&settings);
    return settings.swap_shoulders?NATIVE_KEY_R:NATIVE_KEY_ZR;
}
/* Kevin Boggs: arsenal_physical_grenade_key - New3DS integration. */
static unsigned int arsenal_physical_grenade_key(int original_model)
{
    if(original_model)return NATIVE_KEY_R;
    struct native_control_settings settings;
    n3ds_controls_initialize();n3ds_controls_get(&settings);
    return settings.swap_shoulders?NATIVE_KEY_L:NATIVE_KEY_ZL;
}

/* Kevin Boggs: arsenal_copy_front - New3DS integration. */
static int arsenal_copy_front(const char *label)
{
    extern FILE *capture_fopen(const char *,const char *) __asm__("fopen");
    char path[96];byte buffer[4096];size_t bytes,total=0;
    if(snprintf(path,sizeof(path),"sdmc:/halo-arsenal-%s.rgb",label)<0) return 1;
    if(n3ds_gpu_screen_test()) return 1;
    FILE *source=capture_fopen("sdmc:/halo-native-front.rgb","rb");
    FILE *capture=capture_fopen(path,"wb");
    if(!source || !capture) {if(source) fclose(source);if(capture) fclose(capture);return 1;}
    while((bytes=fread(buffer,1,sizeof(buffer),source))>0) {if(fwrite(buffer,1,bytes,capture)!=bytes) break;total+=bytes;}
    int failed=ferror(source) || bytes!=0 || total!=400*240*3;
    failed|=fclose(source)!=0;failed|=fclose(capture)!=0;
    return failed;
}
/* Kevin Boggs: arsenal_copy_bottom - New3DS integration. */
static int arsenal_copy_bottom(const char *label)
{
    extern FILE *capture_fopen(const char *,const char *) __asm__("fopen");
    char path[96];byte buffer[4096];size_t bytes,total=0;
    if(snprintf(path,sizeof(path),"sdmc:/halo-arsenal-bottom-%s.rgb",label)<0) return 1;
    FILE *source=capture_fopen("sdmc:/halo-native-bottom-front.rgb","rb");
    FILE *capture=capture_fopen(path,"wb");
    if(!source || !capture){if(source)fclose(source);if(capture)fclose(capture);return 1;}
    while((bytes=fread(buffer,1,sizeof(buffer),source))>0){if(fwrite(buffer,1,bytes,capture)!=bytes)break;total+=bytes;}
    int failed=ferror(source) || bytes!=0 || total!=320*240*3;
    failed|=fclose(source)!=0;failed|=fclose(capture)!=0;
    return failed;
}

/* Kevin Boggs: arsenal_prepare_weapon - New3DS integration. */
static int arsenal_prepare_weapon(unsigned int stage,long unit)
{
    char message[256];
    long definition=tag_loaded('weap',arsenal_weapons[stage].tag);
    if(definition==NONE) return 1;
    struct object_placement_data placement;
    object_placement_data_new(&placement,definition,NONE);
    placement.position=unit_get(unit)->object.position;
    arsenal_weapon=object_new(&placement);
    if(arsenal_weapon==NONE || !unit_add_weapon_to_inventory(unit,arsenal_weapon,_unit_add_weapon_replace)) return 1;
    player_control_set_desired_weapon(unit,unit_get(unit)->unit.desired_weapon_index);
    arsenal_initial_fire=weapon_get(arsenal_weapon)->weapon.game_time_last_fired;
    snprintf(message,sizeof(message),"ARSENAL START: %s tag=%s object=%08lx initial_fired=%ld",arsenal_weapons[stage].label,
        arsenal_weapons[stage].tag,arsenal_weapon,arsenal_initial_fire);
    n3ds_log(message);
    return 0;
}

/* Kevin Boggs: arsenal_prepare_vehicle - New3DS integration. */
static int arsenal_prepare_vehicle(long unit)
{
    char message[192];
    long definition=tag_loaded('vehi',"vehicles\\rwarthog\\rwarthog");
    if(definition==NONE) {n3ds_log("ARSENAL ROCKET WARTHOG FAIL: vehicle tag not loaded");return 1;}
    struct game_globals *globals=scenario_get_game_globals();
    struct game_globals_multiplayer_information *multiplayer=TAG_BLOCK_GET_ELEMENT(&globals->multiplayer_information,0,struct game_globals_multiplayer_information);
    snprintf(message,sizeof(message),"ARSENAL ROCKET WARTHOG: globals_vehicle_count=%ld vehicle_set=%d remapped=%08lx",
        multiplayer->vehicles.count,game_engine_get_variant()->universal_variant.vehicle_set,game_engine_remap_vehicle(definition));n3ds_log(message);
    for(short i=0;i<multiplayer->vehicles.count && i<16;++i) {
        struct game_globals_vehicle *entry=TAG_BLOCK_GET_ELEMENT(&multiplayer->vehicles,i,struct game_globals_vehicle);
        snprintf(message,sizeof(message),"ARSENAL ROCKET WARTHOG: globals_vehicle[%d]=%08lx %s",i,entry->vehicle.index,
            entry->vehicle.index==NONE?"-":tag_get_name(entry->vehicle.index));n3ds_log(message);
    }
    struct object_placement_data placement;
    object_placement_data_new(&placement,definition,NONE);
    placement.position=unit_get(unit)->object.position;
    placement.position.x+=5.f;
    arsenal_vehicle=object_new(&placement);
    if(arsenal_vehicle==NONE) {n3ds_log("ARSENAL ROCKET WARTHOG FAIL: original object_new failed");return 1;}
    struct unit_definition *vehicle_definition=unit_definition_get(definition);
    if(!vehicle_definition) {n3ds_log("ARSENAL ROCKET WARTHOG FAIL: unit definition unavailable");return 1;}
    snprintf(message,sizeof(message),"ARSENAL ROCKET WARTHOG: tag=%08lx vehicle=%08lx seats=%ld",definition,arsenal_vehicle,vehicle_definition->unit.seats.count);n3ds_log(message);
    short gunner=NONE;
    for(short i=0;i<vehicle_definition->unit.seats.count;++i) {
        struct unit_seat *seat=TAG_BLOCK_GET_ELEMENT(&vehicle_definition->unit.seats,i,struct unit_seat);
        snprintf(message,sizeof(message),"ARSENAL ROCKET WARTHOG: seat=%d label=%s flags=%08lx",i,seat->label,seat->flags);n3ds_log(message);
        if(TEST_FLAG(seat->flags,_unit_seat_gunner_bit)) {gunner=i;break;}
    }
    if(gunner==NONE) {n3ds_log("ARSENAL ROCKET WARTHOG FAIL: no gunner seat");return 1;}
    if(!unit_enter_seat(unit,arsenal_vehicle,gunner)) {n3ds_log("ARSENAL ROCKET WARTHOG FAIL: original unit_enter_seat rejected gunner");return 1;}
    snprintf(message,sizeof(message),"ARSENAL ROCKET WARTHOG: vehicle=%08lx gunner_seat=%d player=%08lx",arsenal_vehicle,gunner,unit);
    n3ds_log(message);
    return 0;
}

/* Kevin Boggs: arsenal_after_render - New3DS integration. */
static void arsenal_after_render(long frame,long unit)
{
    char message[256];
    if(frame>=ARSENAL_FIRST_FRAME && frame<ARSENAL_VEHICLE_FRAME) {
        unsigned int stage=(unsigned int)(frame-ARSENAL_FIRST_FRAME)/ARSENAL_STAGE_FRAMES;
        unsigned int offset=(unsigned int)(frame-ARSENAL_FIRST_FRAME)%ARSENAL_STAGE_FRAMES;
        if((stage==0 && (offset==30 || offset==38 ||
                         (offset>=46 && offset<=48) || (offset>=54 && offset<=56) ||
                         (offset>=64 && offset<=66) ||
                         (offset>=31 && offset<=39 && (offset&1)))) ||
           (stage==2 && offset>=31 && offset<=39 && (offset&1))) {
            char label[40];
            snprintf(label,sizeof(label),"impact-%s-%02u",stage==0?"ar":"shotgun",offset);
            int failed=arsenal_copy_front(label);
            snprintf(message,sizeof(message),"ARSENAL IMPACT CAPTURE: %s %s",label,failed?"FAILED":"saved");n3ds_log(message);
            if(failed)++arsenal_failures;
        }
        if((stage==0 || stage==2) && (offset==35 || offset==45)) {
            unsigned int count=0,shown=0;
            for(long p=data_next_index(particle_data,NONE);p!=NONE;p=data_next_index(particle_data,p)) {
                struct particle_datum *particle=particle_get(p);++count;
                const char *name=tag_get_name(particle->definition_index);
                if(name && (strstr(name,"impact") || strstr(name,"spark") || strstr(name,"bullet")) && shown++<16) {
                    snprintf(message,sizeof(message),"ARSENAL IMPACT PARTICLE: stage=%u offset=%u tag=%s age=%.3f",
                        stage,offset,name,particle->age);n3ds_log(message);
                }
            }
            snprintf(message,sizeof(message),"ARSENAL IMPACT PARTICLES: stage=%u offset=%u total=%u matched=%u",
                stage,offset,count,shown);n3ds_log(message);
        }
        if((stage==0 || stage==6) && (offset==35 || offset==60)) {
            unsigned int count=0;
            for(long c=data_next_index(contrail_data,NONE);c!=NONE;c=data_next_index(contrail_data,c)) {
                struct contrail_datum *trail=datum_get(contrail_data,c);
                if(count++>=16)break;
                snprintf(message,sizeof(message),"ARSENAL TRAIL: stage=%u offset=%u name=%.105s points=%d,%d,%d,%d object=%08lx",
                    stage,offset,tag_get_name(trail->definition_index),
                    trail->contrail_point_counts[0],trail->contrail_point_counts[1],
                    trail->contrail_point_counts[2],trail->contrail_point_counts[3],trail->object_index);
                n3ds_log(message);
            }
            snprintf(message,sizeof(message),"ARSENAL TRAIL TOTAL: stage=%u offset=%u count=%u",stage,offset,count);n3ds_log(message);
        }
        if(stage==0 && offset>=30 && offset<=45) {
            unsigned int trails=0,points=0;
            for(long c=data_next_index(contrail_data,NONE);c!=NONE;c=data_next_index(contrail_data,c)) {
                const struct contrail_datum *trail=datum_get(contrail_data,c);
                if(!strcmp(tag_get_name(trail->definition_index),"weapons\\assault rifle\\bullet")) {
                    ++trails;points+=trail->contrail_point_counts[0];
                }
            }
            snprintf(message,sizeof(message),"ARSENAL AR TRACER: offset=%u trails=%u points=%u",offset,trails,points);
            n3ds_log(message);
        }
        if(stage==7 && (offset==35 || offset==60)) {
            unsigned int ordinary=0,systems=0;long types[16];unsigned int amounts[16],used=0;
            for(long p=data_next_index(particle_data,NONE);p!=NONE;p=data_next_index(particle_data,p)) {
                struct particle_datum *particle=particle_get(p);++ordinary;
                unsigned int i=0;while(i<used && types[i]!=particle->definition_index)++i;
                if(i==used && used<16){types[used]=particle->definition_index;amounts[used++]=0;}
                if(i<used)++amounts[i];
            }
            for(long p=data_next_index(particle_systems,NONE);p!=NONE;p=data_next_index(particle_systems,p)) {
                struct particle_system_datum *system=particle_system_get(p);++systems;
                snprintf(message,sizeof(message),"ARSENAL FLAME SYSTEM: tag=%08lx name=%s object=%08lx cluster=%d count=%d,%d,%d,%d",
                    system->definition_index,tag_get_name(system->definition_index),system->object_index,
                    system->location.cluster_index,system->types[0].particle_count,
                    system->types[1].particle_count,system->types[2].particle_count,
                    system->types[3].particle_count);n3ds_log(message);
            }
            struct object_iterator projectiles;
            object_iterator_new(&projectiles,_object_mask_projectile,0);
            unsigned int owner_samples=0;
            while(object_iterator_next(&projectiles)) {
                struct object_datum *object=object_get(projectiles.index);
                const struct object_definition *definition=object_definition_get(object->definition_index);
                snprintf(message,sizeof(message),"ARSENAL FLAME PROJECTILE: object=%08lx tag=%08lx name=%s model=%08lx creation=%08lx attachments=%ld pos=%.2f,%.2f,%.2f vel=%.2f,%.2f,%.2f",
                    projectiles.index,object->definition_index,tag_get_name(object->definition_index),
                    definition->object.model.index,definition->object.creation_effect.index,definition->object.attachments.count,
                    object->object.position.x,object->object.position.y,object->object.position.z,
                    object->object.translational_velocity.i,object->object.translational_velocity.j,
                    object->object.translational_velocity.k);n3ds_log(message);
                if(!strcmp(tag_get_name(object->definition_index),"weapons\\flamethrower\\flame") && owner_samples++<4) {
                    long parent=object->object.parent_object_index;
                    const struct object_datum *owner=parent==NONE?NULL:object_try_and_get(parent);
                    real_vector3d relative;
                    vector_from_points3d(&render.camera.position,&object->object.position,&relative);
                    snprintf(message,sizeof(message),"ARSENAL FLAME OWNER: proj=%08lx parent=%08lx parent_parent=%08lx local_unit=%08lx depth=%.2f camera=%.2f,%.2f,%.2f",
                        projectiles.index,parent,owner?owner->object.parent_object_index:NONE,unit,
                        dot_product3d(&relative,&render.camera.forward),render.camera.position.x,
                        render.camera.position.y,render.camera.position.z);
                    n3ds_log(message);
                }
            }
            snprintf(message,sizeof(message),"ARSENAL FLAME FX: offset=%u particles=%u systems=%u types=%u",offset,ordinary,systems,used);n3ds_log(message);
            for(unsigned i=0;i<used;++i) {snprintf(message,sizeof(message),"ARSENAL FLAME TYPE: count=%u tag=%08lx name=%s",amounts[i],types[i],tag_get_name(types[i]));n3ds_log(message);}
            if(offset==35){int failed=arsenal_copy_front("flame-early");
                snprintf(message,sizeof(message),"ARSENAL CAPTURE: flame-early %s",failed?"FAILED":"saved");n3ds_log(message);
                if(failed)++arsenal_failures;}
        }
        if(stage==6 && offset>=30 && offset<=45) {
            char label[32];snprintf(label,sizeof(label),"sniper-shot-%02u",offset);
            int failed=arsenal_copy_front(label);
            snprintf(message,sizeof(message),"ARSENAL FX CAPTURE: %s %s",label,failed?"FAILED":"saved");n3ds_log(message);
            if(failed) ++arsenal_failures;
        }
        if(stage==2 && offset==20) {
            int failed=arsenal_copy_front("grenade-char");
            snprintf(message,sizeof(message),"ARSENAL CAPTURE: grenade-char %s",failed?"FAILED":"saved");n3ds_log(message);
            if(failed)++arsenal_failures;
        }
        if(stage==1 && offset==20) {
            int failed=arsenal_copy_front("blood-decal-diagnostic");
            snprintf(message,sizeof(message),"ARSENAL CAPTURE: blood-decal-diagnostic %s",failed?"FAILED":"saved");n3ds_log(message);
            if(failed)++arsenal_failures;
        }
        if(stage==1 && offset==35) {
            int failed=arsenal_copy_front("grenade-natural");
            snprintf(message,sizeof(message),"ARSENAL CAPTURE: grenade-natural %s",failed?"FAILED":"saved");n3ds_log(message);
            if(failed)++arsenal_failures;
        }
        if(offset==61) {
            int failed=arsenal_copy_front(arsenal_weapons[stage].label);
            snprintf(message,sizeof(message),"ARSENAL CAPTURE: %s %s",arsenal_weapons[stage].label,failed?"FAILED":"saved");n3ds_log(message);
            if(failed) ++arsenal_failures;
            if(stage==0 && arsenal_copy_bottom("normal"))++arsenal_failures;
        }
        if(stage==0 && offset==83) {
            int failed=arsenal_copy_front("scoreboard");
            n3ds_log(failed?"ARSENAL CAPTURE: scoreboard FAILED":"ARSENAL CAPTURE: scoreboard saved");
            if(failed) ++arsenal_failures;
        }
        if((stage==0 && offset==94) || (stage==1 && offset==83)) {
            const char *label=stage==0?"stub-pressed":"score-pressed";
            int failed=arsenal_copy_front(label);
            failed|=arsenal_copy_bottom(label);
            snprintf(message,sizeof(message),"ARSENAL CAPTURE: %s %s",label,failed?"FAILED":"saved");n3ds_log(message);
            if(failed)++arsenal_failures;
        }
        if(offset==91) {
            struct weapon_datum *weapon=weapon_try_and_get(arsenal_weapon);
            long fired=weapon?weapon->weapon.game_time_last_fired:NONE;
            int pass=weapon && fired>arsenal_initial_fire;
            if(pass) ++arsenal_fire_passes;else ++arsenal_failures;
            snprintf(message,sizeof(message),"ARSENAL RESULT: %s fired=%ld initial=%ld rounds=%d %s",arsenal_weapons[stage].label,
                fired,arsenal_initial_fire,weapon?weapon->weapon.magazines[0].rounds_loaded:NONE,pass?"PASS":"FAIL");n3ds_log(message);
        }
    } else if(frame>=ARSENAL_VEHICLE_FRAME && frame<ARSENAL_VEHICLE_FRAME+110) {
        unsigned int offset=(unsigned int)(frame-ARSENAL_VEHICLE_FRAME);
        if(offset==61) {
            int failed=arsenal_copy_front("rocket-warthog");
            n3ds_log(failed?"ARSENAL CAPTURE: rocket-warthog FAILED":"ARSENAL CAPTURE: rocket-warthog saved");
            if(failed) ++arsenal_failures;
        }
        if(offset==91) {
            struct unit_datum *vehicle=unit_try_and_get(arsenal_vehicle);
            long weapon_index=NONE;
            if(vehicle) for(short i=0;i<MAXIMUM_WEAPONS_PER_UNIT;++i)
                if(vehicle->unit.weapon_object_indices[i]!=NONE) {weapon_index=vehicle->unit.weapon_object_indices[i];break;}
            struct weapon_datum *weapon=weapon_try_and_get(weapon_index);
            long fired=weapon?weapon->weapon.game_time_last_fired:NONE;
            int pass=weapon && fired>arsenal_initial_fire && unit_get(unit)->object.parent_object_index==arsenal_vehicle;
            if(pass) ++arsenal_fire_passes;else ++arsenal_failures;
            snprintf(message,sizeof(message),"ARSENAL RESULT: rocket-warthog vehicle=%08lx gun=%08lx fired=%ld initial=%ld seated=%d %s",
                arsenal_vehicle,weapon_index,fired,arsenal_initial_fire,unit_get(unit)->object.parent_object_index==arsenal_vehicle,pass?"PASS":"FAIL");n3ds_log(message);
            snprintf(message,sizeof(message),"ARSENAL SUMMARY: fired=%u/%u failures=%u",arsenal_fire_passes,
#ifdef HALO_N3DS_EXPERIMENTAL_PC_VEHICLE_ONLY
                1u,
#else
                (unsigned)(ARSENAL_COUNT+1),
#endif
                arsenal_failures);n3ds_log(message);
        }
    }
}
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MAP_NAME
/* Observe the original object placement and game rules without changing them. */
/* Kevin Boggs: pc_multiplayer_object_trace - New3DS integration. */
static void pc_multiplayer_object_trace(long frame)
{
    static const struct { unsigned long mask; const char *label; } groups[] = {
        {_object_mask_weapon,"weapon"}, {_object_mask_vehicle,"vehicle"},
        {_object_mask_biped,"biped"}
    };
    char message[256];
    for (unsigned int group=0;group<sizeof(groups)/sizeof(groups[0]);++group) {
        struct object_iterator objects;
        unsigned int count=0;
        const char *examples[3]={0};
        object_iterator_new(&objects,groups[group].mask,0);
        while (object_iterator_next(&objects)) {
            struct object_datum *object=object_get(objects.index);
            if(count<3) examples[count]=tag_get_name(object->definition_index);
            ++count;
        }
        snprintf(message,sizeof(message),
            "EXPERIMENTAL PC OBJECTS: frame=%ld %s=%u tags=%s | %s | %s",
            frame,groups[group].label,count,
            examples[0]?examples[0]:"-",examples[1]?examples[1]:"-",examples[2]?examples[2]:"-");
        n3ds_log(message);
    }
}
#endif
static int empty_sprite_tests(void)
{
    struct build_sprite_data data={0};struct shader_effect_definition shader={0};
    build_sprites_begin(&data,1,NONE,&shader,0);
    data.group_count=2;
    data.groups[0].vertex_buffer_index=rasterizer_dynamic_vertices_new(6,4);
    CHECK(data.groups[0].vertex_buffer_index!=NONE);
    data.groups[0].vertices=rasterizer_dynamic_vertices_lock(data.groups[0].vertex_buffer_index);
    CHECK(data.groups[0].vertices && !n3ds_dynamic_vertices_get(data.groups[0].vertex_buffer_index));
    data.groups[1].vertex_buffer_index=NONE; /* Texture/allocation unavailable. */
    data.centroid.x=1;data.centroid.y=2;data.centroid.z=3;
    build_sprites_end(&data);
    CHECK(n3ds_dynamic_vertices_get(data.groups[0].vertex_buffer_index));
    CHECK(!data.centroid.x && !data.centroid.y && !data.centroid.z);
    rasterizer_dynamic_geometry_end();rasterizer_dynamic_geometry_begin();
    CHECK(!n3ds_dynamic_geometry_bytes());
    n3ds_log("PASS: empty original sprite groups release allocated locks, preserve absent buffers and avoid invalid centroids");return 0;
}
void n3ds_frontend_trace(const char *stage)
{
    extern void n3ds_stall_mark(const char *,unsigned int,int);
    n3ds_stall_mark(stage,current_frame,game_time_get());
    /* Retain a periodic stage trace without synchronous SD writes at every
     * stage of every rendered frame. Errors and lifecycle logs are unchanged. */
    const struct n3ds_cache_view *trace_view=n3ds_engine_cache_view(0);
    /* Only the render boundaries need extra writes in this short window. */
    int cryo_trace=trace_view && !strcmp(trace_view->header.name,"a10") && game_time_get()>=280 && game_time_get()<=360 &&
        (!strcmp(stage,"frame-begin") || !strcmp(stage,"present") || !strcmp(stage,"render-complete"));
    if(current_frame>=110 && (current_frame%30==0 || cryo_trace)) {
        char text[160];snprintf(text,sizeof(text),"FRONTEND STAGE: frame=%ld stage=%s textures=%u bytes=%u",current_frame,stage,n3ds_engine_texture_count(),n3ds_engine_texture_bytes());n3ds_log(text);
    }
}
#ifdef HALO_N3DS_CHECKPOINT_TEST
#include "engine_checkpoint_test.inl"
#endif
#include "engine_frame_pacing.inl"
int halo_engine_boot(void)
{
    char message[256];long frame;struct native_input_sample prior={0};
    extern long long n3ds_engine_tick_frequency(void);
    long long loop_profile[5]={0};unsigned int loop_frames=0;
    n3ds_log("FRONTEND: original menu lifecycle startup");
    n3ds_log("STARTUP: errors initialize");errors_initialize();
    n3ds_log("STARTUP: math initialize");real_math_initialize();
    n3ds_log("STARTUP: native floating-point checks");CHECK(n3ds_engine_float_tests());
    CHECK(n3ds_animation_alignment_tests());
    CHECK(n3ds_generic_tests());
    CHECK(n3ds_effect_vertex_reuse_tests());
    n3ds_log("STARTUP: PASS generic combiner regressions including disabled Xbox texture stages");
    n3ds_log("STARTUP: PASS packed animation translations/scales at all four byte alignments, default/exact/interpolated frames");
    n3ds_log("STARTUP: game state initialize");game_state_initialize();
    n3ds_log("STARTUP: rasterizer initialize");CHECK(rasterizer_initialize());
    if(HALO_N3DS_MISSION_TICKS) {
        extern void n3ds_visibility_verify(int enabled);
        n3ds_visibility_verify(1);
        extern void rasterizer_text_cache_dispose(void);
        extern boolean rasterizer_text_cache_initialize(void);
        extern int halo_engine_dynamic_bitmap_tests(void);
        rasterizer_text_cache_dispose();
        CHECK(n3ds_dynamic_bitmaps_collect());
        CHECK(!halo_engine_dynamic_bitmap_tests());
        CHECK(rasterizer_text_cache_initialize());
    }
    n3ds_log("STARTUP: touch aiming checks");CHECK(!halo_engine_touch_aim_tests());
    CHECK(!halo_engine_model_controls_tests());
    n3ds_log("STARTUP: input initialize");CHECK(input_initialize());
    n3ds_log("STARTUP: console initialize");console_initialize();
    n3ds_log("STARTUP: sprite allocation checks");
    CHECK(!empty_sprite_tests());
    int original_model=n3ds_input_platform_original_model();
    CHECK(n3ds_engine_texture_apply_saved_profile());
    n3ds_log(original_model?
        "PERFORMANCE PROFILE: original; world 320x192 adaptive/RGB565, textures32, LOD gameplay1/cinematic1/weapon2/distant0, foliage1/4-1/6, base-lit materials, particles48/draw64, emitter cap12, sprite layers2, radar16; full-rate controls/HUD":
        "PERFORMANCE PROFILE: New; PC world textures64 standard/128+mip high, LOD gameplay3/closeup cinematics/distant1-2, cosmetic particles144/draw192, dense emitter cap32, large sprite layers8, radar32; full-rate controls/HUD");
    if(HALO_N3DS_MISSION_TICKS) {
        CHECK(!n3ds_gpu_plasma_lut_tests());
        CHECK(!n3ds_gpu_texture_plasma_tests());
        n3ds_log("STARTUP: sampled texture checks");
        CHECK(!n3ds_engine_sampled_texture_tests());
#ifdef HALO_N3DS_RENDERER_TESTS
        extern int n3ds_lightmap_filter_tests(void);
        CHECK(n3ds_lightmap_filter_tests());
#endif
        n3ds_log("STARTUP: GPU menu overlay checks");
        CHECK(!n3ds_gpu_menu_plasma_tests());
        n3ds_log("STARTUP: GPU model transform checks");
        CHECK(!n3ds_gpu_model_transform_tests());
        CHECK(!n3ds_gpu_effect_packed_tests());
        CHECK(!n3ds_gpu_chicago_transform_tests());
        CHECK(!n3ds_gpu_model_batch_tests());
        CHECK(!n3ds_gpu_model_material_tests());
        CHECK(!n3ds_gpu_cube_tests());
        CHECK(!n3ds_gpu_fragment_tests());
        CHECK(!n3ds_gpu_model_lighting_tests());
        CHECK(!n3ds_engine_cube_layout_tests());
        CHECK(!n3ds_gpu_model_reflection_tests());
        CHECK(!n3ds_gpu_model_detail_tests());
        CHECK(!n3ds_gpu_model_resident_tests());
        CHECK(!n3ds_gpu_environment_tests());
        CHECK(!n3ds_gpu_environment_bump_tests());
        CHECK(!n3ds_gpu_flashlight_tests());
    }
#ifdef HALO_N3DS_RENDERER_TESTS
    extern int n3ds_gpu_model_precision_tests(void),n3ds_gpu_world_scale_tests(void);
    CHECK(!n3ds_gpu_model_precision_tests());
    CHECK(!n3ds_gpu_world_scale_tests());
#endif
    n3ds_gpu_loading_pulse(0,1);
    if(HALO_N3DS_MISSION_TICKS) CHECK(!n3ds_gpu_loading_capture(0));
    if(HALO_N3DS_MISSION_TICKS) {
        extern int n3ds_gpu_model_storage_tests(void);
        extern int halo_engine_relocation_tests(void),n3ds_gpu_sensor_profile_tests(void);
        extern int n3ds_audio_codec_reference_tests(void);
        CHECK(n3ds_audio_codec_reference_tests());
        n3ds_log("PASS: ADPCM lookup matches scalar PCM for all indices, mono/stereo and clipping");
        CHECK(n3ds_model_decode_cache_tests());
        CHECK(n3ds_model_skin_cache_tests());
        CHECK(n3ds_model_gpu_cache_tests());
        n3ds_log("PASS: model cache regressions completed before loading maps");
        CHECK(!halo_engine_relocation_tests());
        CHECK(n3ds_gpu_sensor_profile_tests());
        n3ds_log("PASS: original16/New32 radar masks match independent samples, invalidation and reuse");
        CHECK(n3ds_gpu_model_storage_tests());
        extern int n3ds_model_gpu_upload_tests(void);
        CHECK(n3ds_model_gpu_upload_tests());
        #ifdef HALO_N3DS_RENDERER_TESTS
        CHECK(!n3ds_gpu_chicago_tests());
        #endif
        if(original_model) {
            n3ds_gpu_loading_pulse(.5f,1);
            CHECK(!n3ds_gpu_loading_capture(4));
        }
    }
    n3ds_log("STARTUP: sound initialize");sound_initialize();
    n3ds_log("STARTUP: sound cache open");sound_cache_open();
    n3ds_log("STARTUP: game initialize");
    if(n3ds_frontend_fixture_seed) n3ds_frontend_fixture_seed();
    game_initialize();
#ifdef HALO_N3DS_UNIFIED_UI
    {extern void n3ds_campaign_extras_allocate_state(void);n3ds_campaign_extras_allocate_state();}
#endif
    n3ds_log("FRONTEND: game initialized; loading original main menu");
    n3ds_log("STARTUP: main menu load");
    n3ds_main_frontend_initialize();
    CHECK(main_menu_is_active() && global_scenario_try_and_get());
    if(HALO_N3DS_MISSION_TICKS) CHECK(!n3ds_engine_bitmap_resource_tests());
    n3ds_gpu_loading_enable(0);
    if(HALO_N3DS_MISSION_TICKS) {
        collision_timestamp stamp;long long elapsed[2];
        /* Compare disabled collection against the original timestamp path;
         * preserve period bookkeeping while changing no physics calls. */
        for(int enabled=0;enabled<2;++enabled) {
            collision_log_enable(enabled);collision_log_begin_period(0);
            long long start=n3ds_engine_ticks();
            for(int i=0;i<10000;++i) {
                collision_log_start_time(&stamp);
                collision_log_usage(_collision_function_vector_structure);
                collision_log_end_time(_collision_function_vector_structure,stamp.QuadPart);
            }
            elapsed[enabled]=n3ds_engine_ticks()-start;
            CHECK(enabled?stamp.QuadPart>0:stamp.QuadPart==0);
            collision_log_end_period();
        }
        collision_log_enable(FALSE);
        double scale=1000.0/n3ds_engine_tick_frequency();
        snprintf(message,sizeof(message),"PASS: collision diagnostic collection remains opt-in; 10000 queries disabled_ms=%.3f enabled_ms=%.3f",elapsed[0]*scale,elapsed[1]*scale);n3ds_log(message);
    }
    CHECK(n3ds_hs_stack_tests());
    n3ds_log("STARTUP: PASS script stack alignment, nested frames, floating-point values and re-entry");
    n3ds_log("STARTUP: main menu ready; entering frame loop");
#if defined(HALO_N3DS_SMOOTH_NEAR_TEXTURE) && !defined(HALO_N3DS_TEXTURE_CLIP_DEATH)
    long prior_residency_unit=NONE;
#endif
#ifdef HALO_N3DS_TEXTURE_CLIP_DEATH
    real_point3d traversal_death_position={0};
    long traversal_dead_unit=NONE;
    int traversal_relocate_pending=0;
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
    CHECK(!menu_keys_load());
#endif
    unsigned int previous_postgame_buttons=0;
    for(frame=0;(!HALO_N3DS_MISSION_TICKS || frame<HALO_N3DS_MISSION_TICKS) && n3ds_engine_app_running();++frame) {
        struct native_input_sample sample;
        long long start=n3ds_engine_ticks();long local,player,unit=NONE;
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ALL_MAPS
        if(frame==120) {
            snprintf(message,sizeof(message),"EXPERIMENTAL PC MENU: %d expansion maps have matching UI assets and installed caches",n3ds_ui_pc_expansion_map_count());
            n3ds_log(message);
        }
#endif
#ifdef HALO_N3DS_PROBE_A10
        if(frame==90) {
            main_set_map_name("levels\\a10\\a10");
            n3ds_log("PROBE A10: requested original campaign map through main_set_map_name");
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_A10
        if(frame==90) {
            main_set_map_name("levels\\a10\\a10");
            n3ds_log("EXPERIMENTAL PC: requested PC retail a10 campaign map");
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MAP_NAME
        if(frame==90) {
            struct game_variant variant;
            static const char *modes[]={"slayer","ctf","oddball","king","race"};
            const char *name=HALO_N3DS_EXPERIMENTAL_PC_MAP_NAME;
            char map[96];
            CHECK((unsigned int)snprintf(map,sizeof(map),"levels\\test\\%s\\%s",name,name)<sizeof(map));
            CHECK(game_engine_get_variant_by_name(&variant,modes[HALO_N3DS_EXPERIMENTAL_PC_MODE])->game_engine_index!=0);
            CHECK(n3ds_main_queue_solo_multiplayer(map,&variant,0));
            snprintf(message,sizeof(message),"EXPERIMENTAL PC: queued PC retail %s solo %s",map,modes[HALO_N3DS_EXPERIMENTAL_PC_MODE]);
            n3ds_log(message);
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MATRIX
        if(frame==90) {
            struct game_variant variant;
            char map[96];const char *mode=NULL;
            CHECK(!pc_matrix_choice(map,&mode));
            CHECK(game_engine_get_variant_by_name(&variant,mode)->game_engine_index!=0);
            CHECK(n3ds_main_queue_solo_multiplayer(map,&variant,0));
            snprintf(message,sizeof(message),"EXPERIMENTAL PC MATRIX: queued %s %s with 2 bots",map,mode);n3ds_log(message);
        }
#endif
#if defined(HALO_N3DS_BOT_TEST_COUNT) && !defined(HALO_N3DS_EXPERIMENTAL_PC_MAP_NAME) && !defined(HALO_N3DS_EXPERIMENTAL_PC_MATRIX)
        if(frame==90) {
            struct game_variant variant;
            static const char *modes[]={"slayer","ctf","oddball","king","race"};
            CHECK(game_engine_get_variant_by_name(&variant,modes[HALO_N3DS_BOT_TEST_MODE])->game_engine_index!=0);
            CHECK(n3ds_main_queue_solo_multiplayer("levels\\test\\bloodgulch\\bloodgulch",&variant,0));
            snprintf(message,sizeof(message),"BOT PROBE: queued Blood Gulch %s with %d local bots",modes[HALO_N3DS_BOT_TEST_MODE],HALO_N3DS_BOT_TEST_COUNT);n3ds_log(message);
        }
#endif
        n3ds_frontend_trace("checkpoint-poll");n3ds_checkpoint_poll();
        current_frame=frame+1;n3ds_frontend_trace("services");
#ifdef HALO_N3DS_CHECKPOINT_TEST
        CHECK(!checkpoint_fixture_step(frame+1));
#endif
        #ifdef HALO_N3DS_MAP_CYCLE_TEST
        const int cycle_start=HALO_N3DS_MISSION_TICKS-300*(11-HALO_N3DS_MAP_CYCLE_TEST);
        if(frame>=cycle_start && (frame-cycle_start)%300==0) {
            unsigned int index=HALO_N3DS_MAP_CYCLE_TEST-1+(frame-cycle_start)/300;
            CHECK(index<10 && !main_menu_is_active());
            char path[64];snprintf(path,sizeof(path),"levels\\%s\\%s",native_cycle_maps[index],native_cycle_maps[index]);
            main_set_map_name(path);
            snprintf(message,sizeof(message),"MAP CYCLE FIXTURE: request %s",native_cycle_maps[index]);n3ds_log(message);
        }
        if(frame>=cycle_start && (frame-cycle_start)%300==10) {
            unsigned int index=HALO_N3DS_MAP_CYCLE_TEST-1+(frame-cycle_start)/300;
            char path[64];snprintf(path,sizeof(path),"levels\\%s\\%s",native_cycle_maps[index],native_cycle_maps[index]);
            CHECK(global_scenario_index!=NONE && !strcmp(tag_get_name(global_scenario_index),path));
            snprintf(message,sizeof(message),"PASS: map cycle loaded and rendered %s",native_cycle_maps[index]);n3ds_log(message);
        }
        #endif
        #ifdef HALO_N3DS_FATAL_EXIT_TEST
        if(frame+1==330) {
            display_assert("intentional fatal-exit regression with DSP active",__FILE__,__LINE__,TRUE);
            #if HALO_N3DS_FATAL_EXIT_TEST == 2
            halt_and_catch_fire();
            #else
            system_exit(-1);
            #endif
        }
        #endif
        n3ds_main_frontend_services();
        long long services_end=n3ds_engine_ticks();
        n3ds_frontend_trace("input");
        n3ds_input_platform_poll(&sample);
#ifdef HALO_N3DS_POSTGAME_RETURN_TEST
        if(frame+1==450) {
            CHECK(n3ds_main_is_solo_multiplayer());
            game_engine_end_game();
            n3ds_log("POSTGAME TEST: ended original multiplayer match");
        }
        if(frame+1==840) {
            CHECK(n3ds_main_is_solo_multiplayer() && !game_engine_allow_pause());
            sample.held|=NATIVE_KEY_TOUCH;sample.touch_x=160;sample.touch_y=36;
            n3ds_log("POSTGAME TEST: tapped bottom-screen Return to Lobby");
        }
        if(frame+1==900) {
            CHECK(main_menu_is_active() && !n3ds_main_is_solo_multiplayer());
#ifdef HALO_N3DS_PC_LOCAL_MENU
            CHECK(n3ds_pc_local_menu_active());
#endif
            n3ds_log("PASS: postgame touch returned from bot match to lobby");
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
        if(frame+1==28) n3ds_controls_open(1);
        if(frame+1==44) sample.held|=NATIVE_KEY_START;
#endif
#ifdef HALO_N3DS_TEXTURE_CLIP
        if(n3ds_main_is_solo_multiplayer() && game_time_get()>=120 && game_time_get()<360 &&
           player_input_enabled()) sample.circle_y=95;
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
        menu_keys_sample(frame+1,&sample);
        if(menu_b30_fire_start && frame+1>=menu_b30_fire_start &&
           frame+1<menu_b30_fire_start+70)sample.held|=NATIVE_KEY_ZR;
        if(menu_b30_action_frame && frame+1>=menu_b30_action_frame &&
           frame+1<menu_b30_action_frame+3)sample.held|=NATIVE_KEY_X;
#endif
#ifdef HALO_N3DS_SCENIC_TEXTURE
        /* Enter the base doorway once, then leave the camera still while
         * queued textures and the scene settle for a matched comparison. */
        if(n3ds_main_is_solo_multiplayer() && player_input_enabled() &&
           frame+1>=450 && frame+1<675) sample.circle_y=110;
#endif
#ifdef HALO_N3DS_TEXTURE_CLIP_DEATH
        if(n3ds_main_is_solo_multiplayer() && player_input_enabled()) {
            long clip_frame=frame+1;
            if((clip_frame>=450 && clip_frame<900) ||
               (clip_frame>=1330 && clip_frame<1500)) sample.circle_y=110;
            if((clip_frame>=660 && clip_frame<663) ||
               (clip_frame>=795 && clip_frame<798)) sample.held|=NATIVE_KEY_A;
            if(clip_frame>=920 && clip_frame<950) sample.cstick_x=24;
        }
#endif
#ifdef HALO_N3DS_AUDIO_TRACE
        if(n3ds_main_is_solo_multiplayer() && player_input_enabled()) {
            long trace_frame=frame+1;
            if(trace_frame>=450 && trace_frame<900) sample.circle_y=110;
            if((trace_frame>=660 && trace_frame<663) ||
               (trace_frame>=795 && trace_frame<798)) sample.held|=NATIVE_KEY_A;
            if(trace_frame>=920 && trace_frame<950) sample.cstick_x=24;
        }
#endif
        n3ds_input_gameplay_controls(game_in_progress() && !main_menu_is_active() &&
            !ui_widgets_active() && player_input_enabled() && !cinematic_in_progress());
#ifdef HALO_N3DS_EXPERIMENTAL_PC_FIRE
        if(frame+1>=260 && frame+1<290 && game_in_progress() &&
            !main_menu_is_active() && player_input_enabled()) {
            sample.held|=original_model?NATIVE_KEY_L:NATIVE_KEY_ZR;
            if(frame+1==260) n3ds_log("EXPERIMENTAL PC FIRE: holding original primary trigger for 30 frames");
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
        {
            long fixture_local=local_player_get_next(NONE);
            long fixture_player=fixture_local==NONE?NONE:local_player_get_player_index(fixture_local);
            unit=fixture_player==NONE?NONE:player_get(fixture_player)->unit_index;
        }
        if(unit!=NONE && frame+1>=ARSENAL_FIRST_FRAME && frame+1<ARSENAL_VEHICLE_FRAME) {
            unsigned int stage=(unsigned int)(frame+1-ARSENAL_FIRST_FRAME)/ARSENAL_STAGE_FRAMES;
            unsigned int offset=(unsigned int)(frame+1-ARSENAL_FIRST_FRAME)%ARSENAL_STAGE_FRAMES;
            static struct native_control_settings decal_test_saved;
            if(stage==1 && offset==90) {
                struct native_control_settings changed;
                n3ds_controls_get(&decal_test_saved);changed=decal_test_saved;changed.decals=0;
                n3ds_controls_set(&changed);
                if(n3ds_controls_decals()) {++arsenal_failures;n3ds_log("ARSENAL DECALS OFF: setting failed");}
                else n3ds_log("ARSENAL DECALS OFF: active");
            }
            if(stage==1 && offset==91) {
                long definition=tag_loaded('deca',"effects\\decals\\bullet holes\\grenade char");
                if(definition!=NONE && global_decal_data) {
                    short before=global_decal_data->actual_count;
                    real_point3d origin=render.camera.position;
                    real_vector3d velocity=render.camera.forward,right;
                    cross_product3d(&render.camera.forward,&render.camera.up,&right);
                    normalize3d(&right);
                    for(int axis=0;axis<3;++axis) {
                        origin.n[axis]-=right.n[axis]*.45f;
                        velocity.n[axis]*=12.f;
                    }
                    decal_new(definition,&origin,&velocity,.55f,FALSE,NONE,NULL);
                    if(global_decal_data->actual_count!=before) {
                        ++arsenal_failures;n3ds_log("ARSENAL DECALS OFF: creation was not suppressed");
                    } else n3ds_log("ARSENAL DECALS OFF: creation suppressed");
                }
            }
            if(stage==1 && offset==92) {
                int failed=arsenal_copy_front("decals-off");
                if(failed)++arsenal_failures;
                n3ds_log(failed?"ARSENAL CAPTURE: decals-off FAILED":"ARSENAL CAPTURE: decals-off saved");
            }
            if(stage==1 && offset==94) {
                n3ds_controls_set(&decal_test_saved);
                if(!n3ds_controls_decals()) {++arsenal_failures;n3ds_log("ARSENAL DECALS ON: setting failed");}
                else n3ds_log("ARSENAL DECALS ON: active");
            }
            if(stage==1 && offset==96) {
                int failed=arsenal_copy_front("decals-on");
                if(failed)++arsenal_failures;
                n3ds_log(failed?"ARSENAL CAPTURE: decals-on FAILED":"ARSENAL CAPTURE: decals-on saved");
            }
            if(offset==0) CHECK(!arsenal_prepare_weapon(stage,unit));
            if(offset>=30 && offset<69) sample.held|=arsenal_physical_fire_key(original_model);
            if(stage==0 && offset==70) {
                short total=unit_add_grenade_type_to_inventory(unit,0,2);
                snprintf(message,sizeof(message),"ARSENAL GRENADE: granted total=%d",total);n3ds_log(message);
                long blood=tag_loaded('deca',"effects\\decals\\blood splats\\blood splat");
                if(blood!=NONE) {
                    short before=global_decal_data?global_decal_data->actual_count:-1;
                    real_vector3d velocity=render.camera.forward;
                    real_vector3d right;
                    real_point3d origin=render.camera.position;
                    cross_product3d(&render.camera.forward,&render.camera.up,&right);
                    normalize3d(&right);
                    for(int axis=0;axis<3;++axis)origin.n[axis]-=right.n[axis]*.45f;
                    for(int axis=0;axis<3;++axis)velocity.n[axis]*=12.f;
                    decal_new(blood,&origin,&velocity,.6f,FALSE,NONE,NULL);
                    snprintf(message,sizeof(message),"ARSENAL BLOOD DIAGNOSTIC: decals %d->%d origin=%.2f,%.2f,%.2f direction=%.2f,%.2f,%.2f",
                        before,global_decal_data?global_decal_data->actual_count:-1,
                        origin.x,origin.y,origin.z,velocity.i,velocity.j,velocity.k);n3ds_log(message);
                }
            }
            if(stage==0 && offset>=72 && offset<75) sample.held|=arsenal_physical_grenade_key(original_model);
            if(stage==1 && offset==45) {
                long grenade=tag_loaded('deca',"effects\\decals\\bullet holes\\grenade char");
                if(grenade!=NONE) {
                    short before=global_decal_data?global_decal_data->actual_count:-1;
                    real_vector3d velocity=render.camera.forward;
                    real_vector3d right;
                    real_point3d origin=render.camera.position;
                    cross_product3d(&render.camera.forward,&render.camera.up,&right);
                    normalize3d(&right);
                    for(int axis=0;axis<3;++axis)origin.n[axis]-=right.n[axis]*.45f;
                    for(int axis=0;axis<3;++axis)velocity.n[axis]*=12.f;
                    decal_new(grenade,&origin,&velocity,.55f,FALSE,NONE,NULL);
                    snprintf(message,sizeof(message),"ARSENAL GRENADE DIAGNOSTIC: decals %d->%d origin=%.2f,%.2f,%.2f direction=%.2f,%.2f,%.2f",
                        before,global_decal_data?global_decal_data->actual_count:-1,
                        origin.x,origin.y,origin.z,velocity.i,velocity.j,velocity.k);n3ds_log(message);
                }
            }
            if(stage==0 && offset>=75 && offset<86) sample.held|=NATIVE_KEY_SELECT;
            if(stage==0 && offset>=87 && offset<99){sample.held|=NATIVE_KEY_TOUCH;sample.touch_x=40;sample.touch_y=65;}
            if(stage==1 && offset>=75 && offset<86){sample.held|=NATIVE_KEY_TOUCH;sample.touch_x=270;sample.touch_y=140;}
        }
        if(unit!=NONE && frame+1>=ARSENAL_VEHICLE_FRAME && frame+1<ARSENAL_VEHICLE_FRAME+110) {
            unsigned int offset=(unsigned int)(frame+1-ARSENAL_VEHICLE_FRAME);
            if(offset==0) CHECK(!arsenal_prepare_vehicle(unit));
            if(offset==25) {
                struct unit_datum *vehicle=unit_try_and_get(arsenal_vehicle);
                if(vehicle) for(short i=0;i<MAXIMUM_WEAPONS_PER_UNIT;++i) {
                    struct weapon_datum *gun=weapon_try_and_get(vehicle->unit.weapon_object_indices[i]);
                    if(gun) {arsenal_initial_fire=gun->weapon.game_time_last_fired;break;}
                }
            }
            if(offset>=30 && offset<69) sample.held|=arsenal_physical_fire_key(original_model);
        }
#endif
        if((sample.held&(NATIVE_KEY_START|NATIVE_KEY_SELECT))==(NATIVE_KEY_START|NATIVE_KEY_SELECT)) break;
        unsigned int postgame_buttons=sample.held;
        if(n3ds_main_is_solo_multiplayer() && !game_engine_allow_pause() &&
           (sample.held&NATIVE_KEY_B) && !(previous_postgame_buttons&NATIVE_KEY_B)) {
            n3ds_postgame_return_to_lobby();n3ds_log("POSTGAME: return to lobby requested by B");
            sample.held&=~NATIVE_KEY_B;
        }
        previous_postgame_buttons=postgame_buttons;
        n3ds_engine_input_sample(&sample);input_update();input_abstraction_update();
        if(frame%30==0 || memcmp(&sample,&prior,sizeof(sample))) {
            snprintf(message,sizeof(message),"FRONTEND INPUT: frame=%ld held=%08x circle=%d,%d look=%d,%d enabled=%d cinematic=%d skip=%d touch=%u,%u",frame+1,sample.held,sample.circle_x,sample.circle_y,sample.cstick_x,sample.cstick_y,player_input_enabled(),cinematic_in_progress(),cinematic_can_be_skipped(),sample.touch_x,sample.touch_y);n3ds_log(message);prior=sample;
        }
        event_manager_update();n3ds_frontend_trace("widgets");process_ui_widgets();bink_playback_update();
        n3ds_frontend_trace("simulation");
        long long simulation_start=n3ds_engine_ticks();
        if(game_in_progress()) {
#ifdef HALO_N3DS_AUDIO_TRACE
            static long long sim_sections[5];
            static unsigned int sim_samples;
            long long sim_stamp=simulation_start,sim_next;
#endif
            player_control_update(1.f/30.f);
#ifdef HALO_N3DS_AUDIO_TRACE
            sim_next=n3ds_engine_ticks();sim_sections[0]+=sim_next-sim_stamp;sim_stamp=sim_next;
#endif
            game_time_update(1.f/30.f);
#ifdef HALO_N3DS_AUDIO_TRACE
            sim_next=n3ds_engine_ticks();sim_sections[1]+=sim_next-sim_stamp;sim_stamp=sim_next;
#endif
            director_update(1.f/30.f);observer_update(1.f/30.f);
#ifdef HALO_N3DS_AUDIO_TRACE
            sim_next=n3ds_engine_ticks();sim_sections[2]+=sim_next-sim_stamp;sim_stamp=sim_next;
#endif
            n3ds_main_mission_save_step(FALSE);
#ifdef HALO_N3DS_AUDIO_TRACE
            sim_next=n3ds_engine_ticks();sim_sections[3]+=sim_next-sim_stamp;sim_stamp=sim_next;
            if(++sim_samples==30) {
                double factor=1000.0/(n3ds_engine_tick_frequency()*sim_samples);
                snprintf(message,sizeof(message),"SIM DETAIL: time=%ld controls_ms=%.3f game_ms=%.3f camera_ms=%.3f save_ms=%.3f",
                    game_time_get(),sim_sections[0]*factor,sim_sections[1]*factor,sim_sections[2]*factor,sim_sections[3]*factor);
                n3ds_log(message);memset(sim_sections,0,sizeof(sim_sections));sim_samples=0;
            }
#endif
        }
#ifdef HALO_N3DS_AUDIO_TRACE
        if(frame+1==1200 && n3ds_main_is_solo_multiplayer()) {
            struct data_iterator players;
            int participants=0,kills=0,deaths=0;
            data_iterator_new(&players,player_data);
            const struct player_datum *entry;
            while((entry=data_iterator_next(&players))!=NULL) {
                ++participants;deaths+=entry->statistics.deaths;
                for(unsigned int category=0;category<4;++category) kills+=entry->statistics.kills[category];
            }
            snprintf(message,sizeof(message),"BOT MATCH SCORE: players=%d kills=%d deaths=%d",participants,kills,deaths);
            n3ds_log(message);
        }
#endif
        local=local_player_get_next(NONE);
        if(local!=NONE) {player=local_player_get_player_index(local);if(player!=NONE) unit=player_get(player)->unit_index;}
#ifdef HALO_N3DS_SCENIC_TEXTURE
        if(frame+1==5410 && unit!=NONE) {
            real_point3d other_base={17.20f,-54.24f,-13.35f};
            real_vector3d toward_outside={0.f,1.f,0.f};
            object_set_position(unit,&other_base,&toward_outside,global_up3d);
            player_control_set_facing(local,&toward_outside);
            n3ds_log("SCENIC: moved local player to the opposite Timberland base exit");
        }
        if(frame+1==6010 && unit!=NONE) {
            real_point3d first_base={-15.76f,54.22f,-13.35f};
            real_vector3d toward_outside={0.f,-1.f,0.f};
            object_set_position(unit,&first_base,&toward_outside,global_up3d);
            player_control_set_facing(local,&toward_outside);
            n3ds_log("SCENIC: returned to the first Timberland base exit");
        }
#endif
#ifdef HALO_N3DS_TEXTURE_CLIP_DEATH
        if(unit!=NONE) SET_FLAG(object_get(unit)->object.damage_flags,_object_cannot_take_damage_bit,TRUE);
        if(unit!=NONE && (frame+1)%60==0) {
            const struct object_datum *clip_object=object_get(unit);
            snprintf(message,sizeof(message),"TEXTURE CLIP POSITION: frame=%ld x=%.2f y=%.2f z=%.2f",
                frame+1,clip_object->object.position.x,clip_object->object.position.y,clip_object->object.position.z);
            n3ds_log(message);
        }
#endif
#if defined(HALO_N3DS_SMOOTH_NEAR_TEXTURE) && !defined(HALO_N3DS_TEXTURE_CLIP_DEATH)
        if(unit!=NONE && unit!=prior_residency_unit) {
            if(prior_residency_unit!=NONE) n3ds_engine_texture_respawn_transition();
            prior_residency_unit=unit;
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
        if(menu_kill_player && unit!=NONE && n3ds_main_is_solo_multiplayer()) {
            snprintf(message,sizeof(message),"MENU KEYS: forced player death frame=%ld unit=%08lx",frame+1,unit);
            n3ds_log(message);
#ifdef HALO_N3DS_TEXTURE_CLIP_DEATH
            traversal_death_position=object_get(unit)->object.position;
            traversal_dead_unit=unit;
            traversal_relocate_pending=1;
#endif
            SET_FLAG(object_get(unit)->object.damage_flags,_object_cannot_take_damage_bit,FALSE);
            unit_kill(unit);menu_kill_player=0;
        }
#endif
#ifdef HALO_N3DS_TEXTURE_CLIP_DEATH
        if(traversal_relocate_pending && unit!=NONE && unit!=traversal_dead_unit) {
            short best=NONE,count=player_get_starting_location_count();
            float farthest=-1.f;short eligible=0;int preferred=0;
            for(short index=0;index<count;++index) {
                const struct player_starting_location *location=player_get_starting_location(index);
                if(!location || game_engine_get_starting_location_rating(player,location)<=0.f) continue;
                ++eligible;
                float dx=location->position.x-traversal_death_position.x;
                float dy=location->position.y-traversal_death_position.y;
                float dz=location->position.z-traversal_death_position.z;
                float distance2=dx*dx+dy*dy+dz*dz;
                int outdoor=location->position.x>30.f && location->position.y< -20.f;
                if((outdoor && !preferred) || (outdoor==preferred && distance2>farthest)) {
                    preferred=outdoor;farthest=distance2;best=index;
                }
            }
            if(best!=NONE) {
                const struct player_starting_location *location=player_get_starting_location(best);
                real_vector3d forward;
                forward.i=-location->position.x;
                forward.j=-location->position.y;
                forward.k=0.f;
                if(normalize3d(&forward)==0.f) vector3d_from_angle(&forward,location->facing);
                object_set_position(unit,&location->position,&forward,global_up3d);
                player_control_set_facing(local,&forward);
#if defined(HALO_N3DS_NEAR_WORLD_WIDTH) && HALO_N3DS_NEAR_WORLD_WIDTH==512
                n3ds_engine_texture_respawn_transition();
#endif
                snprintf(message,sizeof(message),"TEXTURE CLIP RELOCATION: frame=%ld start=%d eligible=%d distance=%.1f from=%.1f,%.1f,%.1f to=%.1f,%.1f,%.1f",
                    frame+1,best,eligible,sqrtf(farthest),traversal_death_position.x,traversal_death_position.y,traversal_death_position.z,
                    location->position.x,location->position.y,location->position.z);
                n3ds_log(message);
            } else n3ds_log("TEXTURE CLIP RELOCATION: no eligible distant spawn");
            traversal_relocate_pending=0;
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
        menu_keys_go_to_extra(unit);
        menu_keys_go_to_b30_extra(unit);
        menu_keys_survey_extras(unit);
#endif
#ifdef HALO_N3DS_UNIFIED_UI
        {extern void n3ds_campaign_extras_update(long);n3ds_campaign_extras_update(unit);}
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
        menu_keys_enter_b30_extra(frame+1,unit);
        menu_keys_try_b30_extra(frame+1,unit);
#endif
        if(unit!=NONE && n3ds_main_is_solo_multiplayer()) {
            CHECK(local_player_count()==1);
            #ifdef HALO_N3DS_GRENADE_INVENTORY_TEST
            if(frame+1==300) {
                snprintf(message,sizeof(message),"GRENADE INVENTORY FIXTURE: before=%d,%d; original inventory add, not an organic pickup",unit_get_grenade_count(unit,0),unit_get_grenade_count(unit,1));n3ds_log(message);
                unit_add_grenade_type_to_inventory(unit,0,1);unit_add_grenade_type_to_inventory(unit,1,1);
                CHECK(unit_get_grenade_count(unit,0)>0 && unit_get_grenade_count(unit,1)>0);
            }
            #endif
            #ifdef HALO_N3DS_CAMO_TEST
            if(frame+1==300) {
                unit_get(unit)->unit.flags|=1u<<_unit_active_camouflaged_bit;
                unit_get(unit)->unit.active_camouflage=1.f;
                n3ds_log("CAMO FIXTURE: activated original unit camouflage state");
            }
            #endif
            #ifdef HALO_N3DS_SNIPER_INVENTORY_TEST
            if(frame+1==300) {
                struct object_placement_data placement;
                long definition=tag_loaded('weap',"weapons\\sniper rifle\\sniper rifle");
                CHECK(definition!=NONE);
                object_placement_data_new(&placement,definition,NONE);
                placement.position=unit_get(unit)->object.position;
                long weapon=object_new(&placement);CHECK(weapon!=NONE);
                CHECK(unit_add_weapon_to_inventory(unit,weapon,_unit_add_weapon_replace));
                player_control_set_desired_weapon(unit,unit_get(unit)->unit.desired_weapon_index);
                n3ds_log("SNIPER FIXTURE: equipped original map-owned weapon through inventory; zoom still requires controller input");
            }
            if(frame+1>=330 && (frame+1)%30==0) {
                short slot=unit_get(unit)->unit.current_weapon_index;
                long weapon=slot==NONE?NONE:unit_get(unit)->unit.weapon_object_indices[slot];
                short zoom=player_control_get_zoom_level(local);
                snprintf(message,sizeof(message),"SNIPER FIXTURE: frame=%ld weapon=%08lx zoom=%d magnification=%.3f",frame+1,weapon,zoom,weapon==NONE?0.f:weapon_get_zoom_magnification(weapon,zoom));n3ds_log(message);
                if(frame+1==390) CHECK(weapon!=NONE && weapon_get(weapon)->definition_index==tag_loaded('weap',"weapons\\sniper rifle\\sniper rifle"));
                if(frame+1==450 || frame+1==780) CHECK(zoom==0);
                if(frame+1==510 || frame+1==840) CHECK(zoom==1);
                if(frame+1==660) CHECK(zoom==NONE);
            }
            #endif
        }
        if(unit!=NONE && !main_menu_is_active()) n3ds_mission_combat_trace(unit,game_time_get());
#ifdef HALO_N3DS_EXPERIMENTAL_PC_FIRE
        if (unit!=NONE && (frame+1==259 || frame+1==270 || frame+1==290 || frame+1==320)) {
            short slot=unit_get(unit)->unit.current_weapon_index;
            long weapon_index=slot==NONE?NONE:unit_get(unit)->unit.weapon_object_indices[slot];
            struct weapon_datum *weapon=weapon_try_and_get(weapon_index);
            snprintf(message,sizeof(message),
                "EXPERIMENTAL PC FIRE: frame=%ld weapon=%s last_fired=%ld rounds=%d",
                frame+1,weapon?tag_get_name(weapon->definition_index):"-",
                weapon?weapon->weapon.game_time_last_fired:NONE,
                weapon?weapon->weapon.magazines[0].rounds_loaded:NONE);
            n3ds_log(message);
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MAP_NAME
        if (unit!=NONE && frame+1>=300 && (frame+1)%150==0)
            pc_multiplayer_object_trace(frame+1);
#endif
        #ifdef HALO_N3DS_MEMORY_CHECKS
        if(frame+1==900) {
            extern void *n3ds_game_state_identity(unsigned long long *);
            unsigned long long generation;struct game_state_header *live=n3ds_game_state_identity(&generation),header;
            boolean corrupted=FALSE;
            CHECK(unit!=NONE && live && !main_menu_is_active());
            game_state_write_to_persistent_storage(live,&live->checksum,sizeof(*live),0x345000);
            CHECK(game_state_read_header_from_persistent_storage(&header,&header.checksum,sizeof(header),0x345000,&corrupted));
            CHECK(!corrupted && !header.checksum && !memcmp(&header,live,offsetof(struct game_state_header,checksum)));
            game_state_read_from_persistent_storage(live,0x345000);
            n3ds_log("PASS: allocation-free full Campaign snapshot validates and restores through original reader");
        }
        #endif
        #ifdef HALO_N3DS_MAW_CUTSCENE_TEST
        if(frame+1==1100) {
            CHECK(unit!=NONE && !main_menu_is_active() && !cinematic_in_progress());
            const char *name=HALO_N3DS_MAW_CUTSCENE_TEST==2?"cinematic_finale":"cinematic_bridge";
            short script=hs_find_script_by_name(name);CHECK(script!=NONE);
            const struct hs_script *definition=TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->hs_scripts,script,struct hs_script);
            /* Sleeping/void expressions also return NONE after starting. */
            hs_runtime_evaluate(definition->root_expression_index);
            snprintf(message,sizeof(message),"MAW CUTSCENE FIXTURE: invoked original %s script; no shader/geometry substitutions",name);n3ds_log(message);
        }
        if(frame+1==1200) CHECK(cinematic_in_progress());
        #endif
        if(HALO_N3DS_MISSION_TICKS && unit!=NONE && !main_menu_is_active() && frame%30==0) {
            const real_euler_angles2d *angles=player_control_get_facing_angles(local);
            snprintf(message,sizeof(message),"LOOK PERMISSION: frame=%ld input=%d camera=%d inhibited=%d facing=%d cinematic=%d yaw=%.5f pitch=%.5f",frame+1,player_input_enabled(),player_control_camera_control_is_active(),director_inhibited_input(local),director_inhibited_facing(local),cinematic_in_progress(),angles->yaw,angles->pitch);
            n3ds_log(message);
        }
        #if defined(HALO_N3DS_SNIPER_INVENTORY_TEST) && defined(HALO_N3DS_GRENADE_INVENTORY_TEST)
        /* Combined inventory fixtures exercise each model's real action path. */
        if(frame+1==630) {
            CHECK(unit!=NONE && TEST_FLAG(unit_get(unit)->unit.control_flags,_unit_control_crouch_modifier_bit));
            CHECK(unit_get(unit)->unit.throttle.i==0 && unit_get(unit)->unit.throttle.j==0);
            n3ds_log("CONTROLS FIXTURE: crouch held without directional movement");
        }
        if(frame+1==660) {
            CHECK(unit!=NONE && !TEST_FLAG(unit_get(unit)->unit.control_flags,_unit_control_crouch_modifier_bit));
            CHECK(unit_get_grenade_count(unit,0)==0 && unit_get_grenade_count(unit,1)==1);
            n3ds_log("CONTROLS FIXTURE: crouch released; one frag grenade thrown through original gameplay");
        }
        #endif
        long long simulation_end=n3ds_engine_ticks();
        #ifdef HALO_N3DS_HIDDEN_HUD_AIM_TEST
        if(frame+1>=831 && frame+1<=930) {
            extern boolean scripted_show_hud(boolean);
            CHECK(unit!=NONE && player_input_enabled() && !cinematic_in_progress());
            scripted_show_hud(FALSE);
            if(frame+1==831) n3ds_log("HIDDEN HUD AIM FIXTURE: campaign HUD disabled; live touch input must still turn the player");
        }
        if(frame+1==931) {
            extern boolean scripted_show_hud(boolean);
            scripted_show_hud(TRUE);
            n3ds_log("HIDDEN HUD AIM FIXTURE: campaign HUD restored");
        }
        #endif
        sound_render();
        long long audio_end=n3ds_engine_ticks();
        n3ds_frontend_trace("render");
        #ifdef HALO_N3DS_BSP_PROBE
        if(frame+1==1000) {
            CHECK(!main_menu_is_active() && !n3ds_main_is_solo_multiplayer());
            unsigned int loading_before=n3ds_gpu_loading_submissions();
            CHECK(scenario_switch_structure_bsp(HALO_N3DS_BSP_PROBE));
            CHECK(loading_before==n3ds_gpu_loading_submissions());
            n3ds_log("PASS: in-game BSP transition submitted no loading-screen frames");
            snprintf(message,sizeof(message),"BSP PROBE: selected=%d for final renderer frame; not a campaign progression test",HALO_N3DS_BSP_PROBE);n3ds_log(message);
        }
        #endif
        CHECK(!n3ds_engine_mission_render(unit,game_time_get()));
#ifdef HALO_N3DS_SCENIC_TEXTURE
        if(frame+1==5400 && unit!=NONE) {
            const struct object_datum *scenic=object_get(unit);
            snprintf(message,sizeof(message),"SCENIC FIRST: frame=%ld pos=%.2f,%.2f,%.2f",
                frame+1,scenic->object.position.x,scenic->object.position.y,scenic->object.position.z);
            n3ds_log(message);
            CHECK(!n3ds_gpu_loading_capture(1));
            n3ds_log("SCENIC FIRST: frame captured after three minutes without bots");
        }
        if(frame+1==6000 && unit!=NONE) {
            const struct object_datum *scenic=object_get(unit);
            snprintf(message,sizeof(message),"SCENIC SECOND: frame=%ld pos=%.2f,%.2f,%.2f",
                frame+1,scenic->object.position.x,scenic->object.position.y,scenic->object.position.z);
            n3ds_log(message);
        }
        if((frame+1==6300 || frame+1==6600) && unit!=NONE) {
            unsigned int stage=frame+1==6300?2:3;
            CHECK(!n3ds_gpu_loading_capture(stage));
            snprintf(message,sizeof(message),"SCENIC FIRST ALTERNATE: frame=%ld captured",frame+1);
            n3ds_log(message);
        }
#endif
#ifdef HALO_N3DS_TEXTURE_CLIP
        if(n3ds_main_is_solo_multiplayer() && game_time_get()>=120 && game_time_get()<360 &&
           (game_time_get()-120)%4==0) {
            extern int n3ds_gpu_texture_clip_frame(unsigned int);
            CHECK(n3ds_gpu_texture_clip_frame((unsigned int)game_time_get()));
        }
#endif
#ifdef HALO_N3DS_TEXTURE_CLIP_DEATH
        if(n3ds_main_is_solo_multiplayer() && frame+1>=480 && frame+1<=1650 &&
           (frame+1-480)%15==0) {
            double work_ms=(n3ds_engine_ticks()-start)*1000.0/n3ds_engine_tick_frequency();
            double fps=work_ms>33.333?1000.0/work_ms:30.0;
            snprintf(message,sizeof(message),"TEXTURE CLIP PERF: frame=%ld work_ms=%.3f fps=%.2f textures=%u bytes=%u",
                frame+1,work_ms,fps,n3ds_engine_texture_count(),n3ds_engine_texture_bytes());
            n3ds_log(message);
            extern int n3ds_gpu_texture_clip_frame(unsigned int);
            CHECK(n3ds_gpu_texture_clip_frame((unsigned int)(frame+1)));
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_MENU_KEYS
        if(frame+1==menu_key_capture_frame) {
            CHECK(!n3ds_gpu_screen_test());
            n3ds_log("MENU KEYS: screen capture saved");
        }
#endif
#ifdef HALO_N3DS_EXPERIMENTAL_PC_ARSENAL
        if(frame+1==40) {
            snprintf(message,sizeof(message),"ARSENAL SETTINGS: active=%d widgets=%d menu=%d",
                n3ds_controls_active(),ui_widgets_active(),main_menu_is_active());n3ds_log(message);
            int failed=arsenal_copy_front("settings") | arsenal_copy_bottom("settings");
            if(failed)++arsenal_failures;
            n3ds_log(failed?"ARSENAL CAPTURE: settings FAILED":"ARSENAL CAPTURE: settings saved");
        }
        if(unit!=NONE) arsenal_after_render(frame+1,unit);
#endif
#if !defined(HALO_N3DS_AUDIO_TRACE) && (defined(HALO_N3DS_PROBE_A10) || (defined(HALO_N3DS_BOT_TEST_COUNT) && !defined(HALO_N3DS_EXPERIMENTAL_PC_UI)) || (defined(HALO_N3DS_EXPERIMENTAL_PC_UI) && !defined(HALO_N3DS_EXPERIMENTAL_PC_ALL_MAPS)))
        if(frame+1==300) {
            int capture_failed=n3ds_gpu_screen_test();
#ifdef HALO_N3DS_EXPERIMENTAL_PC_UI
            n3ds_log(capture_failed?"EXPERIMENTAL PC: screen capture check failed":"EXPERIMENTAL PC: screen capture saved");
#else
            n3ds_log(capture_failed?"PROBE A10: screen capture check failed":"PROBE A10: screen capture saved");
#endif
        }
#endif
        #if HALO_N3DS_MAW_CUTSCENE_TEST == 2
        {
            static unsigned int empty_bsp_frames;
            if(global_structure_bsp_index_get()==9) ++empty_bsp_frames;
            if(frame+1==HALO_N3DS_MISSION_TICKS) {
                CHECK(empty_bsp_frames>=30);
                snprintf(message,sizeof(message),"PASS: Maw finale empty BSP9 rendered %u frames",empty_bsp_frames);n3ds_log(message);
            }
        }
        #endif
        #ifdef HALO_N3DS_PLASMA_SHIELD_TEST
        {
            extern unsigned int n3ds_plasma_test_draws(void);
            static int captured;
            if(!captured && n3ds_plasma_test_draws()>0) {
                CHECK(!n3ds_gpu_screen_test());
                /* These are platform captures outside the emulated Xbox drives. */
                extern FILE *capture_fopen(const char *,const char *) __asm__("fopen");
                FILE *source=capture_fopen("sdmc:/halo-native-front.rgb","rb");
                FILE *capture=capture_fopen("sdmc:/halo-plasma-fixture.rgb","wb");
                CHECK(source && capture);
                byte buffer[4096];size_t bytes,total=0;
                while((bytes=fread(buffer,1,sizeof(buffer),source))>0) {CHECK(fwrite(buffer,1,bytes,capture)==bytes);total+=bytes;}
                CHECK(!ferror(source) && total==400*240*3);
                CHECK(!fclose(source) && !fclose(capture));
                captured=1;n3ds_log("PLASMA FIXTURE: captured illuminated map-owned shield; diagnostic intensity only");
            }
        }
        #endif
        n3ds_frontend_trace("render-complete");
        loop_profile[0]+=services_end-start;loop_profile[1]+=simulation_start-services_end;
        loop_profile[2]+=simulation_end-simulation_start;loop_profile[3]+=audio_end-simulation_end;
        loop_profile[4]+=n3ds_engine_ticks()-audio_end;++loop_frames;
        if((frame+1)%30==0) {
            double scale=1000.0/(n3ds_engine_tick_frequency()*(double)loop_frames);
            snprintf(message,sizeof(message),"FRONTEND PROFILE: time=%ld services_ms=%.3f input_ms=%.3f simulation_ms=%.3f audio_ms=%.3f render_ms=%.3f",game_time_get(),loop_profile[0]*scale,loop_profile[1]*scale,loop_profile[2]*scale,loop_profile[3]*scale,loop_profile[4]*scale);n3ds_log(message);
            memset(loop_profile,0,sizeof(loop_profile));loop_frames=0;
        }
        if(frame%30==0) {
            snprintf(message,sizeof(message),"FRONTEND FRAME: frame=%ld time=%ld menu=%d local=%ld unit=%08lx held=%08x",frame+1,game_time_get(),main_menu_is_active(),local,unit,sample.held);
            n3ds_log(message);
        }
#ifdef HALO_N3DS_POSTGAME_RETURN_TEST
        if(frame+1==832) CHECK(!n3ds_gpu_screen_quad_capture());
#endif
        if(HALO_N3DS_MISSION_TICKS && frame+1==200) CHECK(!n3ds_gpu_screen_quad_capture());
        input_frame_end();
        const long long pacing_stages[5]={services_end,simulation_start,simulation_end,audio_end,n3ds_engine_ticks()};
        frame_pacing_observe(start,pacing_stages,!main_menu_is_active() && !cinematic_in_progress());
        n3ds_gpu_frame_work((n3ds_engine_ticks()-start)*(1000.f/n3ds_engine_tick_frequency()));
        n3ds_engine_pace_frame(start);
    }
    #ifdef HALO_N3DS_PLASMA_SHIELD_TEST
    {
        extern unsigned int n3ds_plasma_test_draws(void);
        CHECK(n3ds_plasma_test_draws()>=12);
        snprintf(message,sizeof(message),"PASS: positive plasma renderer: %u map-owned draws",n3ds_plasma_test_draws());n3ds_log(message);
    }
    #endif
    snprintf(message,sizeof(message),"FRONTEND END: frames=%ld menu=%d time=%ld",frame,main_menu_is_active(),game_time_get());n3ds_log(message);
    game_time_end();game_dispose_from_old_map();game_unload();game_dispose();
    sound_dispose();
    /* The finished targets survive map teardown until rasterizer_dispose.
     * Capture only after releasing map/geometry/texture memory: even enough
     * aggregate free bytes may not contain a contiguous readback image.
     * This diagnostic is absent from live builds and from HOME Close. */
    if(HALO_N3DS_MISSION_TICKS && n3ds_engine_app_running()) CHECK(!n3ds_gpu_screen_test());
    #ifdef HALO_N3DS_MEMORY_CHECKS
    {
        extern int halo_engine_sound_tests(void);
        CHECK(!halo_engine_sound_tests());
    }
    #endif
    console_dispose();input_dispose();rasterizer_dispose();real_math_dispose();
    n3ds_log("PASS: frontend lifecycle probe; menu selection and gameplay routes require separate verification");
    return 0;
}
