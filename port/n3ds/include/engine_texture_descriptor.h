#ifndef HALO_N3DS_ENGINE_TEXTURE_DESCRIPTOR_H
#define HALO_N3DS_ENGINE_TEXTURE_DESCRIPTOR_H

/* Engine-owned IDs: never serialize a GPU_TEXCOLOR or a native pointer.
 * Unknown CPU consumers conservatively require readable RGBA8 bytes. */
enum {
    NATIVE_TEXTURE_FORMAT_RGBA8=1,
    NATIVE_TEXTURE_FORMAT_RGB8=2,
    NATIVE_TEXTURE_FORMAT_LA8=3,
    NATIVE_TEXTURE_CONVERSION_RGBA8=1,
    NATIVE_TEXTURE_CPU_RGBA8=1,
    NATIVE_TEXTURE_CPU_UNKNOWN=2,
    NATIVE_TEXTURE_MAX_FACES=6
};
typedef char native_texture_uint32_required[(sizeof(unsigned int)==4)?1:-1];
struct native_texture_face {unsigned int offset,bytes;};
struct native_texture_descriptor {
    unsigned int format,width,height,faces,levels,source_mip,conversion_policy,cpu_requirements;
    /* Selected original input size, final GPU image bytes, and peak temporary
     * CPU preparation bytes. Scratch already includes any input staging; do
     * not add source_bytes again when accounting resident plus scratch. */
    unsigned int source_bytes,resident_bytes,scratch_bytes;
    /* Canonical ranges in a logical, contiguous resident payload. A 2D span
     * includes its whole mip chain. Actual cube faces allocate separately. */
    struct native_texture_face face[NATIVE_TEXTURE_MAX_FACES];
};
struct native_texture_span {void *data;unsigned int bytes;};

static inline unsigned int n3ds_texture_descriptor_rgba8_face_bytes(
    unsigned int width,unsigned int height,unsigned int faces)
{
    if(width<8 || height<8 || width>512 || height>512 ||
       (width&(width-1)) || (height&(height-1)) || (faces!=1 && faces!=6) ||
       (faces==6 && (width!=height || width>64))) return 0;
    /* The dimension bounds make both this product and six face ranges fit. */
    return width*height*4;
}
/* Native 2D mip levels are consecutive tiled images down to an 8x8 minimum. */
static inline unsigned int n3ds_texture_mip_bytes(
    unsigned int width,unsigned int height,unsigned int levels,unsigned int format)
{
    if(!n3ds_texture_descriptor_rgba8_face_bytes(width,height,1) || !levels || levels>7 ||
       (width>>(levels-1))<8 || (height>>(levels-1))<8) return 0;
    unsigned int stride;
    if(format==NATIVE_TEXTURE_FORMAT_RGBA8) stride=4;
    else if(format==NATIVE_TEXTURE_FORMAT_RGB8) stride=3;
    else return 0;
    unsigned int bytes=0;
    for(unsigned int level=0;level<levels;++level) bytes+=(width>>level)*(height>>level)*stride;
    return bytes;
}
static inline int n3ds_texture_descriptor_valid(const struct native_texture_descriptor *descriptor)
{
    if(!descriptor || !descriptor->levels ||
       descriptor->conversion_policy!=NATIVE_TEXTURE_CONVERSION_RGBA8 ||
       descriptor->source_mip>31 || !descriptor->source_bytes) return 0;
    unsigned int face_bytes=n3ds_texture_descriptor_rgba8_face_bytes(
        descriptor->width,descriptor->height,descriptor->faces);
    if(!face_bytes) return 0;
    if(descriptor->format==NATIVE_TEXTURE_FORMAT_RGBA8) {
        if(descriptor->cpu_requirements!=(NATIVE_TEXTURE_CPU_RGBA8|NATIVE_TEXTURE_CPU_UNKNOWN)) return 0;
        if(descriptor->levels!=1) {
            if(descriptor->faces!=1) return 0;
            face_bytes=n3ds_texture_mip_bytes(descriptor->width,descriptor->height,descriptor->levels,descriptor->format);
        }
    }
    else if(descriptor->format==NATIVE_TEXTURE_FORMAT_RGB8 && descriptor->faces==1 && !descriptor->cpu_requirements) {
        face_bytes=n3ds_texture_mip_bytes(descriptor->width,descriptor->height,descriptor->levels,descriptor->format);
    }
#ifdef HALO_N3DS_TEXTURE_COMPARE
    else if(descriptor->format==NATIVE_TEXTURE_FORMAT_LA8 && descriptor->faces==1 &&
        descriptor->levels==1 && !descriptor->cpu_requirements) face_bytes/=2;
#endif
    else return 0;
    if(!face_bytes || descriptor->resident_bytes!=face_bytes*descriptor->faces ||
       descriptor->scratch_bytes>~0u-descriptor->resident_bytes) return 0;
    for(unsigned int i=0;i<NATIVE_TEXTURE_MAX_FACES;++i) {
        unsigned int bytes=i<descriptor->faces?face_bytes:0;
        unsigned int offset=i<descriptor->faces?i*face_bytes:0;
        if(descriptor->face[i].bytes!=bytes || descriptor->face[i].offset!=offset) return 0;
    }
    return 1;
}
static inline int n3ds_texture_descriptor_rgba8(struct native_texture_descriptor *descriptor,
    unsigned int width,unsigned int height,unsigned int faces,unsigned int source_mip,
    unsigned int source_bytes,unsigned int scratch_bytes)
{
    struct native_texture_descriptor value={0};
    if(!descriptor) return 0;
    *descriptor=value;
    unsigned int face_bytes=n3ds_texture_descriptor_rgba8_face_bytes(width,height,faces);
    if(!face_bytes || source_mip>31 || !source_bytes || scratch_bytes>~0u-face_bytes*faces) return 0;
    value.format=NATIVE_TEXTURE_FORMAT_RGBA8;value.width=width;value.height=height;
    value.faces=faces;value.levels=1;value.source_mip=source_mip;
    value.conversion_policy=NATIVE_TEXTURE_CONVERSION_RGBA8;
    value.cpu_requirements=NATIVE_TEXTURE_CPU_RGBA8|NATIVE_TEXTURE_CPU_UNKNOWN;
    value.source_bytes=source_bytes;value.resident_bytes=face_bytes*faces;value.scratch_bytes=scratch_bytes;
    for(unsigned int i=0;i<faces;++i) {value.face[i].offset=i*face_bytes;value.face[i].bytes=face_bytes;}
    *descriptor=value;
    return 1;
}

/* Start with the normal one-level 2D plan for the chosen base size. Only an
 * approved opaque, GPU-only source may select RGB8. The caller fills every
 * authored level in the returned span before finalization/publication. */
static inline int n3ds_texture_descriptor_mip_chain(struct native_texture_descriptor *descriptor,
    unsigned int format,unsigned int levels)
{
    if(!n3ds_texture_descriptor_valid(descriptor) || descriptor->format!=NATIVE_TEXTURE_FORMAT_RGBA8 ||
       descriptor->faces!=1 || descriptor->levels!=1) return 0;
    unsigned int bytes=n3ds_texture_mip_bytes(descriptor->width,descriptor->height,levels,format);
    if(!bytes || descriptor->scratch_bytes>~0u-bytes) return 0;
    descriptor->format=format;
    descriptor->levels=levels;
    if(format==NATIVE_TEXTURE_FORMAT_RGB8) descriptor->cpu_requirements=0;
    descriptor->resident_bytes=descriptor->face[0].bytes=bytes;
    return 1;
}

#ifdef HALO_N3DS_TEXTURE_COMPARE
static inline int n3ds_texture_descriptor_compact(struct native_texture_descriptor *descriptor,unsigned int format)
{
    if(format==NATIVE_TEXTURE_FORMAT_RGB8) return n3ds_texture_descriptor_mip_chain(descriptor,format,1);
    if(!n3ds_texture_descriptor_valid(descriptor) || descriptor->format!=NATIVE_TEXTURE_FORMAT_RGBA8 ||
       descriptor->faces!=1 || descriptor->levels!=1 || format!=NATIVE_TEXTURE_FORMAT_LA8) return 0;
    descriptor->format=format;
    descriptor->cpu_requirements=0;
    descriptor->resident_bytes=descriptor->face[0].bytes=descriptor->resident_bytes/2;
    return 1;
}
#endif

#if HALO_N3DS_TEXTURE_LOD_FIXTURE
static inline unsigned int n3ds_texture_rgb8_mip_bytes(
    unsigned int width,unsigned int height,unsigned int levels)
{
    return n3ds_texture_mip_bytes(width,height,levels,NATIVE_TEXTURE_FORMAT_RGB8);
}
static inline int n3ds_texture_descriptor_rgb8_mips(struct native_texture_descriptor *descriptor,
    unsigned int width,unsigned int height,unsigned int levels,unsigned int source_mip,
    unsigned int source_bytes,unsigned int scratch_bytes)
{
    if(!descriptor) return 0;
    unsigned int bytes=n3ds_texture_rgb8_mip_bytes(width,height,levels);
    if(!bytes || source_mip>31 || !source_bytes || scratch_bytes>~0u-bytes) return 0;
    *descriptor=(struct native_texture_descriptor){
        .format=NATIVE_TEXTURE_FORMAT_RGB8,.width=width,.height=height,.faces=1,.levels=levels,
        .source_mip=source_mip,.conversion_policy=NATIVE_TEXTURE_CONVERSION_RGBA8,
        .source_bytes=source_bytes,.resident_bytes=bytes,.scratch_bytes=scratch_bytes,
        .face={{0,bytes}}
    };
    return 1;
}
#endif

#endif
