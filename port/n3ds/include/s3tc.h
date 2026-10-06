#pragma once
#include <stdint.h>
#include <stddef.h>
/* Decode one linear DXT1 mip level to RGBA bytes. No allocation or ownership
 * changes. Reject overlapping buffers and invalid spans before any write.
 * RGB565 endpoints use floor(component*255/max), then integer interpolation;
 * this is the xemu reference precision policy, not measured Xbox precision. */
int halo_dxt1_decode(const void *source,size_t source_bytes,uint32_t width,uint32_t height,
                    void *output,size_t output_bytes,uint32_t output_pitch);
/* Xbox formats 0x0c=DXT1, 0x0e=DXT3, 0x0f=DXT5. */
int halo_s3tc_decode(uint32_t format,const void *source,size_t source_bytes,
                    uint32_t width,uint32_t height,void *output,
                    size_t output_bytes,uint32_t output_pitch);
