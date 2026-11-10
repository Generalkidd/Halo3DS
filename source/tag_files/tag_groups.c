/* New3DS integration additions by Kevin Boggs. */
/*
TAG_GROUPS.C
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_files.h"
#include "byte_swapping.h"
#include "tag_groups.h"

#ifdef HALO_N3DS
#include "engine_cache.h"
/* Cached validation only: retain the original lookup on first use, on a
 * reference change, and after each map generation. Tag names are immutable. */
static struct {
    const struct tag_reference *reference;
    const char *name;
    unsigned long generation,group;
    long index;
} reference_cache[128];
#endif

/* ---------- public code */

long verify_tag_reference(
	const struct tag_reference *reference)
{
	long index;

	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3055, reference);
	#ifdef HALO_N3DS
    unsigned int slot=((unsigned long)reference>>3)&127u;
    unsigned long generation=n3ds_engine_tag_generation();
    if(reference_cache[slot].reference==reference &&
       reference_cache[slot].generation==generation &&
       reference_cache[slot].name==reference->name &&
       reference_cache[slot].group==reference->group_tag &&
       reference_cache[slot].index==reference->index)
        return reference->index;
#endif
    index = tag_loaded(reference->group_tag, reference->name);
#ifdef HALO_N3DS
    if(index==reference->index) {
        reference_cache[slot].reference=reference;
        reference_cache[slot].name=reference->name;
        reference_cache[slot].generation=generation;
        reference_cache[slot].group=reference->group_tag;
        reference_cache[slot].index=index;
    }
#endif
	
	match_vassert(
		"c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3061, reference->index==index,
		csprintf(temporary,
			"tag reference \"%s\" and actual index do not match: is %08lX but should be %08lX",
			reference->name,
			reference->index,
			index));

	return index;
}

void* tag_data_get_pointer(
	const struct tag_data *data,
	long offset, 
	long size) 
{
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3073, size>=0);
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3074, offset>=0 && offset+size<=data->size);

	return (void *)((byte *)data->address + offset);
}

void *tag_block_get_element_with_size(
	const struct tag_block *block,
	long index, 
	long element_size) 
{
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3084, block);
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3085, block->count>=0);
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3086, !block->definition || block->definition->element_size==element_size);

	match_vassert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3089, index>=0 && index<block->count,
		csprintf(temporary,
			"#%d is not a valid %s index in [#0,#%d)",
			index,
			block->definition ? block->definition->name : "<unknown>", block->count));
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3090, block->address);

	return (void *)((byte *)block->address + (index * element_size));
}
