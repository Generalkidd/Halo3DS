/* Optional, reconstructible mirrors. Canonical C3D_Tex owners and their linear
 * bytes never change; only the renderer-owned sampler copy selects VRAM. */
#ifdef HALO_N3DS_TEXTURE_VRAM_MIRRORS
enum { TEXTURE_MIRROR_SLOTS=64, TEXTURE_MIRROR_BYTES=1024*1024,
    TEXTURE_MIRROR_RESERVE=1024*1024, TEXTURE_MIRROR_UPLOAD_BYTES=64*1024 };
enum texture_mirror_state { MIRROR_EMPTY, MIRROR_REQUESTED, MIRROR_READY,
    MIRROR_RETIRED, MIRROR_REJECTED };
static struct texture_mirror {
    void *owner,*source,*data;
    unsigned int identity,bytes,allocated;
    unsigned char state;
} texture_mirrors[TEXTURE_MIRROR_SLOTS];
static unsigned int texture_mirror_bytes,texture_mirror_uploads,texture_mirror_failures;
static int texture_mirror_eligible,texture_mirror_frozen,texture_mirror_candidate;

/* Revision IDs intentionally do not qualify. Lookup compares owner addresses
 * before dereferencing, so an expired queued request cannot touch freed RAM. */
static unsigned int texture_mirror_identity(void *resource)
{
    if(!resource)return 0;
    for(unsigned int link=immutable_bucket[identity_bucket(resource)];link;link=immutable[link-1].next)
        if(immutable[link-1].resource==resource)
            return link<=IMMUTABLE_SLOTS?immutable[link-1].id:0;
    return 0;
}
static unsigned int texture_mirror_size(const C3D_Tex *texture)
{
    if(!texture || !texture->data ||
       C3D_TexGetType((C3D_Tex *)texture)!=GPU_TEX_2D ||
       (texture->fmt!=GPU_RGBA8 && texture->fmt!=GPU_RGB8) ||
       texture->width<8 || texture->height<8 || texture->width>1024 || texture->height>1024 ||
       (texture->width&(texture->width-1)) || (texture->height&(texture->height-1)) ||
       texture->maxLevel>(unsigned int)C3D_TexCalcMaxLevel(texture->width,texture->height) ||
       texture->minLevel>texture->maxLevel)return 0;
    unsigned int size=texture->width*texture->height*(texture->fmt==GPU_RGBA8?4u:3u);
    if(texture->size!=size)return 0;
    size=C3D_TexCalcTotalSize(size,texture->maxLevel);
    /* Every accepted mip ends at least at 8x8. RGB8 is then a multiple of
     * 16 bytes too, matching the raw TextureCopy transfer granularity. */
    return size && !(size&15u) && size<=TEXTURE_MIRROR_UPLOAD_BYTES &&
        linearGetSize(texture->data)>=size?size:0;
}
static struct texture_mirror *texture_mirror_find(void *owner,unsigned int identity)
{
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i)
        if(texture_mirrors[i].state!=MIRROR_EMPTY && texture_mirrors[i].owner==owner &&
           texture_mirrors[i].identity==identity)return texture_mirrors+i;
    return NULL;
}
static void *texture_mirror_storage(void *owner,unsigned int identity,void *canonical)
{
    if(!texture_mirror_candidate || !identity)return canonical;
    struct texture_mirror *mirror=texture_mirror_find(owner,identity);
    return texture_mirror_candidate && mirror && mirror->state==MIRROR_READY &&
        mirror->source==canonical?mirror->data:canonical;
}
static void texture_mirror_request(void *owner)
{
    if(!texture_mirror_eligible || texture_mirror_frozen)return;
    unsigned int identity=texture_mirror_identity(owner);
    if(!identity || texture_mirror_find(owner,identity))return;
    unsigned int bytes=texture_mirror_size(owner);if(!bytes)return;
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i)if(texture_mirrors[i].state==MIRROR_EMPTY) {
        texture_mirrors[i]=(struct texture_mirror){owner,((C3D_Tex *)owner)->data,NULL,
            identity,bytes,0,MIRROR_REQUESTED};return;
    }
}
/* A mode change must repair retained samplers even if their next consumer
 * never calls bind_descriptor. Keep stage-specific wrap/filter/LOD overrides. */
static void texture_mirror_rebind(void)
{
    for(unsigned int stage=0;stage<3;++stage)if(bound[stage] && bound_sampler[stage].data) {
        C3D_Tex *owner=bound[stage]?bound[stage]:&neutral;
        if(C3D_TexGetType(owner)!=GPU_TEX_2D)continue;
        unsigned int identity=texture_mirror_identity(bound[stage]);
        void *data=texture_mirror_storage(bound[stage],identity,owner->data);
        if(bound_sampler[stage].data!=data) {
            bound_sampler[stage].data=data;
            C3D_TexBind(stage,bound_sampler+stage);++bind_updates;
        }
    }
}
void n3ds_gpu_texture_mirror_select(int candidate)
{
    texture_mirror_candidate=!!candidate && texture_mirror_eligible;
    texture_mirror_rebind();
}
static void texture_mirror_forget(void *owner)
{
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i) {
        struct texture_mirror *mirror=texture_mirrors+i;
        if(mirror->state==MIRROR_EMPTY || mirror->owner!=owner)continue;
        if(mirror->data)mirror->state=MIRROR_RETIRED;
        else memset(mirror,0,sizeof(*mirror));
    }
    /* Original owners are still alive here. Commands already recorded retain
     * their old VRAM addresses until the between-frame collector fences. */
    texture_mirror_rebind();
}
static void texture_mirror_wait(void)
{
    extern int n3ds_c3d_texture_mirror_wait_idle(void);
    /* Unlike the generic texture barrier, this cannot take its APT-close
     * shortcut after a copy has been dispatched. A stuck queue retains both
     * canonical source and unpublished destination until the process exits. */
    if(!n3ds_c3d_texture_mirror_wait_idle())abort();
}
void n3ds_gpu_texture_mirror_idle(int eligible,int freeze)
{
    texture_mirror_eligible=!!eligible;texture_mirror_frozen=!!freeze;
    if(!eligible)n3ds_gpu_texture_mirror_select(0);
    if(!initialized || freeze || n3ds_gpu_frame_active() || aptShouldClose() || !gspHasGpuRight())return;
    int work=0;
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i) {
        struct texture_mirror *mirror=texture_mirrors+i;
        if(mirror->state!=MIRROR_EMPTY && mirror->state!=MIRROR_RETIRED &&
           texture_mirror_identity(mirror->owner)!=mirror->identity) {
            if(mirror->data)mirror->state=MIRROR_RETIRED;
            else memset(mirror,0,sizeof(*mirror));
        }
        work|=mirror->state==MIRROR_RETIRED || (eligible && mirror->state==MIRROR_REQUESTED);
    }
    if(!work)return;
    texture_mirror_rebind();texture_mirror_wait();
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i) {
        struct texture_mirror *mirror=texture_mirrors+i;
        if(mirror->state==MIRROR_RETIRED) {
            vramFree(mirror->data);texture_mirror_bytes-=mirror->allocated;
            memset(mirror,0,sizeof(*mirror));
        }
    }
    if(!eligible || aptShouldClose() || !gspHasGpuRight())return;
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i) {
        struct texture_mirror *mirror=texture_mirrors+i;
        if(mirror->state!=MIRROR_REQUESTED)continue;
        C3D_Tex *owner=mirror->owner;
        if(texture_mirror_identity(owner)!=mirror->identity ||
           texture_mirror_size(owner)!=mirror->bytes || owner->data!=mirror->source) {
            memset(mirror,0,sizeof(*mirror));continue;
        }
        /* Rejections stay attached to this identity: no retry/eviction churn.
         * Map unload or an explicit revision makes the fixed slot reusable. */
        mirror->state=MIRROR_REJECTED;
        unsigned int free_bytes=vramSpaceFree(),rounded=(mirror->bytes+127u)&~127u;
        if(texture_mirror_bytes>TEXTURE_MIRROR_BYTES-rounded ||
           free_bytes<TEXTURE_MIRROR_RESERVE+rounded)return;
        mirror->data=vramAlloc(mirror->bytes);
        if(!mirror->data) {++texture_mirror_failures;return;}
        /* The allocator rounds to 128 bytes and can retain a larger tail
         * under metadata pressure. Charge actual storage, not payload bytes. */
        mirror->allocated=vramGetSize(mirror->data);
        if(mirror->allocated<mirror->bytes || mirror->allocated>TEXTURE_MIRROR_BYTES-texture_mirror_bytes ||
           vramSpaceFree()<TEXTURE_MIRROR_RESERVE) {
            ++texture_mirror_failures;vramFree(mirror->data);mirror->data=NULL;mirror->allocated=0;return;
        }
        texture_mirror_bytes+=mirror->allocated;
        if(R_FAILED(GSPGPU_FlushDataCache(mirror->source,mirror->bytes)) ||
           aptShouldClose() || !gspHasGpuRight()) {
            ++texture_mirror_failures;vramFree(mirror->data);
            texture_mirror_bytes-=mirror->allocated;mirror->data=NULL;mirror->allocated=0;return;
        }
        /* The checked adapter returns only before dispatch or after exact
         * queue + callback retirement. A generic PPF event is insufficient. */
        extern int n3ds_c3d_texture_mirror_copy(void *,void *,unsigned int);
        if(!n3ds_c3d_texture_mirror_copy(mirror->source,mirror->data,mirror->bytes) ||
           aptShouldClose() || !gspHasGpuRight()) {
            ++texture_mirror_failures;vramFree(mirror->data);
            texture_mirror_bytes-=mirror->allocated;mirror->data=NULL;mirror->allocated=0;return;
        }
        mirror->state=MIRROR_READY;++texture_mirror_uploads;
        texture_mirror_rebind();return; /* <= one 64 KiB upload per boundary */
    }
}
void n3ds_gpu_texture_mirror_stats(unsigned int *resident,unsigned int *bytes,
    unsigned int *uploads,unsigned int *failures)
{
    unsigned int count=0;
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i)count+=texture_mirrors[i].state==MIRROR_READY;
    if(resident)*resident=count;
    if(bytes)*bytes=texture_mirror_bytes; /* includes retired, not yet reclaimed storage */
    if(uploads)*uploads=texture_mirror_uploads;
    if(failures)*failures=texture_mirror_failures;
}
static void texture_mirror_dispose(void)
{
    n3ds_gpu_texture_mirror_select(0);
    texture_mirror_wait();
    for(unsigned int i=0;i<TEXTURE_MIRROR_SLOTS;++i)if(texture_mirrors[i].data)vramFree(texture_mirrors[i].data);
    memset(texture_mirrors,0,sizeof(texture_mirrors));
    texture_mirror_bytes=texture_mirror_uploads=texture_mirror_failures=0;
    texture_mirror_eligible=texture_mirror_frozen=texture_mirror_candidate=0;
}
#endif
