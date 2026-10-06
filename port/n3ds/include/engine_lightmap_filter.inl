/* Lightmap atlases contain red/yellow checkerboard in unused space. Never
 * average those sentinel texels into surface lighting when reducing an atlas.
 * Filter at upload time only, with unchanged GPU dimensions and draw passes. */
static unsigned int lightmap_tile(unsigned int x,unsigned int y,unsigned int w,unsigned int h)
{
    static const unsigned int spread[8]={0,1,4,5,16,17,20,21};
    y=h-1-y;return ((y/8)*(w/8)+x/8)*64+spread[x&7]+2*spread[y&7];
}
static unsigned int lightmap_word(const byte *p,unsigned int index)
{return p[index*2]|(unsigned int)p[index*2+1]<<8;}
static int lightmap_padding(const byte *p,unsigned int at)
{
    unsigned int v=lightmap_word(p,at);
    /* Adjacent X and Y bits are the low two bits of Xbox's swizzle.
     * A solid authored red/yellow light is not this alternating pattern. */
    return (v==0xf800 || v==0xffe0) && lightmap_word(p,at^1)==(v^0x7e0) && lightmap_word(p,at^2)==(v^0x7e0);
}
static int lightmap_reduce(const byte *source,unsigned int bytes,unsigned int w,unsigned int h,
    unsigned int tw,unsigned int th,unsigned int *out,unsigned short *distance)
{
    unsigned int xs[1024],ys[1024];
    if(!source || !out || !distance || w<4 || h<4 || w>1024 || h>1024 ||
       tw<8 || th<8 || tw>512 || th>512 || (w&(w-1)) || (h&(h-1)) ||
       (tw&(tw-1)) || (th&(th-1)) || bytes<w*h*2) return 0;
    for(unsigned int x=0;x<w;++x) {long s[2];bitmap_swizzle_vector2d(w,h,x,0,s);xs[x]=s[0];}
    for(unsigned int y=0;y<h;++y) {long s[2];bitmap_swizzle_vector2d(w,h,0,y,s);ys[y]=s[1];}
    for(unsigned int y=0;y<th;++y) for(unsigned int x=0;x<tw;++x) {
        unsigned int r=0,g=0,b=0,n=0,x0=x*w/tw,x1=MAX(x0+1,(x+1)*w/tw),y0=y*h/th,y1=MAX(y0+1,(y+1)*h/th);
        for(unsigned int sy=y0;sy<y1;++sy) for(unsigned int sx=x0;sx<x1;++sx) {
            unsigned int at=xs[sx]|ys[sy];if(lightmap_padding(source,at)) continue;
            unsigned int v=lightmap_word(source,at),a=v>>11,c=(v>>5)&63,d=v&31;
            r+=(a<<3)|(a>>2);g+=(c<<2)|(c>>4);b+=(d<<3)|(d>>2);++n;
        }
        distance[y*tw+x]=n?0:65535;
        out[lightmap_tile(x,y,tw,th)]=n?((r+n/2)/n)<<24|((g+n/2)/n)<<16|((b+n/2)/n)<<8|255:255;
    }
    /* Extend the nearest retained lighting into empty reduced cells. This
     * keeps bilinear samples at chart edges away from the debug checker. */
    for(unsigned int pass=0;pass<2;++pass) for(unsigned int i=0;i<tw*th;++i) {
        unsigned int at=pass?tw*th-1-i:i,x=at%tw,y=at/tw;
        unsigned int near[2]={pass?(x+1<tw?at+1:at):(x?at-1:at),pass?(y+1<th?at+tw:at):(y?at-tw:at)};
        for(unsigned int k=0;k<2;++k) if((unsigned int)distance[near[k]]+1<distance[at]) {
            distance[at]=distance[near[k]]+1;
            out[lightmap_tile(x,y,tw,th)]=out[lightmap_tile(near[k]%tw,near[k]/tw,tw,th)];
        }
    }
    return 1;
}
