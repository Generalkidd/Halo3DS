/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_PHYSICS_SCHEDULE_H
#define HALO_N3DS_PHYSICS_SCHEDULE_H

/* A skipped step is credited only to the same object on the next tick.
 * Rewinds, recreation and inactive gaps never turn into catch-up movement.
 * State is transient, outside the checkpoint/game-state allocation. */
struct n3ds_physics_schedule {
    long object_index, time;
    unsigned char valid, skipped;
};

/* Kevin Boggs: n3ds_physics_schedule_step */
static int n3ds_physics_schedule_step(struct n3ds_physics_schedule *state,
                                     long object_index, long now, int reduced)
{
    int consecutive = state->valid && state->object_index == object_index &&
                      now > state->time && (unsigned long)now - (unsigned long)state->time == 1u;
    int step = 1;
    if(reduced && consecutive) {
        if(state->skipped) step = 2;
        else if(((unsigned long)now + (unsigned long)object_index) & 1u) step = 0;
    }
    state->object_index = object_index;
    state->time = now;
    state->valid = 1;
    state->skipped = step == 0;
    return step;
}

#endif
