#include "engine_action_replay.h"

#if HALO_N3DS_ACTION_REPLAY
#include <errno.h>
#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/ui_widget.h"
#include "items/weapons.h"
#include "math/random_math.h"
#include "memory/data.h"
#include "scenario/scenario.h"
#include "units/units.h"
#include "engine_cache.h"
#include "cache_reader.h"

/* The compatibility wrappers translate Xbox paths under halo-source.
 * This journal uses the native SD root. */
#undef fopen
#undef remove
#undef rename

enum { REPLAY_VERSION=2,
    REPLAY_LIMIT=HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS ? HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS : 3600,
    REPLAY_ACTION_WORDS=9 };
static const char replay_path[]="sdmc:/halo-action-replay.bin";
static const char replay_temporary[]="sdmc:/halo-action-replay.tmp";
static const char replay_magic[8]={'H','3','D','S','A','C','T',0};
struct replay_state {
    uint64_t world,scripts;
    uint32_t rng,local_rng;
    int32_t bsp;
    uint32_t reserved;
};
struct replay_header {
    char magic[8],scenario[128];
    uint32_t version,count,first_tick,map_checksum,tag_crc,map_length;
    int32_t difficulty;
    uint32_t reserved;
    struct replay_state initial;
    uint64_t checksum;
};
struct replay_tick {
    uint32_t tick;
    int32_t player,bsp_before;
    uint32_t available,action[REPLAY_ACTION_WORDS],reserved;
    struct replay_state after;
};
_Static_assert(sizeof(struct replay_header)==208,"Replay file header layout");
_Static_assert(sizeof(struct replay_tick)==88,"Replay tick layout");

static struct replay_header header;
static struct replay_tick *records;
static unsigned int count,tag_generation;
static int started,finished,failed,tick_open,action_seen;
extern void n3ds_log(const char *message);
extern boolean n3ds_main_is_solo_multiplayer(void);

static int replay_error(const char *reason)
{
    if(!failed) {
        char message[192];
        snprintf(message,sizeof(message),"ACTION REPLAY FAIL: sample=%u %s",count,reason);
        n3ds_log(message);
    }
    failed=1;
    return 0;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits,&value,sizeof(bits));
    return bits;
}

static float action_float(uint32_t bits)
{
    float value;
    memcpy(&value,&bits,sizeof(value));
    return value;
}

static uint64_t world_digest(void)
{
    uint64_t hash=N3DS_ACTION_REPLAY_HASH_INIT;
#define FIELD(value) (hash=n3ds_action_replay_mix(hash,(uint32_t)(value)))
#define REAL(value) FIELD(float_bits(value))
    struct data_iterator players;
    const struct player_datum *player;
    data_iterator_new(&players,player_data);
    while((player=data_iterator_next(&players))) {
        FIELD(players.datum_index);
        FIELD(player->unit_index);
        FIELD(player->dead_unit_index);
        FIELD(player->team_index);
        FIELD(player->respawn_timer);
        FIELD(player->respawn_penalty);
        FIELD(player->statistics.deaths);
    }
    FIELD(NONE);
    struct object_iterator objects;
    const struct object_datum *datum;
    object_iterator_new(&objects,_object_mask_all,0);
    while((datum=object_iterator_next(&objects))) {
        const struct _object_datum *object=&datum->object;
        FIELD(objects.index);
        FIELD(datum->definition_index);
        FIELD(object->type);
        FIELD(object->owner_object_index);
        FIELD(object->parent_object_index);
        FIELD(object->parent_node_index);
        FIELD(object->location.leaf_index);
        FIELD(object->location.cluster_index);
        for(unsigned int i=0;i<3;++i) {
            REAL(object->position.n[i]);
            REAL(object->forward.n[i]);
            REAL(object->up.n[i]);
            REAL(object->translational_velocity.n[i]);
            REAL(object->angular_velocity.n[i]);
        }
        REAL(object->scale);
        REAL(object->body_vitality);
        REAL(object->shield_vitality);
        FIELD(object->shield_damage_decay_timer);
        FIELD(object->body_damage_decay_timer);
        FIELD(object->shield_stun_ticks);
        FIELD(object->damage_flags);
        FIELD(object->animation.animation_graph_index);
        FIELD(object->animation.state.index);
        FIELD(object->animation.state.frame_index);
        FIELD(object->animation.interpolation_frame_index);
        FIELD(object->animation.interpolation_frame_count);
        if(TEST_FLAG(_object_mask_unit,object->type)) {
            const struct _unit_datum *unit=&((const struct unit_datum *)datum)->unit;
            FIELD(unit->actor_index);
            FIELD(unit->control_flags);
            FIELD(unit->timer);
            FIELD(unit->persistent_control_timer);
            FIELD(unit->player_index);
            FIELD(unit->current_weapon_index);
            FIELD(unit->desired_weapon_index);
            FIELD(unit->grenade_throw_state);
            FIELD(unit->grenade_throw_ticks);
            FIELD(unit->current_zoom_level);
            FIELD(unit->animation.state);
            for(unsigned int i=0;i<MAXIMUM_WEAPONS_PER_UNIT;++i)
                FIELD(unit->weapon_object_indices[i]);
            for(unsigned int i=0;i<NUMBER_OF_UNIT_GRENADE_TYPES;++i)
                FIELD(unit->grenade_counts[i]);
        } else if(object->type==_object_type_weapon) {
            const struct _weapon_datum *weapon=&((const struct weapon_datum *)datum)->weapon;
            FIELD(weapon->state);
            FIELD(weapon->state_timer);
            FIELD(weapon->game_time_last_fired);
            REAL(weapon->heat);
            REAL(weapon->age);
            for(unsigned int i=0;i<NUMBEROF(weapon->magazines);++i) {
                FIELD(weapon->magazines[i].state);
                FIELD(weapon->magazines[i].state_timer);
                FIELD(weapon->magazines[i].rounds_total);
                FIELD(weapon->magazines[i].rounds_loaded);
                FIELD(weapon->magazines[i].rounds_fractional_recharged);
            }
            for(unsigned int i=0;i<NUMBEROF(weapon->triggers);++i) {
                FIELD(weapon->triggers[i].state);
                FIELD(weapon->triggers[i].state_timer);
            }
        }
    }
    FIELD(NONE);
#undef FIELD
#undef REAL
    return hash;
}

static struct replay_state replay_state_now(void)
{
    return (struct replay_state){.world=world_digest(),.scripts=n3ds_action_replay_script_digest(),
        .rng=get_random_seed(),.local_rng=*get_global_local_random_seed_address(),
        .bsp=global_structure_bsp_index_get()};
}

static int state_equal(const struct replay_state *a,const struct replay_state *b)
{
    if(a->bsp==b->bsp && a->rng==b->rng && a->local_rng==b->local_rng &&
       a->world==b->world && a->scripts==b->scripts)return 1;
    char message[144];
    snprintf(message,sizeof(message),"state differs: bsp=%d global_rng=%d local_rng=%d objects/players=%d scripts=%d",
        a->bsp!=b->bsp,a->rng!=b->rng,a->local_rng!=b->local_rng,a->world!=b->world,a->scripts!=b->scripts);
    return replay_error(message);
}

static void action_encode(const struct player_action *action,uint32_t words[REPLAY_ACTION_WORDS])
{
    words[0]=action->control_flags;
    words[1]=float_bits(action->desired_facing.yaw);
    words[2]=float_bits(action->desired_facing.pitch);
    words[3]=float_bits(action->throttle.i);
    words[4]=float_bits(action->throttle.j);
    words[5]=float_bits(action->primary_trigger);
    words[6]=(int32_t)action->desired_weapon_index;
    words[7]=(int32_t)action->desired_grenade_index;
    words[8]=(int32_t)action->desired_zoom_level;
}

static struct player_action action_decode(const uint32_t words[REPLAY_ACTION_WORDS])
{
    return (struct player_action){.control_flags=words[0],
        .desired_facing={.yaw=action_float(words[1]),.pitch=action_float(words[2])},
        .throttle={.i=action_float(words[3]),.j=action_float(words[4])},
        .primary_trigger=action_float(words[5]),.desired_weapon_index=(int32_t)words[6],
        .desired_grenade_index=(int32_t)words[7],.desired_zoom_level=(int32_t)words[8]};
}

static uint64_t checksum_bytes(uint64_t hash,const void *memory,unsigned int bytes)
{
    const byte *data=memory;
    for(unsigned int i=0;i<bytes;++i)hash=n3ds_action_replay_mix(hash,data[i]);
    return hash;
}

static uint64_t replay_checksum(void)
{
    uint64_t hash=checksum_bytes(N3DS_ACTION_REPLAY_HASH_INIT,&header,offsetof(struct replay_header,checksum));
    return checksum_bytes(hash,records,header.count*sizeof(*records));
}

static int replay_read(void)
{
    FILE *input=fopen(replay_path,"rb");
    if(!input)return replay_error("cannot open sdmc:/halo-action-replay.bin");
    int ok=fread(&header,sizeof(header),1,input)==1 &&
        !memcmp(header.magic,replay_magic,sizeof(replay_magic)) && header.version==REPLAY_VERSION &&
        header.count>0 && header.count<=REPLAY_LIMIT && header.first_tick<=INT32_MAX-header.count &&
        !header.reserved && !header.initial.reserved && memchr(header.scenario,0,sizeof(header.scenario)) &&
        fread(records,sizeof(*records),header.count,input)==header.count &&
        fgetc(input)==EOF && !ferror(input);
    fclose(input);
    if(!ok || header.checksum!=replay_checksum())return replay_error("invalid, incomplete or incompatible file");
#if HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS
    if(header.count!=REPLAY_LIMIT)return replay_error("unexpected fixture journal length");
#endif
    for(unsigned int i=0;i<header.count;++i) {
        const struct replay_tick *sample=records+i;
        if(sample->tick!=header.first_tick+i || sample->reserved || sample->after.reserved ||
           sample->available!=(unsigned int)(sample->player!=NONE))
            return replay_error("invalid tick sequence or action presence");
        if(!sample->available)continue;
        for(unsigned int j=1;j<=5;++j)
            if(!isfinite(action_float(sample->action[j])))return replay_error("nonfinite recorded action");
        int32_t weapon=sample->action[6],grenade=sample->action[7],zoom=sample->action[8];
        /* Dequeue and unit_control both enforce strict inventory bounds. */
        if(weapon<NONE || weapon>=MAXIMUM_WEAPONS_PER_UNIT || grenade<NONE ||
           grenade>=NUMBER_OF_UNIT_GRENADE_TYPES || zoom<NONE || zoom>INT16_MAX)
            return replay_error("recorded action index out of range");
    }
    return 1;
}

static int replay_start(uint32_t tick)
{
    const struct n3ds_cache_view *map=n3ds_engine_cache_view(0);
    const char *scenario=tag_get_name(global_scenario_index);
    if(!scenario || strlen(scenario)>=sizeof(header.scenario) || !map || !map->tags)
        return replay_error("scenario identity unavailable");
    records=(calloc)(REPLAY_LIMIT,sizeof(*records));
    if(!records)return replay_error("action buffer allocation failed");
    struct replay_state state=replay_state_now();
    if(HALO_N3DS_ACTION_REPLAY==2) {
        if(!replay_read())return 0;
        if(header.first_tick!=tick || strcmp(header.scenario,scenario) ||
           header.map_checksum!=map->header.checksum || header.tag_crc!=map->source_tags_crc ||
           header.map_length!=(uint32_t)map->header.file_length || header.difficulty!=game_difficulty_level_get())
            return replay_error("scenario, map, difficulty or start tick differs");
        if(!state_equal(&header.initial,&state))return 0;
    } else {
        FILE *existing=fopen(replay_path,"rb");
        if(existing) {
            fclose(existing);
            return replay_error("move the existing sdmc:/halo-action-replay.bin before recording");
        }
        if(errno!=ENOENT)return replay_error("cannot check the replay output path");
        memcpy(header.magic,replay_magic,sizeof(replay_magic));
        strcpy(header.scenario,scenario);
        header.version=REPLAY_VERSION;
        header.first_tick=tick;
        header.map_checksum=map->header.checksum;
        header.tag_crc=map->source_tags_crc;
        header.map_length=map->header.file_length;
        header.difficulty=game_difficulty_level_get();
        header.initial=state;
    }
    tag_generation=n3ds_engine_tag_generation();
    started=1;
    n3ds_log(HALO_N3DS_ACTION_REPLAY==1?"ACTION REPLAY: recording campaign ticks":"ACTION REPLAY: verified file and starting state");
    return 1;
}

int n3ds_action_replay_begin_tick(long tick)
{
    if(failed || finished)return 0;
    if(!started && main_menu_is_active())return 1;
    if(main_menu_is_active() || game_connection()!=_game_connection_local ||
       n3ds_main_is_solo_multiplayer() || global_scenario_index==NONE)
        return replay_error("a single local campaign map is required");
    if(tick<0 || (!started && tick>INT32_MAX-REPLAY_LIMIT))return replay_error("invalid game tick");
    if(!started && !replay_start((uint32_t)tick))return 0;
    if(tick_open || (uint32_t)tick!=header.first_tick+count || tag_generation!=n3ds_engine_tag_generation())
        return replay_error("game tick discontinuity or map replacement");
    int bsp=global_structure_bsp_index_get();
    if(HALO_N3DS_ACTION_REPLAY==1) {
        records[count].tick=tick;
        records[count].bsp_before=bsp;
    } else if(records[count].bsp_before!=bsp)return replay_error("BSP differs before tick");
    tick_open=1;
    action_seen=0;
    return 1;
}

int n3ds_action_replay_apply(struct player_action *actions,unsigned int slots,int live)
{
    if(failed)return 0;
    if(!tick_open)return live;
    if(action_seen)return replay_error("multiple action consumptions within one tick");
    if(!live)return replay_error("ordinary action queue did not dequeue");
    action_seen=1;
    long player=data_next_index(player_data,NONE);
    if(player!=NONE && (data_next_index(player_data,player)!=NONE || (unsigned short)player>=slots))
        return replay_error("unsupported player count or action slot");
    unsigned int slot=(unsigned short)player;
    struct replay_tick *sample=records+count;
    if(HALO_N3DS_ACTION_REPLAY==1) {
        sample->player=player;
        sample->available=player!=NONE;
        if(sample->available)action_encode(actions+slot,sample->action);
        return live;
    }
    if(sample->player!=player)return replay_error("active player identity differs");
    if(sample->available)actions[slot]=action_decode(sample->action);
    return live;
}

void n3ds_action_replay_end_tick(void)
{
    if(!tick_open)return;
    tick_open=0;
    if(failed)return;
    if(!action_seen || tag_generation!=n3ds_engine_tag_generation()) {
        replay_error("action seam missing or map replaced during tick");
        return;
    }
    const struct replay_state state=replay_state_now();
    if(HALO_N3DS_ACTION_REPLAY==1)records[count].after=state;
    else if(!state_equal(&records[count].after,&state))return;
    ++count;
    finished=count==(HALO_N3DS_ACTION_REPLAY==1?REPLAY_LIMIT:header.count);
}

int n3ds_action_replay_failed(void)
{
    return failed;
}

void n3ds_action_replay_restore(void)
{
    /* Loading the starting checkpoint is allowed. A later restore can keep
     * the same tick and map generation, so detect it at publication. */
    if(started && !finished)replay_error("checkpoint restored during the route");
}

int n3ds_action_replay_should_exit(void)
{
    if(started && !finished && !failed &&
       (!game_in_progress() || main_menu_is_active() || tag_generation!=n3ds_engine_tag_generation()))
        replay_error("campaign ended or map was replaced");
    return failed || finished;
}

int n3ds_action_replay_owns_input(void)
{
    return HALO_N3DS_ACTION_REPLAY==2 && started;
}

static int replay_write(void)
{
    header.count=count;
    header.checksum=replay_checksum();
    FILE *output=fopen(replay_temporary,"wb");
    if(!output)return replay_error("cannot open replay output");
    int ok=fwrite(&header,sizeof(header),1,output)==1 &&
        fwrite(records,sizeof(*records),count,output)==count;
    if(fclose(output)!=0)ok=0;
    if(!ok || rename(replay_temporary,replay_path)!=0) {
        remove(replay_temporary);
        return replay_error("could not publish a complete replay file");
    }
    return 1;
}

int n3ds_action_replay_finish(int clean_exit)
{
    if(!clean_exit)failed=1;
    if(!failed && (!count || tick_open || (HALO_N3DS_ACTION_REPLAY==2 && !finished)))
        replay_error("route ended before replay verification completed");
#if HALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS
    if(!failed && count!=REPLAY_LIMIT)replay_error("fixture stopped before its tick limit");
#endif
    if(!failed && HALO_N3DS_ACTION_REPLAY==1)replay_write();
    if(!failed) {
        char message[144];
        snprintf(message,sizeof(message),"ACTION REPLAY PASS: %s %u ticks (first=%lu), sdmc:/halo-action-replay.bin",
            HALO_N3DS_ACTION_REPLAY==1?"recorded":"matched",count,(unsigned long)header.first_tick);
        n3ds_log(message);
    }
    free(records);
    records=NULL;
    return !failed;
}
#endif
