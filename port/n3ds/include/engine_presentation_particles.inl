/* The sprite path borrows two adjacent render samples. Particle update,
 * LOD admission, order, frame selection and lifetime stay authoritative. */
#include "engine_cache.h"
#include "game/game.h"
#include "scenario/scenario.h"

struct native_particle_sample {
    long index, definition;
    union {
        real_point3d position;
        struct {
            long object_index;
            short node_index;
            byte local_player_index;
        } parent;
    } placement;
    real age, lifespan;
    long leaf;
    short cluster;
    word flags;
    byte state, attached;
};

static struct native_particle_bank {
    unsigned int tick, generation;
    int bsp;
    struct native_particle_sample samples[MAXIMUM_RENDERED_PARTICLES];
} *particle_banks;
static unsigned int particle_latest, particle_sample_count;
static int particle_allocation_failed;

void n3ds_presentation_particles_reset(void)
{
    particle_sample_count=0;
}

void n3ds_presentation_particles_capture(void)
{
    if(!particle_banks) {
        if(particle_allocation_failed)return;
        particle_banks=(malloc)(2*sizeof(*particle_banks));
        if(!particle_banks) {
            particle_allocation_failed=1;
            n3ds_algorithm_profile_add(N3DS_ALG_CPU_PARTICLE_PRESSURE,1);
            return;
        }
    }
    unsigned int tick=game_time_get(), generation=n3ds_engine_cache_generation();
    int bsp=global_structure_bsp_index_get();
    const struct native_particle_bank *latest=&particle_banks[particle_latest];
    if(particle_sample_count && latest->tick==tick && latest->generation==generation && latest->bsp==bsp)
        return;
    particle_sample_count=particle_sample_count && tick-latest->tick==1u &&
        latest->generation==generation && latest->bsp==bsp ? 2u : 1u;
    particle_latest^=1u;
    struct native_particle_bank *bank=&particle_banks[particle_latest];
    bank->tick=tick;
    bank->generation=generation;
    bank->bsp=bsp;
    for(unsigned int slot=0;slot<MAXIMUM_RENDERED_PARTICLES;++slot)bank->samples[slot].index=NONE;
    unsigned int captured=0;
    for(long index=data_next_index(particle_data,NONE);index!=NONE;index=data_next_index(particle_data,index)) {
        const struct particle_datum *particle=particle_get(index);
        unsigned int slot=DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
        int attached=particle->object_index!=NONE;
        if(slot>=MAXIMUM_RENDERED_PARTICLES ||
           (attached && TEST_FLAG(particle->flags,_particle_datum_attached_to_local_player_bit)))continue;
        bank->samples[slot]=(struct native_particle_sample){
            .index=index, .definition=particle->definition_index,
            .age=particle->age, .lifespan=particle->lifespan, .leaf=particle->location.leaf_index,
            .cluster=particle->location.cluster_index, .flags=particle->flags, .state=particle->state,
            .attached=attached,
        };
        struct native_particle_sample *sample=&bank->samples[slot];
        if(attached) {
            sample->placement.parent.object_index=particle->object_index;
            sample->placement.parent.node_index=particle->node_index;
            sample->placement.parent.local_player_index=particle->local_player_index;
        } else sample->placement.position=particle->position;
        ++captured;
    }
    n3ds_algorithm_profile_add(N3DS_ALG_CPU_PARTICLE_SNAPSHOT,captured);
}

static const struct native_particle_sample *native_particle_pair(unsigned int slot,
    const struct particle_datum *live, const struct native_particle_sample **previous_sample)
{
    if(particle_sample_count!=2 || slot>=MAXIMUM_RENDERED_PARTICLES)return NULL;
    const struct native_particle_sample *previous=&particle_banks[particle_latest^1u].samples[slot];
    const struct native_particle_sample *current=&particle_banks[particle_latest].samples[slot];
    if(current->index==NONE || previous->index!=current->index ||
       ((unsigned long)current->index>>16)!=(unsigned short)live->header.identifier ||
       current->definition!=live->definition_index || previous->definition!=current->definition ||
       previous->leaf!=current->leaf || previous->cluster!=current->cluster ||
       current->leaf!=live->location.leaf_index || current->cluster!=live->location.cluster_index ||
       previous->flags!=current->flags || current->flags!=live->flags ||
       previous->state!=current->state || current->state!=live->state ||
       previous->attached!=current->attached || current->attached!=(live->object_index!=NONE) ||
       previous->lifespan!=current->lifespan || current->lifespan!=live->lifespan ||
       current->age!=live->age || !(current->age>previous->age && current->age-previous->age<=0.05f))
        return NULL;
    *previous_sample=previous;
    return current;
}

static int native_particle_midpoint(unsigned int slot, const struct particle_datum *live, real_point3d *position)
{
    const struct native_particle_sample *previous;
    const struct native_particle_sample *current=native_particle_pair(slot,live,&previous);
    if(!current || current->attached ||
       memcmp(&current->placement.position,&live->position,sizeof(live->position)) ||
       !(distance_squared3d(&previous->placement.position,&current->placement.position)<=0.25f))return 0;
    points_interpolate(&previous->placement.position,&current->placement.position,0.5f,position);
    return 1;
}

static const real_matrix4x3 *native_particle_midpoint_parent(unsigned int slot, const struct particle_datum *live)
{
    const struct native_particle_sample *previous;
    const struct native_particle_sample *current=native_particle_pair(slot,live,&previous);
    if(!current || !current->attached ||
       previous->placement.parent.object_index!=live->object_index ||
       current->placement.parent.object_index!=live->object_index ||
       previous->placement.parent.node_index!=live->node_index ||
       current->placement.parent.node_index!=live->node_index ||
       previous->placement.parent.local_player_index!=live->local_player_index ||
       current->placement.parent.local_player_index!=live->local_player_index)return NULL;
    return n3ds_presentation_particle_parent_node(live->object_index,live->node_index);
}
