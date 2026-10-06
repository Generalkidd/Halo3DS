/* SHA-1 core and checksum prefix adapted from port/linux/src/xbox_xapi.c.
 * This is the desktop port's integrity format, not an original Xbox signature.
 * The fixed public prefix is not an authentication secret. */
#include <3ds.h>
#include <stdint.h>
#include <string.h>
#include "engine_signatures.h"
struct sha1_context
{
	uint32_t state[5];
	uint64_t length;
	unsigned char buffer[64];
	uint32_t buffered;
};

#define SHA1_ROTATE(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void sha1_block(struct sha1_context *context, const unsigned char *block)
{
	uint32_t words[80];
	uint32_t a, b, c, d, e;
	int index;

	for (index = 0; index < 16; index++)
	{
		words[index] = ((uint32_t)block[index * 4] << 24) | ((uint32_t)block[index * 4 + 1] << 16) |
			((uint32_t)block[index * 4 + 2] << 8) | block[index * 4 + 3];
	}
	for (; index < 80; index++)
		words[index] = SHA1_ROTATE(words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);

	a = context->state[0];
	b = context->state[1];
	c = context->state[2];
	d = context->state[3];
	e = context->state[4];
	for (index = 0; index < 80; index++)
	{
		uint32_t f, k, temporary;

		if (index < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
		else if (index < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
		else if (index < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
		else { f = b ^ c ^ d; k = 0xca62c1d6; }
		temporary = SHA1_ROTATE(a, 5) + f + e + k + words[index];
		e = d;
		d = c;
		c = SHA1_ROTATE(b, 30);
		b = a;
		a = temporary;
	}
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
}

static void sha1_initialize(struct sha1_context *context)
{
	memset(context, 0, sizeof(*context));
	context->state[0] = 0x67452301;
	context->state[1] = 0xefcdab89;
	context->state[2] = 0x98badcfe;
	context->state[3] = 0x10325476;
	context->state[4] = 0xc3d2e1f0;
}

static void sha1_update(struct sha1_context *context, const unsigned char *data, uint32_t size)
{
	context->length += size;
	while (size)
	{
		uint32_t count = 64 - context->buffered;

		if (count > size)
			count = size;
		memcpy(context->buffer + context->buffered, data, count);
		context->buffered += count;
		data += count;
		size -= count;
		if (context->buffered == 64)
		{
			sha1_block(context, context->buffer);
			context->buffered = 0;
		}
	}
}

static void sha1_finish(struct sha1_context *context, unsigned char digest[20])
{
	uint64_t bits = context->length * 8;
	unsigned char padding = 0x80;
	unsigned char length_bytes[8];
	int index;

	sha1_update(context, &padding, 1);
	padding = 0;
	while (context->buffered != 56)
		sha1_update(context, &padding, 1);
	for (index = 0; index < 8; index++)
		length_bytes[index] = (unsigned char)(bits >> (56 - index * 8));
	sha1_update(context, length_bytes, 8);
	for (index = 0; index < 20; index++)
		digest[index] = (unsigned char)(context->state[index / 4] >> (24 - (index % 4) * 8));
}

static const unsigned char signature_title_key[] = "halo-linux content signature";


enum { SLOTS=16, MAX_GENERATION=0x00ffffff, INVALID_HANDLE=6, INVALID_PARAMETER=87 };
struct signature_slot { struct sha1_context context; unsigned int generation; int live; };
static struct signature_slot slots[SLOTS];
static LightLock signature_lock;
static struct signature_slot *lookup(unsigned int token)
{
    struct signature_slot *slot=&slots[token&15];
    return (token&0xf0000000U)==0x60000000U && slot->live &&
        slot->generation==((token&0x0fffffffU)>>4)?slot:NULL;
}
unsigned int n3ds_signature_begin(unsigned int flags)
{
    unsigned int token=0;
    if(flags) return 0; /* Original Halo requests only portable flag zero. */
    LightLock_Lock(&signature_lock);
    for(unsigned int i=0;i<SLOTS;++i) if(!slots[i].live && slots[i].generation<MAX_GENERATION) {
        struct signature_slot *slot=&slots[i];
        sha1_initialize(&slot->context);
        sha1_update(&slot->context,signature_title_key,sizeof(signature_title_key));
        ++slot->generation; slot->live=1;
        token=0x60000000U|(slot->generation<<4)|i; break;
    }
    LightLock_Unlock(&signature_lock); return token;
}
unsigned int n3ds_signature_update(unsigned int token,const unsigned char *data,unsigned int size)
{
    unsigned int result=INVALID_HANDLE;
    LightLock_Lock(&signature_lock); struct signature_slot *slot=lookup(token);
    if(slot) {
        if((!data && size) || slot->context.length>UINT64_MAX/8-size) result=INVALID_PARAMETER;
        else { sha1_update(&slot->context,data,size); result=0; }
    }
    LightLock_Unlock(&signature_lock); return result;
}
unsigned int n3ds_signature_end(unsigned int token,unsigned char *digest)
{
    unsigned int result=INVALID_HANDLE;
    LightLock_Lock(&signature_lock); struct signature_slot *slot=lookup(token);
    if(slot) {
        if(digest) sha1_finish(&slot->context,digest);
        memset(&slot->context,0,sizeof(slot->context)); slot->live=0; result=0;
    }
    LightLock_Unlock(&signature_lock); return result;
}
unsigned int n3ds_signature_count(void)
{
    unsigned int count=0; LightLock_Lock(&signature_lock);
    for(unsigned int i=0;i<SLOTS;++i) count+=slots[i].live!=0;
    LightLock_Unlock(&signature_lock); return count;
}
