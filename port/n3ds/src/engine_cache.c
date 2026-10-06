#include "cseries.h"
#include "cache_reader.h"
#include "cache_build_compatibility.h"
#include "engine_cache.h"
#include "engine_textures.h"
#include "cache_files.h"
#include "crc.h"
#include "data.h"
#include "tag_groups.h"
#include "scenario_definitions.h"
#include "scenario.h"
#include "structure_bsp_definitions.h"
#include "collision_bsp_definitions.h"
#include "collision_bsp.h"

void n3ds_log(const char *message);
void n3ds_cache_progress(const char *stage,unsigned long done,unsigned long total)
{
    extern long long n3ds_engine_ticks(void),n3ds_engine_tick_frequency(void);
    extern int n3ds_input_platform_original_model(void);
    extern void n3ds_gpu_loading_pulse(float,int);
    static long long start;
    if(!done) {start=n3ds_engine_ticks();n3ds_log(stage);}
    if(total>=65536)
        n3ds_gpu_loading_pulse((float)done/total,0);
    if(done==total) {
        char message[160];snprintf(message,sizeof(message),"LOAD STAGE: %s bytes=%lu ms=%.1f",stage,total,
            (n3ds_engine_ticks()-start)*1000./n3ds_engine_tick_frequency());n3ds_log(message);
    }
}
struct cache_file_tag_instance *global_tag_instances;

struct relocation_header {
    unsigned long magic, version, bytes, xbox_base, data_crc, count, records_crc, header_crc;
};
struct relocation_record { unsigned long offset, expected, target; };

static unsigned long crc32(const void *bytes, long count)
{
    unsigned long crc;
    crc_new(&crc);
    if(count<65536) crc_checksum_buffer(&crc,bytes,count);
    else {
        n3ds_cache_progress("Checking map",0,count);
        for(long offset=0;offset<count;) {
            long chunk=MIN(count-offset,262144);
            crc_checksum_buffer(&crc,(const byte *)bytes+offset,chunk);
            offset+=chunk;n3ds_cache_progress("Checking map",offset,count);
        }
    }
    return crc ^ 0xffffffff;
}

static int apply_records(struct n3ds_cache_view *view, const struct relocation_record *records,
                         unsigned long count, const struct n3ds_cache_view *global)
{
    unsigned long i, previous = 0;
    if (view->tag_bytes < 4 || count > view->tag_bytes/4) return 1;
    for (i = 0; i < count; ++i) {
        const struct relocation_record *r = &records[i];
        if ((r->offset & 3) || r->offset > view->tag_bytes-4 ||
            (i && r->offset <= previous) ||
            *(unsigned long *)(view->tags+r->offset) != r->expected) return 1;
        if (r->target != 0xffffffff) {
            if (r->target & 0x80000000) {
                if (!global || (r->target & 0x7fffffff) >= global->tag_bytes) return 1;
            } else if (r->target >= view->tag_bytes) return 1;
        }
        previous = r->offset;
    }
    for (i = 0; i < count; ++i) {
        const struct relocation_record *r = &records[i];
        *(void **)(view->tags+r->offset) = r->target == 0xffffffff ? NULL :
            (r->target & 0x80000000) ? global->tags+(r->target & 0x7fffffff) : view->tags+r->target;
    }
    return 0;
}

int halo_engine_relocation_tests(void)
{
    /* Independent bitwise oracle, including deliberately unaligned input and
     * split updates. The private ARM CRC table has no on-disk ABI. */
    byte sample[260];unsigned long state=0x12345678;
    for(unsigned int i=0;i<sizeof(sample);++i) {state=state*1664525u+1013904223u;sample[i]=state>>24;}
    if(crc32("123456789",9)!=0xcbf43926u || crc32(sample,0)!=0) return 1;
    for(unsigned int offset=0;offset<4;++offset) for(unsigned int length=1;length<=256;++length) {
        unsigned long expected=0xffffffff,actual;
        for(unsigned int i=0;i<length;++i) {
            expected^=sample[offset+i];
            for(unsigned int bit=0;bit<8;++bit) expected=(expected>>1)^((expected&1)?0xedb88320u:0);
        }
        crc_new(&actual);crc_checksum_buffer(&actual,sample+offset,length/2);
        crc_checksum_buffer(&actual,sample+offset+length/2,length-length/2);
        if(actual!=expected) return 1;
    }
    n3ds_log("PASS: aligned CRC table: standard vector plus 1024 bitwise-oracle/unaligned/split comparisons");
    unsigned long original[4] = {0x11, 0x22, 0x33, 0x44}, words[4], i;
    struct n3ds_cache_view view = {0};
    struct relocation_record records[2];
    view.tags = (byte *)words; view.tag_bytes = sizeof(words);
    for (i = 0; i < 6; ++i) {
        memcpy(words, original, sizeof(words));
        records[0] = (struct relocation_record){0, 0x11, 4};
        records[1] = (struct relocation_record){4, 0x22, 8};
        switch (i) {
        case 0: records[1].offset = 0; break;
        case 1: records[1].offset = sizeof(words); break;
        case 2: records[1].offset = 2; break;
        case 3: records[1].expected = 0x99; break;
        case 4: records[1].target = sizeof(words); break;
        case 5: records[1].target = 0x80000000; break;
        }
        if (!apply_records(&view, records, 2, NULL) || memcmp(words, original, sizeof(words))) return 1;
    }
    records[0] = (struct relocation_record){0, 0x11, 4};
    records[1] = (struct relocation_record){4, 0x22, 0xffffffff};
    if (apply_records(&view, records, 2, NULL) || words[0] != (unsigned long)(words+1) || words[1]) return 1;
    n3ds_log("PASS: invalid relocation records rejected before any map changes");
    return 0;
}

static int relocate(struct n3ds_cache_view *view, const char *path, const struct n3ds_cache_view *global)
{
    struct relocation_header header;
    struct relocation_record *records = NULL;
    FILE *stream = fopen(path, "rb");
    unsigned long bytes;
    int result = 1;
    if (!stream) return 1;
    if (fread(&header, 1, sizeof(header), stream) != sizeof(header) ||
        header.magic != 0x314c524e || header.version != 1 ||
        header.bytes != view->tag_bytes || header.xbox_base != view->xbox_base ||
        !header.count || header.count > view->tag_bytes/4 ||
        header.data_crc != crc32(view->tags, view->tag_bytes) ||
        header.header_crc != crc32(&view->header, sizeof(view->header))) goto done;
    bytes = header.count*sizeof(*records);
    records = (malloc)(bytes);
    if (!records || !n3ds_cache_read_region(stream,records,bytes,"Reading relocations") || fgetc(stream) != EOF ||
        ferror(stream) || header.records_crc != crc32(records, bytes)) goto done;
    /* Validate every patch before changing any map bytes. Records include the
     * old word so a stale sidecar or duplicate/overlapping patch is rejected. */
    if (apply_records(view, records, header.count, global)) goto done;
    view->xbox_base = (unsigned long)view->tags;
    if (!global) view->instances = view->tag_header->tag_instances;
    result = 0;
done:
    free(records); fclose(stream);
    return result;
}

/* Original-model BSPs share one reusable region for the entire mission.
 * Small cinematic sections must not release a large hole for model/audio
 * caches to fragment before the next gameplay section needs it again. */
static struct n3ds_cache_view bsp_arena;
static int bsp_arena_attempted;
static void bsp_close(struct n3ds_cache_view *view)
{
    if(view->tags && view->tags==bsp_arena.tags) memset(view,0,sizeof(*view));
    else n3ds_cache_close(view);
}
static void bsp_arena_prepare(const struct scenario_structure_bsp_reference *references,long count)
{
    extern int n3ds_input_platform_original_model(void);
    if(bsp_arena_attempted || !n3ds_input_platform_original_model()) return;
    bsp_arena_attempted=1;
    unsigned long largest=0;
    for(long i=0;i<count;++i) {
        unsigned long bytes=references[i].file_size;
        if(bytes<24 || bytes>0x1600000) return;
        if(bytes>largest) largest=bytes;
    }
    /* Do not reserve an unusually large later section at the expense of all
     * current working space. Such maps retain the exact allocation/retry path. */
    if(largest<1024*1024 || largest>10*1024*1024) return;
    bsp_arena.tag_bytes=largest;
    bsp_arena.tags=n3ds_cache_allocate(&bsp_arena);
    char message[160];snprintf(message,sizeof(message),"BSP ARENA: capacity=%lu allocated=%d linear=%d",largest,bsp_arena.tags!=NULL,bsp_arena.tags_linear);n3ds_log(message);
}
#include "engine_bsp_read_cache.inl"
static int load_bsp(struct n3ds_cache_view *bsp, const struct n3ds_cache_view *global,
                    const struct scenario_structure_bsp_reference *ref, const char *map, const char *sidecar)
{
    FILE *stream;
    memset(bsp, 0, sizeof(*bsp));
    if (ref->file_size < 24 || ref->file_size > 0x1600000 || ref->file_offset < 2048 ||
        ref->file_offset > global->header.file_length-ref->file_size) return 1;
    bsp->header = global->header;
    bsp->tag_bytes = ref->file_size;
    bsp->xbox_base = (unsigned long)ref->base_address;
    char allocation[160];snprintf(allocation,sizeof(allocation),"BSP LOAD: bytes=%lu offset=%ld",bsp->tag_bytes,ref->file_offset);n3ds_log(allocation);extern void debug_dump_memory(void);debug_dump_memory();
    if(bsp_arena.tags && bsp->tag_bytes<=bsp_arena.tag_bytes) {
        bsp->tags=bsp_arena.tags;bsp->tags_linear=bsp_arena.tags_linear;
    } else bsp->tags = n3ds_cache_allocate(bsp);
    if(!bsp->tags) {
        extern int n3ds_input_platform_original_model(void);
        /* Optional warm copies must never prevent the mandatory live BSP. */
        if(bsp_read_bytes || bsp_pending) {bsp_reads_clear();bsp->tags=n3ds_cache_allocate(bsp);}
        if(!bsp->tags && n3ds_input_platform_original_model()) {
            extern void n3ds_model_decode_cache_flush(void),sound_cache_flush(void);
            n3ds_model_decode_cache_flush();sound_cache_flush();texture_cache_flush();
            bsp->tags=n3ds_cache_allocate(bsp);
            n3ds_log("BSP LOAD: retried allocation after reclaiming optional caches");
        }
    }
    if(!bsp->tags) {n3ds_log("BSP LOAD FAILED: allocation");debug_dump_memory();return 1;}
    bsp_prefetch_collect(ref->file_offset);
    struct bsp_read_entry *cached=bsp_read_find(ref->file_offset,bsp->tag_bytes);
    byte *copy=NULL;
    if(cached) {
        memcpy(bsp->tags,cached->data,bsp->tag_bytes);
        char message[128];snprintf(message,sizeof(message),"BSP READ CACHE: hit bytes=%lu resident=%lu hits=%lu",bsp->tag_bytes,bsp_read_bytes,bsp_read_hits);n3ds_log(message);
    } else {
        stream=fopen(map,"rb");
        if(!stream) {n3ds_log("BSP LOAD FAILED: map open");return 1;}
        int read=!fseek(stream,ref->file_offset,SEEK_SET) && n3ds_cache_read_region(stream,bsp->tags,bsp->tag_bytes,"Reading scenery");
        fclose(stream);
        if(!read) {n3ds_log("BSP LOAD FAILED: scenery read");return 1;}
        copy=bsp_read_reserve(bsp->tag_bytes);
        if(copy) memcpy(copy,bsp->tags,bsp->tag_bytes);
    }
    int result=relocate(bsp,sidecar,global);
    if(result) {free(copy);n3ds_log("BSP LOAD FAILED: relocation");}
    else bsp_read_publish(copy,ref->file_offset,bsp->tag_bytes);
    return result;
}

/* Own one tag cache and one current BSP, matching the original engine's
 * lifetime. GPU texture/sound caches are separate platform work; no Xbox
 * resource registration is attempted on these CPU-resident native buffers. */
static struct n3ds_cache_view native_tags, native_bsp;
static struct scenario_structure_bsp_reference *native_reference;
static char native_map[128], native_name[32];
/* Use the native file service's recursive lock for compound seek/read and map
 * lifetime operations; FILE remains opaque across the engine/platform ABI. */
static FILE *native_read_stream;
static unsigned int native_stream_opens,native_stream_reads,native_stream_bytes;
void n3ds_log(const char *message);
void n3ds_files_lock(void);
void n3ds_files_unlock(void);
static unsigned int native_cache_generation,native_tag_generation;
unsigned int n3ds_engine_tag_generation(void) {return native_tag_generation;}
unsigned int n3ds_engine_cache_generation(void) { return native_cache_generation; }
static void cache_generation_advance(void)
{
    extern void n3ds_collision_cache_clear(void);n3ds_collision_cache_clear();
    assert(native_cache_generation!=0xffffffffU);++native_cache_generation;
}

const struct n3ds_cache_view *n3ds_engine_cache_view(int bsp)
{
    return bsp ? &native_bsp : &native_tags;
}

static int load_tag_candidate(const char *name,struct n3ds_cache_view *candidate,char *path,char *base_name)
{
    struct cache_file_tag_instance *scenario;
    const char *base, *scan;
    char sidecar[128];
    if (!name) return 1;
    base = name;
    for (scan = name; *scan; ++scan)
        if (*scan == '/' || *scan == '\\') base = scan+1;
    if (!*base || strlen(base) >= sizeof(native_name)) return 1;
    for (scan = base; *scan; ++scan)
        if (!((*scan >= 'a' && *scan <= 'z') || (*scan >= 'A' && *scan <= 'Z') ||
              (*scan >= '0' && *scan <= '9') || *scan == '_')) return 1;
    snprintf(path, 128, "sdmc:/halo-source/%s.map", base);
    snprintf(sidecar, sizeof(sidecar), "sdmc:/halo-source/%s.nrl", base);
    if (n3ds_cache_open(candidate, path, 1) ||
        _stricmp(candidate->header.name, base) || relocate(candidate, sidecar, NULL)) goto fail;
    scenario = n3ds_cache_tag(candidate, candidate->tag_header->scenario_tag_index, 'scnr');
    if (!scenario || !n3ds_cache_resolve(candidate, scenario->base_address, sizeof(struct scenario))) goto fail;
    strcpy(base_name,base);
    return 0;
fail:
    n3ds_cache_close(candidate);
    return 1;
}

/* Listing must not deserialize every level while a menu is opening. Check
 * the small header pair and extents here; actual selection/loading retains
 * the complete tag CRC, relocation and scenario validation below. */
boolean n3ds_engine_map_installed(const char *name)
{
    struct cache_file_header header;
    struct relocation_header relocation;
    const char *base=name,*scan;
    char path[128];FILE *file;long size;
    if(!name) return FALSE;
    for(scan=name;*scan;++scan) if(*scan=='/' || *scan=='\\') base=scan+1;
    if(!*base || strlen(base)>=32) return FALSE;
    for(scan=base;*scan;++scan) if(!((*scan>='a' && *scan<='z') || (*scan>='A' && *scan<='Z') || (*scan>='0' && *scan<='9') || *scan=='_')) return FALSE;
    snprintf(path,sizeof(path),"sdmc:/halo-source/%s.map",base);
    file=fopen(path,"rb");if(!file) return FALSE;
    int read=fread(&header,1,sizeof(header),file)==sizeof(header);
    if(fseek(file,0,SEEK_END)) read=0;
    size=ftell(file);fclose(file);
    if(!read || header.header_signature!='head' || header.footer_signature!='foot' || header.version!=5 ||
       !memchr(header.name,0,32) || !memchr(header.build,0,32) || _stricmp(header.name,base) ||
       !cache_build_is_supported(header.build,1) || header.file_length<2048 || header.file_length>0x11600000 ||
       size<header.file_length || header.tag_data_offset<2048 || header.tag_data_size<36 || header.tag_data_size>0x1600000 ||
       header.tag_data_offset>header.file_length-header.tag_data_size) return FALSE;
    snprintf(path,sizeof(path),"sdmc:/halo-source/%s.nrl",base);
    file=fopen(path,"rb");if(!file) return FALSE;
    read=fread(&relocation,1,sizeof(relocation),file)==sizeof(relocation);
    if(fseek(file,0,SEEK_END)) read=0;
    size=ftell(file);fclose(file);
    return read && relocation.magic==0x314c524e && relocation.version==1 &&
        relocation.bytes==(unsigned long)header.tag_data_size && relocation.xbox_base==0x803a6000 &&
        relocation.count && relocation.count<=relocation.bytes/4 &&
        size==(long)(sizeof(relocation)+relocation.count*sizeof(struct relocation_record)) &&
        relocation.header_crc==crc32(&header,sizeof(header));
}

boolean n3ds_engine_map_available(const char *name)
{
    /* Menu/precache queries must not read the entire tag block repeatedly.
     * Both hardware profiles validate CRCs and every relocation in
     * load_tag_candidate before publishing the map. No trust cache survives
     * selection, and malformed data still fails closed at actual load. */
    return n3ds_engine_map_installed(name);
}

long n3ds_engine_tags_load(const char *name)
{
    struct n3ds_cache_view candidate={0};
    char path[128],base[32];
    if(native_tags.tags || load_tag_candidate(name,&candidate,path,base)) return NONE;
    native_tags = candidate;
    cache_generation_advance();++native_tag_generation;
    strcpy(native_map, path);
    strcpy(native_name, base);
    assert(!native_read_stream);
    native_stream_opens=native_stream_reads=native_stream_bytes=0;
    cache_files_bind_native(&native_tags.header, native_tags.tag_header);
    return native_tags.tag_header->scenario_tag_index;
}

boolean n3ds_engine_bsp_load(struct scenario_structure_bsp_reference *reference)
{
    struct scenario *scenario;
    struct scenario_structure_bsp_reference *references;
    struct cache_file_structure_bsp_header *header;
    struct cache_file_tag_instance *instance;
    struct n3ds_cache_view candidate = {0};
    char sidecar[128];
    long index, count;
    if (!native_tags.tags || native_bsp.tags || !reference) return FALSE;
    scenario = tag_get('scnr', native_tags.tag_header->scenario_tag_index);
    count = scenario->structure_bsp_references.count;
    if (count <= 0 || count > 65535) return FALSE;
    references = n3ds_cache_resolve(&native_tags, scenario->structure_bsp_references.address,
                                   count*sizeof(*reference));
    if (!references) return FALSE;
    for (index = 0; index < count; ++index) if (&references[index] == reference) break;
    if (index == count) return FALSE;
    instance = n3ds_cache_tag(&native_tags, reference->structure_bsp.index, 'sbsp');
    if (!instance || instance->base_address) return FALSE;
    bsp_arena_prepare(references,count);
    snprintf(sidecar, sizeof(sidecar), "sdmc:/halo-source/%s-bsp%ld.nrl", native_name, index);
    if (load_bsp(&candidate, &native_tags, reference, native_map, sidecar)) goto fail;
    header = (struct cache_file_structure_bsp_header *)candidate.tags;
    if (header->signature != 'sbsp' ||
        !n3ds_cache_resolve(&candidate, header->base_address, sizeof(struct structure_bsp))) goto fail;
    native_bsp = candidate;
    cache_generation_advance();
    native_reference = reference;
    cache_files_bind_native_bsp(header, reference->structure_bsp.index);
    bsp_prefetch_next(references,count,index,native_map);
    return TRUE;
fail:
    bsp_close(&candidate);
    return FALSE;
}

void n3ds_engine_bsp_unload(struct scenario_structure_bsp_reference *reference)
{
    extern void n3ds_engine_geometry_flush(void);
    assert(reference && reference == native_reference && native_bsp.tags);
    n3ds_engine_geometry_flush();
    extern void n3ds_engine_texture_bsp_transition(void);n3ds_engine_texture_bsp_transition();
    cache_files_bind_native_bsp(NULL, reference->structure_bsp.index);
    native_reference = NULL;
    cache_generation_advance();
    bsp_close(&native_bsp);
}

/* Foundation/cache-only probes have no mission batch owner. */
__attribute__((weak)) void n3ds_engine_mission_batches_release(void) {}
void n3ds_engine_tags_unload(void)
{
    extern void n3ds_model_decode_cache_flush(void);
    extern void rasterizer_text_cache_flush(void);
    extern void n3ds_engine_sounds_unload(void);
    /* Glyph-cache entries retain pointers into these map-owned font records. */
    rasterizer_text_cache_flush();
    n3ds_engine_sounds_unload();
    n3ds_model_decode_cache_flush();
    extern void n3ds_engine_mission_batches_release(void);
    n3ds_engine_mission_batches_release();
    texture_cache_flush();
    if (native_reference) n3ds_engine_bsp_unload(native_reference);
    n3ds_cache_close(&bsp_arena);bsp_arena_attempted=0;bsp_reads_clear();
    extern void n3ds_gpu_model_cache_release(void);
    n3ds_gpu_model_cache_release();
    n3ds_files_lock();
    if(native_read_stream) {
        int closed=!fclose(native_read_stream);native_read_stream=NULL;assert(closed);
        char message[160];snprintf(message,sizeof(message),"MAP READ STREAM: opens=%u reads=%u bytes=%u closed=%d",native_stream_opens,native_stream_reads,native_stream_bytes,closed);n3ds_log(message);
    }
    if (native_tags.tags) {++native_tag_generation;cache_generation_advance();cache_files_bind_native(NULL, NULL);}
    n3ds_cache_close(&native_tags);
    native_map[0] = native_name[0] = 0;
    n3ds_files_unlock();
    /* Original models need a contiguous region for the next campaign cache.
     * Release GPU scratch images allocated during the old map, after its last
     * frame and before the next allocation. Do not reserve unused map memory
     * at the expense of the current level's geometry. */
    extern int n3ds_input_platform_original_model(void),n3ds_gpu_bottom_owned(void);
    if(n3ds_input_platform_original_model() && n3ds_gpu_bottom_owned()) {
        extern void n3ds_gpu_map_scratch_release(void);
        extern struct bitmap_data *hardware_character_cache_get_bitmap(void);
        extern void rasterizer_bitmap_delete(struct bitmap_data *);
        extern boolean rasterizer_bitmap_new(struct bitmap_data *);
        extern int n3ds_dynamic_bitmaps_collect(void);
        /* Glyph pointers were flushed above. Preserve the CPU atlas allocated
         * at startup so later map buffers can reuse contiguous heap space. */
        struct bitmap_data *atlas=hardware_character_cache_get_bitmap();
        assert(atlas);rasterizer_bitmap_delete(atlas);
        assert(n3ds_dynamic_bitmaps_collect());
        n3ds_gpu_map_scratch_release();
        assert(rasterizer_bitmap_new(atlas));
        n3ds_log("MAP MEMORY: original-model font/HUD/material scratch reclaimed between maps");
    }
}

boolean n3ds_engine_cache_read(unsigned long offset, unsigned long bytes, void *destination)
{
    boolean result=FALSE;
    n3ds_files_lock();
    if (!native_tags.tags || !destination || !bytes || offset < 2048 ||
        offset > (unsigned long)native_tags.header.file_length ||
        bytes > (unsigned long)native_tags.header.file_length-offset) goto done;
    if(!native_read_stream) {
        native_read_stream=fopen(native_map,"rb");
        if(!native_read_stream) goto done;
        ++native_stream_opens;
    }
    result = !fseek(native_read_stream, offset, SEEK_SET) && fread(destination, 1, bytes, native_read_stream) == bytes;
    if(result) {++native_stream_reads;native_stream_bytes+=bytes;}
    else {fclose(native_read_stream);native_read_stream=NULL;}
done:
    n3ds_files_unlock();
    return result;
}

int n3ds_engine_cache_stream_live(void)
{n3ds_files_lock();int live=native_read_stream!=NULL;n3ds_files_unlock();return live;}

int n3ds_engine_cache_stream_tests(void)
{
    unsigned char expected[64],actual[64];
    if(!native_tags.tags || native_tags.header.file_length<8192) return 1;
    unsigned int offsets[4]={2048,4090,2055,(unsigned int)native_tags.header.file_length-64};
    FILE *reference=fopen(native_map,"rb");if(!reference) return 1;
    int failed=0;
    for(unsigned int i=0;i<4;++i) {
        if(fseek(reference,offsets[i],SEEK_SET) || fread(expected,1,64,reference)!=64 ||
           !n3ds_engine_cache_read(offsets[i],64,actual) || memcmp(expected,actual,64)) {failed=1;break;}
    }
    fclose(reference);
    unsigned int opens=native_stream_opens,reads=native_stream_reads;
    if(n3ds_engine_cache_read(0,1,actual) || n3ds_engine_cache_read(2048,0,actual) ||
       n3ds_engine_cache_read(2048,1,NULL) || n3ds_engine_cache_read(0xffffffffU,1,actual) ||
       n3ds_engine_cache_read(native_tags.header.file_length-1,2,actual) ||
       opens!=native_stream_opens || reads!=native_stream_reads || opens!=1 || !n3ds_engine_cache_stream_live()) failed=1;
    if(!failed) n3ds_log("PASS: persistent map stream: 4 independent seek/read comparisons including EOF, 5 rejected ranges, one shared read-only handle");
    return failed;
}
