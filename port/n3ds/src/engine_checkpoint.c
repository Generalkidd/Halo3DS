#include "cseries.h"
#include "engine_checkpoint.h"
#include "memory/data.h"
#include "memory/memory_pool.h"
#include "memory/lruv_cache.h"
#include "saved games/game_state.h"
void *n3ds_game_state_identity(unsigned long long *generation);
void n3ds_log(const char *);
static struct allocation {const char *name,*type;byte *base;unsigned int size;} allocations[128];
static unsigned int allocation_count;
void n3ds_checkpoint_register(const char *name,const char *type,void *base,unsigned int size)
{
    if(!strcmp(name,"header")) allocation_count=0;
    assert(allocation_count<128);
    allocations[allocation_count++]=(struct allocation){name,type,base,size};
}
static int array_visit(struct data_array *a,n3ds_checkpoint_visitor visit)
{ return visit(&a->data,0); }
unsigned long n3ds_checkpoint_callback(unsigned int index)
{
    /* Symbolic callback IDs survive relinking the executable. */
    for(unsigned int i=0;i<allocation_count;++i) if(!strcmp(allocations[i].name,"decal vertex cache")) {
        struct lruv_cache *c=(void *)allocations[i].base;
        return index==1?(unsigned long)c->delete_block_proc:index==2?(unsigned long)c->locked_block_proc:0;
    }
    return 0;
}
int n3ds_checkpoint_visit(n3ds_checkpoint_visitor visit)
{
    for(unsigned int i=0;i<allocation_count;++i) {
        struct allocation *a=&allocations[i];
        if(a->type && !strcmp(a->type,"data array")) {
            struct data_array *d=(void *)a->base;
            if(!array_visit(d,visit)) return 0;
            /* These are the pointer-bearing datum layouts in the native arena.
             * Other datum types store handles/indices, not memory addresses.
             * Sizes/offsets are audited against the ARM compiler's DWARF. */
            unsigned int offsets[3],count=0,size=0;
            if(!strcmp(a->name,"object")) {size=12;offsets[count++]=8;}
            else if(!strcmp(a->name,"effect")) {size=252;offsets[count++]=48;offsets[count++]=52;offsets[count++]=56;}
            else if(!strcmp(a->name,"glow")) {size=604;offsets[count++]=592;offsets[count++]=596;}
            else if(!strcmp(a->name,"glow particles")) {size=100;offsets[count++]=92;offsets[count++]=96;}
            else if(!strcmp(a->name,"recorded animations")) {size=100;offsets[count++]=16;}
            if(size && d->size!=size) return 0;
            if(count && d->valid) for(int j=0;j<d->maximum_count;++j) {
                byte *p=(byte *)d->data+j*d->size;
                if(!*(short *)p) continue;
                for(unsigned int k=0;k<count;++k) if(!visit(p+offsets[k],0)) return 0;
            }
        } else if(a->type && !strcmp(a->type,"memory pool")) {
            struct memory_pool *p=(void *)a->base;
            if(!visit(&p->base_address,0) || !visit(&p->first_block,0) || !visit(&p->last_block,0)) return 0;
            struct memory_pool_block *b=p->first_block;
            unsigned int guard=0;
            while(b) {
                if(++guard>4096 || (byte *)b<a->base+sizeof(*p) || (byte *)(b+1)>a->base+a->size) return 0;
                if(!visit(&b->reference,0) || !visit(&b->next_block,0) || !visit(&b->previous_block,0)) return 0;
                b=b->next_block;
            }
        } else if(a->type && !strcmp(a->type,"lruv cache")) {
            struct lruv_cache *c=(void *)a->base;
            if(!visit(&c->delete_block_proc,0) || !visit(&c->locked_block_proc,0) ||
               !visit(&c->blocks,0) || !array_visit(c->blocks,visit)) return 0;
        } else if(!strcmp(a->name,"screen effect filth")) {
            if(a->size!=120 || !visit(a->base+8,0) || !visit(a->base+40,0) || !visit(a->base+52,0)) return 0;
        }
    }
    return n3ds_checkpoint_hs_visit(visit) && n3ds_checkpoint_hud_visit(visit) && n3ds_checkpoint_detail_visit(visit);
}
