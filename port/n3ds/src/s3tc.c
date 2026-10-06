/* Reused from the preserved 3DS project: a CPU-only DXT decoder. */
#include "s3tc.h"
#include <string.h>
static uint32_t read16(const unsigned char *p)
{ return p[0]|(uint32_t)p[1]<<8; }
int halo_s3tc_decode(uint32_t format,const void *source,size_t source_bytes,uint32_t width,uint32_t height,
                    void *output,size_t output_bytes,uint32_t output_pitch)
{
    if(format!=0x0c && format!=0x0e && format!=0x0f) return -1;
    if(!source || !output || !width || !height || width>1024 || height>1024 || output_pitch<width*4) return -1;
    uint32_t bx=(width+3)/4,by=(height+3)/4,block_bytes=format==0x0c?8:16;
    uint64_t input_span=(uint64_t)bx*by*block_bytes,output_span=(uint64_t)(height-1)*output_pitch+width*4;
    if(input_span>source_bytes || output_span>output_bytes) return -1;
    uintptr_t a=(uintptr_t)source,b=(uintptr_t)output;
    if((a<=b && b-a<input_span) || (b<a && a-b<output_span)) return -1;
    const unsigned char *src=source;unsigned char *dst=output;
    for(uint32_t yb=0;yb<by;yb++) for(uint32_t xb=0;xb<bx;xb++) {
        const unsigned char *alpha=src+((size_t)yb*bx+xb)*block_bytes;
        const unsigned char *block=alpha+(format==0x0c?0:8);
        uint32_t first=read16(block),second=read16(block+2);
        int four_colors=format!=0x0c || first>second;
        unsigned char colors[4][4]={{0}};
        uint32_t values[2]={first,second};
        for(unsigned i=0;i<2;i++) {
            colors[i][0]=(unsigned char)(((values[i]>>11)&31)*255/31);
            colors[i][1]=(unsigned char)(((values[i]>>5)&63)*255/63);
            colors[i][2]=(unsigned char)((values[i]&31)*255/31);colors[i][3]=255;
        }
        colors[2][3]=255;
        for(unsigned c=0;c<3;c++) {
            colors[2][c]=(unsigned char)(four_colors?(2*colors[0][c]+colors[1][c])/3:(colors[0][c]+colors[1][c])/2);
            colors[3][c]=(unsigned char)(four_colors?(colors[0][c]+2*colors[1][c])/3:0);
        }
        colors[3][3]=four_colors?255:0;
        uint32_t indices=read16(block+4)|(read16(block+6)<<16);
        unsigned char alpha_values[8]={0};uint64_t alpha_indices=0;
        if(format==0x0f) {
            alpha_values[0]=alpha[0];alpha_values[1]=alpha[1];
            unsigned denominator=alpha[0]>alpha[1]?7:5;
            for(unsigned i=1;i<denominator;i++)
                alpha_values[i+1]=(unsigned char)(((denominator-i)*alpha[0]+i*alpha[1])/denominator);
            if(denominator==5) { alpha_values[6]=0;alpha_values[7]=255; }
            for(unsigned i=0;i<6;i++) alpha_indices|=(uint64_t)alpha[i+2]<<(8*i);
        }
        for(uint32_t y=0;y<4 && yb*4+y<height;y++) for(uint32_t x=0;x<4 && xb*4+x<width;x++) {
            unsigned pixel=y*4+x,index=(indices>>(2*pixel))&3;
            unsigned char *out=dst+(size_t)(yb*4+y)*output_pitch+(xb*4+x)*4;
            memcpy(out,colors[index],4);
            if(format==0x0e) out[3]=(unsigned char)(((alpha[pixel/2]>>(4*(pixel&1)))&15)*17);
            if(format==0x0f) out[3]=alpha_values[(alpha_indices>>(3*pixel))&7];
        }
    }
    return 0;
}
int halo_dxt1_decode(const void *source,size_t source_bytes,uint32_t width,uint32_t height,
                    void *output,size_t output_bytes,uint32_t output_pitch)
{ return halo_s3tc_decode(0x0c,source,source_bytes,width,height,output,output_bytes,output_pitch); }

