#ifndef HALO_N3DS_ENGINE_SIGNATURES_H
#define HALO_N3DS_ENGINE_SIGNATURES_H
unsigned int n3ds_signature_begin(unsigned int flags);
unsigned int n3ds_signature_update(unsigned int handle,const unsigned char *data,unsigned int size);
unsigned int n3ds_signature_end(unsigned int handle,unsigned char *digest);
unsigned int n3ds_signature_count(void);
int halo_engine_signature_tests(void);
int halo_engine_profile_tests(void);
#endif
