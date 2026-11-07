#ifndef HALO_N3DS_ENGINE_TEXTURE_DESCRIPTOR_H
#define HALO_N3DS_ENGINE_TEXTURE_DESCRIPTOR_H

/* Engine-owned IDs: never serialize a GPU_TEXCOLOR or a native pointer.
 * This stage supports only the existing, single-level RGBA8 representation.
 * Unknown CPU consumers conservatively require those same readable bytes. */
enum {
    NATIVE_TEXTURE_FORMAT_RGBA8=1,
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
    /* Canonical ranges in a logical, contiguous resident payload. Actual
     * cube images have separate GPU allocations, returned as spans below. */
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
static inline int n3ds_texture_descriptor_valid(const struct native_texture_descriptor *descriptor)
{
    if(!descriptor || descriptor->format!=NATIVE_TEXTURE_FORMAT_RGBA8 || descriptor->levels!=1 ||
       descriptor->conversion_policy!=NATIVE_TEXTURE_CONVERSION_RGBA8 ||
       descriptor->cpu_requirements!=(NATIVE_TEXTURE_CPU_RGBA8|NATIVE_TEXTURE_CPU_UNKNOWN) ||
       descriptor->source_mip>31 || !descriptor->source_bytes) return 0;
    unsigned int face_bytes=n3ds_texture_descriptor_rgba8_face_bytes(
        descriptor->width,descriptor->height,descriptor->faces);
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

#endif
