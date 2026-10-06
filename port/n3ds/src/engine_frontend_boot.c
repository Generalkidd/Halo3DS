#include "engine_checkpoint.h"
/* Original main-menu widgets and lifecycle on the native renderer. */
#include "cseries.h"
#include "sound/sound_manager.h"
#include "cache/sound_cache.h"
#include "units/units.h"
#include "engine_game.h"
#include "engine_hud.h"
#include "engine_generic.h"
#include "engine_chicago.h"
#include "engine_transparent.h"
#include "errors.h"
#include "game_state.h"
#include "game.h"
#include "players.h"
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
#include "player_control.h"
#include "cutscene/cinematics.h"
#include "render/render_sprite.h"
#include "engine_widgets.h"
#include "engine_dynamic_geometry.h"
#include "engine_cache.h"
#include "cache_reader.h"
#include "physics/collision_usage.h"
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
    CHECK(n3ds_engine_texture_resolution(original_model?32:64));
    n3ds_log(original_model?
        "PERFORMANCE PROFILE: original; world 320x192 adaptive/RGB565, textures32, LOD gameplay1/cinematic1/weapon2/distant0, foliage1/4-1/6, base-lit materials, particles48/draw64, emitter cap12, sprite layers2, radar16; full-rate controls/HUD":
        "PERFORMANCE PROFILE: New; world textures64, LOD gameplay3/closeup cinematics/distant1-2, cosmetic particles144/draw192, dense emitter cap32, large sprite layers8, radar32; full-rate controls/HUD");
    if(HALO_N3DS_MISSION_TICKS) {
        CHECK(!n3ds_gpu_plasma_lut_tests());
        CHECK(!n3ds_gpu_texture_plasma_tests());
        n3ds_log("STARTUP: sampled texture checks");
        CHECK(!n3ds_engine_sampled_texture_tests());
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
    for(frame=0;(!HALO_N3DS_MISSION_TICKS || frame<HALO_N3DS_MISSION_TICKS) && n3ds_engine_app_running();++frame) {
        struct native_input_sample sample;
        long long start=n3ds_engine_ticks();long local,player,unit=NONE;
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
        n3ds_input_gameplay_controls(game_in_progress() && !main_menu_is_active() &&
            !ui_widgets_active() && player_input_enabled() && !cinematic_in_progress());
        if((sample.held&(NATIVE_KEY_START|NATIVE_KEY_SELECT))==(NATIVE_KEY_START|NATIVE_KEY_SELECT)) break;
        n3ds_engine_input_sample(&sample);input_update();input_abstraction_update();
        if(frame%30==0 || memcmp(&sample,&prior,sizeof(sample))) {
            snprintf(message,sizeof(message),"FRONTEND INPUT: frame=%ld held=%08x circle=%d,%d look=%d,%d enabled=%d cinematic=%d skip=%d touch=%u,%u",frame+1,sample.held,sample.circle_x,sample.circle_y,sample.cstick_x,sample.cstick_y,player_input_enabled(),cinematic_in_progress(),cinematic_can_be_skipped(),sample.touch_x,sample.touch_y);n3ds_log(message);prior=sample;
        }
        event_manager_update();n3ds_frontend_trace("widgets");process_ui_widgets();bink_playback_update();
        n3ds_frontend_trace("simulation");
        long long simulation_start=n3ds_engine_ticks();
        if(game_in_progress()) {
            player_control_update(1.f/30.f);game_time_update(1.f/30.f);
            director_update(1.f/30.f);observer_update(1.f/30.f);
            n3ds_main_mission_save_step(FALSE);
        }
        local=local_player_get_next(NONE);
        if(local!=NONE) {player=local_player_get_player_index(local);if(player!=NONE) unit=player_get(player)->unit_index;}
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
