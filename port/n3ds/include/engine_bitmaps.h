#ifndef HALO_N3DS_ENGINE_BITMAPS_H
#define HALO_N3DS_ENGINE_BITMAPS_H
struct bitmap_data;
unsigned int n3ds_bitmap_decode_pixel(const unsigned char *pixel,int format);
int n3ds_dynamic_bitmap_update(struct bitmap_data *bitmap);
void *n3ds_dynamic_bitmap_resource(const struct bitmap_data *bitmap);
int n3ds_dynamic_bitmaps_collect(void);
unsigned int n3ds_dynamic_bitmap_bytes(void);
unsigned int n3ds_dynamic_bitmap_count(void);
int halo_engine_dynamic_bitmap_tests(void);
#endif
