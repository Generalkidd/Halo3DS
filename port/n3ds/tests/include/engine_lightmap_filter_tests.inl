#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_lightmap_filter_tests(void)
{
    byte source[32*16*2];unsigned int out[16*16];unsigned short distance[16*16];
    for(unsigned int y=0;y<16;++y) for(unsigned int x=0;x<32;++x) {
        long sw[2];bitmap_swizzle_vector2d(32,16,x,y,sw);unsigned int at=(sw[0]|sw[1])*2;
        unsigned int v=((x^y)&1)?0xf800:0xffe0;
        if(x<8 && y<8) v=0x7bef;
        source[at]=v;source[at+1]=v>>8;
    }
    if(!lightmap_reduce(source,sizeof(source),32,16,8,8,out,distance)) return 0;
    for(unsigned int i=0;i<64;++i) if(out[i]!=0x7b7d7bffu) return 0;
    /* Pure authored red/yellow illumination has no checker pattern. */
    for(unsigned int c=0;c<2;++c) {
        unsigned int v=c?0xffe0:0xf800;
        for(unsigned int i=0;i<sizeof(source);i+=2) {source[i]=v;source[i+1]=v>>8;}
        if(!lightmap_reduce(source,sizeof(source),32,16,8,8,out,distance)) return 0;
        for(unsigned int i=0;i<64;++i) if(out[i]!=(c?0xffff00ffu:0xff0000ffu)) return 0;
    }
    /* Empty padding stays black and cannot overflow the propagation distance. */
    for(unsigned int y=0;y<16;++y) for(unsigned int x=0;x<32;++x) {
        long sw[2];bitmap_swizzle_vector2d(32,16,x,y,sw);unsigned int at=(sw[0]|sw[1])*2;
        unsigned int v=((x^y)&1)?0xf800:0xffe0;source[at]=v;source[at+1]=v>>8;
    }
    if(!lightmap_reduce(source,sizeof(source),32,16,8,8,out,distance)) return 0;
    for(unsigned int i=0;i<64;++i) if(out[i]!=255) return 0;
    if(lightmap_reduce(source,10,32,16,8,8,out,distance) || lightmap_reduce(source,sizeof(source),31,16,8,8,out,distance)) return 0;
    n3ds_log("PASS: lightmap filtering: rectangular Xbox swizzle, checker exclusion, nearest lighting extension, authored red/yellow preservation, empty and invalid input");return 1;
}
#endif
