/*
CRC.C

symbols in this file:
001088D0 0010:
	_crc_new (0000)
001088E0 0040:
	_build_crc_table (0000)
00108920 0080:
	_crc_checksum_buffer (0000)
0027D2E4 000f:
	??_C@_0P@JPGOHOCM@buffer_size?$DO?$DN0?$AA@ (0000)
0027D2F4 001c:
	??_C@_0BM@FPJPBIIF@c?3?2halo?2SOURCE?2memory?2crc?4c?$AA@ (0000)
00456220 0401:
	_crc_globals (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "memory/crc.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

#ifndef HALO_N3DS
#pragma pack(push, 1)
#endif
struct crc_globals
{
	unsigned long table[256];
	boolean initialized;
};
#ifndef HALO_N3DS
#pragma pack(pop)
#endif
/* Private CPU state, never serialized. Native ARM needs an aligned table:
 * packed storage previously required four byte loads for every table lookup. */

/* ---------- prototypes */

/* ---------- globals */

#ifndef HALO_ANDROID /* Mach-O section names differ; the default is .bss anyway */
#pragma bss_seg(".bss")
#endif
struct crc_globals crc_globals;
#ifdef HALO_N3DS
static unsigned long native_crc_slices[3][256];
#endif
#ifndef HALO_ANDROID
#pragma bss_seg()
#endif

/* ---------- public code */

void crc_new(
	unsigned long *crc_reference)
{
	*crc_reference = 0xFFFFFFFF;
	return;
}

/* The descriptive private name follows the recovered cross-build CRC implementation. */
static void build_crc_table(
	unsigned long *crc_table)
{
	unsigned long byte_index;
	long byte_count;

	byte_index = 0;
	byte_count = 256;
	do
	{
		unsigned long crc = byte_index;
		long bit_count;

		bit_count = 8;
		do
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		} while (--bit_count);

		*crc_table = crc;
		byte_index++;
		crc_table++;
	} while (--byte_count);

	return;
}

void crc_checksum_buffer(
	unsigned long *crc_reference,
	void const *buffer,
	long buffer_size)
{
	unsigned long crc;
	unsigned long table_index;

	match_assert("c:\\halo\\SOURCE\\memory\\crc.c", 42, buffer_size>=0);

	if (!crc_globals.initialized)
	{
		build_crc_table(crc_globals.table);
#ifdef HALO_N3DS
        for(unsigned int n=0;n<256;++n) {
            unsigned long c=crc_globals.table[n];
            for(unsigned int k=0;k<3;++k) {c=(c>>8)^crc_globals.table[c&255];native_crc_slices[k][n]=c;}
        }
#endif
		crc_globals.initialized = TRUE;
	}

	crc = *crc_reference;
#ifdef HALO_N3DS
    /* Preserve the exact Xbox polynomial/seed. Aligned four-byte slices
     * shorten full checkpoint/map verification without approximate hashes. */
    const byte *p=buffer;
    while(buffer_size && ((unsigned long)p&3)) {crc=(crc>>8)^crc_globals.table[(crc^*p++)&255];--buffer_size;}
    while(buffer_size>=4) {
        unsigned long word=*(const unsigned long *)p, v=crc^word;
        crc=native_crc_slices[2][v&255]^native_crc_slices[1][(v>>8)&255]^native_crc_slices[0][(v>>16)&255]^crc_globals.table[v>>24];
        p+=4;buffer_size-=4;
    }
    buffer=p;
#endif
	if (buffer_size > 0)
	{
		do
		{
			table_index = (*(byte const *)buffer ^ crc) & 0xFF;
			table_index = crc_globals.table[table_index];
			crc >>= 8;
			buffer = (byte const *)buffer + 1;
			crc ^= table_index;
		} while (--buffer_size);
	}

	*crc_reference = crc;
	return;
}

/* ---------- private code */

#ifdef HALO_N3DS
int n3ds_crc_slice_tests(void)
{
    byte data[1032];unsigned int random=7;
    for(unsigned int i=0;i<sizeof(data);++i) {random=random*1664525u+1013904223u;data[i]=random>>24;}
    for(unsigned int offset=0;offset<4;++offset) for(unsigned int length=0;length<=1024;length+=length<16?1:37) {
        unsigned long expected=0xffffffffu,actual=expected,split=expected;
        for(unsigned int i=0;i<length;++i) {expected^=data[offset+i];for(int b=0;b<8;++b)expected=(expected>>1)^((expected&1)?0xedb88320u:0);}
        crc_checksum_buffer(&actual,data+offset,length);
        crc_checksum_buffer(&split,data+offset,length/3);crc_checksum_buffer(&split,data+offset+length/3,length-length/3);
        if(actual!=expected || split!=expected)return 0;
    }
    return 1;
}
#endif
