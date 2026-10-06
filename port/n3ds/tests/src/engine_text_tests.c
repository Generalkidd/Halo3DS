/* Actual map font data through the original glyph cache and draw entry point.
 * Caption deliberately identifies the static fixture rather than gameplay. */
#include "cseries.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "text/font_group.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_text.h"
#include "rasterizer/rasterizer_debug_options.h"
#include "bitmaps/bitmap_group.h"
#include "engine_text.h"
#include "engine_cache.h"
#include "cache_reader.h"
struct font_character {
    unsigned short character; short character_width,bitmap_width,bitmap_height;
    short bitmap_origin_x,bitmap_origin_y,hardware_character_index,pad;
    long pixels_offset;
};
_Static_assert(sizeof(struct font_character)==20,"Original glyph ABI");
static struct font_header *font;
static struct font_character *glyphs[64];
static short glyph_x[64],glyph_y[64];
static unsigned int glyph_count;
static const char caption[]="HALO CE - STATIC SCENE";
void n3ds_log(const char *message);
static struct font_character *find_character(struct font_header *candidate,unsigned int code)
{
    struct font_character *characters=candidate->characters.address;
    for(long i=0;i<candidate->characters.count;++i) if(characters[i].character==code) return &characters[i];
    return NULL;
}
int halo_engine_text_fixture_prepare(void)
{
    const struct n3ds_cache_view *view=n3ds_engine_cache_view(0);
    struct tag_iterator iterator;
    long handle,selected=NONE;
    font=NULL; glyph_count=0;
    tag_iterator_new(&iterator,'font');
    while((handle=tag_iterator_next(&iterator))!=NONE) {
        struct font_header *candidate=tag_get('font',handle);
        if(strncmp(tag_get_name(handle),"ui\\",3)) continue;
        if(candidate->ascending_height<8 || candidate->ascending_height>20 ||
           candidate->characters.count<=0 || candidate->characters.count>65536 ||
           !n3ds_cache_resolve(view,candidate->characters.address,candidate->characters.count*20) ||
           candidate->pixels.size<=0 || !n3ds_cache_resolve(view,candidate->pixels.address,candidate->pixels.size)) continue;
        int valid=1,width=8;
        for(unsigned int i=0;caption[i];++i) {
            struct font_character *c=find_character(candidate,caption[i]);
            if(!c || c->character_width<=0 ||
               c->bitmap_width>40 || c->bitmap_height>40 || c->pixels_offset<0 ||
               (c->bitmap_width>0 && c->bitmap_height>0 &&
                c->pixels_offset>candidate->pixels.size-c->bitmap_width*c->bitmap_height) ||
               c->hardware_character_index!=NONE) { valid=0; break; }
            width+=c->character_width;
        }
        if(valid && width<392 && (!font || candidate->ascending_height<font->ascending_height)) {
            font=candidate; selected=handle;
        }
    }
    if(!font) { n3ds_log("TEXT FIXTURE FAIL: no supported map font"); return 1; }
    int cursor=8,baseline=8+font->ascending_height;
    unsigned char *coverage=calloc(400*240,1); if(!coverage) return 1;
    for(unsigned int i=0;caption[i];++i) {
        struct font_character *c=find_character(font,caption[i]);
        int x=cursor-c->bitmap_origin_x,y=baseline-c->bitmap_origin_y;
        cursor+=c->character_width;
        if(c->bitmap_width<=0 || c->bitmap_height<=0) continue;
        if(x<0 || y<0 || x+c->bitmap_width>400 || y+c->bitmap_height>240 || glyph_count>=64) { free(coverage); return 1; }
        glyphs[glyph_count]=c; glyph_x[glyph_count]=x; glyph_y[glyph_count]=y; ++glyph_count;
        const unsigned char *pixels=(const unsigned char *)font->pixels.address+c->pixels_offset;
        for(int row=0;row<c->bitmap_height;++row) for(int col=0;col<c->bitmap_width;++col) {
            unsigned int alpha=(pixels[row*c->bitmap_width+col]>>4)*17;
            unsigned char *previous=coverage+(y+row)*400+x+col;
            *previous=alpha+((*previous)*(255-alpha)+127)/255;
        }
    }
    FILE *proof=fopen("sdmc:/halo-source/halo-native-font-coverage.bin","wb");
    int good=proof && fwrite(coverage,400*240,1,proof)==1;
    if(proof && fclose(proof)) good=0;
    free(coverage); if(!good) { n3ds_log("TEXT FIXTURE FAIL: could not save font coverage reference"); return 1; }
    char message[192];
    snprintf(message,sizeof(message),"Text fixture: original map font %s, %u glyphs, caption %s",tag_get_name(selected),glyph_count,caption);
    n3ds_log(message);
    return 0;
}
void halo_engine_text_fixture_draw(void)
{
    struct rasterizer_dynamic_screen_geometry_parameters p={0};
    struct bitmap_data *atlas=hardware_character_cache_get_bitmap();
    assert(font && glyph_count && atlas);
    p.map[0]=atlas; p.map_texture_scale[0].i=1.f/atlas->width; p.map_texture_scale[0].j=1.f/atlas->height;
    p.map_scale[0].i=p.map_scale[0].j=1; p.point_sampled=TRUE;
    p.framebuffer_blend_function=0;
    rasterizer_debug_options.dynamic_screen_geometry=TRUE;
    rasterizer_text_begin(&p);
    for(unsigned int i=0;i<glyph_count;++i) {
        struct font_character *c=glyphs[i];
        rasterizer_draw_character(NULL,font,c,0xffffffffU,glyph_x[i],glyph_y[i],0,0,c->bitmap_width,c->bitmap_height);
    }
    rasterizer_text_end();
}
