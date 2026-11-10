/* Host test: clang -std=c11 -Iport/n3ds/include
 * port/n3ds/tests/src/circle_pad_pro_tests.c -o build/cpp-tests.exe */
#define HALO_CPP_PROTOCOL_TEST
#include "../../src/engine_circle_pad_pro.c"
#include <assert.h>
#include <stdio.h>

static void pack_axes(unsigned char *p,unsigned int x,unsigned int y)
{p[0]=(unsigned char)x;p[1]=(unsigned char)((x>>8)|(y<<4));p[2]=(unsigned char)(y>>4);}
int main(void)
{
    /* Published CRC-8/SMBUS check vector, rather than a self-derived checksum. */
    assert(cpp_crc((const unsigned char *)"123456789",9)==0xf4);
    unsigned char response[69]={0x11,0,0,64,0};
    unsigned char *candidate=response+5+16;float scale=8.f;
    pack_axes(candidate+1,2048,2048);
    memcpy(candidate+4,&scale,4);memcpy(candidate+8,&scale,4);
    candidate[15]=cpp_crc(candidate,15);
    struct cpp_calibration c={0};
    assert(cpp_calibrate(response,69,0,&c));
    assert(c.x==2048 && c.y==2048 && c.sx==8.f && c.sy==8.f);
    assert(!cpp_calibrate(response,68,0,&c));
    assert(!cpp_calibrate(response,69,16,&c));
    candidate[15]^=1;assert(!cpp_calibrate(response,69,0,&c));candidate[15]^=1;
    float invalid=NAN;memcpy(candidate+4,&invalid,4);candidate[15]=cpp_crc(candidate,15);
    assert(!cpp_calibrate(response,69,0,&c));
    invalid=0;memcpy(candidate+4,&invalid,4);candidate[15]=cpp_crc(candidate,15);
    assert(!cpp_calibrate(response,69,0,&c));
    memcpy(candidate+4,&scale,4);candidate[15]=cpp_crc(candidate,15);
    assert(cpp_calibrate(response,69,0,&c));

    unsigned char input[6]={0x10,0,0,0,0xff,0};short x=1,y=1;
    pack_axes(input+1,2048,2048);assert(cpp_input(input,6,&c,&x,&y) && x==0 && y==0);
    pack_axes(input+1,2100,2000);assert(cpp_input(input,6,&c,&x,&y) && x==52 && y==-48);
    pack_axes(input+1,4095,0);assert(cpp_input(input,6,&c,&x,&y) && x==156 && y==-156);
    assert(!cpp_input(input,5,&c,&x,&y));input[0]=0x11;assert(!cpp_input(input,6,&c,&x,&y));input[0]=0x10;

    unsigned char packet[80]={0xa5,0x31,6},ring[128]={0},out[69];
    memcpy(packet+3,input,6);packet[9]=cpp_crc(packet,9);
    for(unsigned int offset=0;offset<128;++offset) {
        for(unsigned int i=0;i<10;++i)ring[(offset+i)%128]=packet[i];
        assert(cpp_packet(ring,128,offset,10,out,69)==6 && !memcmp(out,input,6));
        assert(!cpp_packet(ring,128,offset,9,out,69));
        assert(!cpp_packet(ring,128,offset,10,out,5));
        ring[(offset+9)%128]^=1;assert(!cpp_packet(ring,128,offset,10,out,69));
    }
    packet[2]=64;packet[3]=69;memcpy(packet+4,response,69);packet[73]=cpp_crc(packet,73);
    for(unsigned int i=0;i<74;++i)ring[(120+i)%128]=packet[i];
    assert(cpp_packet(ring,128,120,74,out,69)==69 && !memcmp(response,out,69));
    assert(!cpp_packet(ring,128,128,74,out,69));
    assert(!cpp_packet(ring,128,0,0xffffffffu,out,69));
    assert(!cpp_packet(ring,0,0,74,out,69));
    /* Exercise corrupt packet metadata and every small/truncated frame size. */
    for(unsigned int n=0;n<512;++n)
        for(unsigned int offset=0;offset<132;++offset) {
            int got=cpp_packet(ring,128,offset,n,out,sizeof(out));assert(got>=0 && got<=69);
        }
    puts("PASS: CPP calibration CRC/ranges, packed axes, clamping, wrapped frames, malformed lengths and truncated input");
    return 0;
}
