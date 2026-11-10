/* New3DS integration additions by Kevin Boggs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "engine_float_phase.h"
static int original;
static unsigned int generation;
/* Kevin Boggs: n3ds_engine_cache_generation */
unsigned int n3ds_engine_cache_generation(void){return generation;}
#define NONE (-1)
/* MODEL_NAME_IMPLEMENTATION */
static unsigned int queries;
/* Kevin Boggs: lookup_reference */
static short lookup_reference(long m,const char *name){++queries;return name && !strcmp(name,"head")?(short)(m+generation):-1;}
/* Kevin Boggs: n3ds_input_platform_original_model */
int n3ds_input_platform_original_model(void){return original;}
/* Kevin Boggs: n3ds_save_memory_crc */
unsigned int n3ds_save_memory_crc(const void *a,unsigned int n,const void *b,unsigned int m){
    uint32_t crc=~0u;const unsigned char *p=a;
    for(unsigned int k=0;k<2;++k){for(unsigned int i=0;i<n;++i){crc^=p[i];for(int j=0;j<8;++j)crc=(crc>>1)^((0u-(crc&1))&0xedb88320u);}p=b;n=m;}
    return crc;
}
static long test_profile;static int profile_reads,profile_available=1;
static char profile_directory[256]="u:\\one\\";
/* Kevin Boggs: player_ui_get_active_player_profile_index */
long player_ui_get_active_player_profile_index(short p){(void)p;return test_profile;}
/* Kevin Boggs: player_ui_get_path_to_local_player_profile_directory */
int player_ui_get_path_to_local_player_profile_directory(short p,char *path){(void)p;++profile_reads;if(!profile_available)return 0;strcpy(path,profile_directory);return 1;}
/* CHECKPOINT_PATH_IMPLEMENTATION */
static int fail_after=-1;static unsigned live_allocations;
/* Kevin Boggs: checked_malloc */
static void *checked_malloc(size_t n){if(fail_after==0)return NULL;if(fail_after>0)--fail_after;void *p=malloc(n);if(p)++live_allocations;return p;}
/* Kevin Boggs: checked_calloc */
static void *checked_calloc(size_t n,size_t size){if(fail_after==0)return NULL;if(fail_after>0)--fail_after;void *p=calloc(n,size);if(p)++live_allocations;return p;}
/* Kevin Boggs: checked_free */
static void checked_free(void *p){if(p){assert(live_allocations);--live_allocations;}free(p);}
#define malloc checked_malloc
#define calloc checked_calloc
#define free checked_free
/* CHECKPOINT_SORT_IMPLEMENTATION */
#include "engine_save_codec.c"
#undef malloc
#undef calloc
#undef free
/* Kevin Boggs: bits_equal */
static int bits_equal(float a,float b){uint32_t x,y;memcpy(&x,&a,4);memcpy(&y,&b,4);return x==y || (isnan(a)&&isnan(b));}
/* Kevin Boggs: main */
int main(void){
    char path[256];test_profile=1;n3ds_checkpoint_profile_changed();assert(profile_reads==1);
    for(int i=0;i<100;++i)assert(path_for_directory(NULL,path) && !strcmp(path,"u:\\one\\savegame.n3ds"));
    assert(profile_reads==1);
    test_profile=2;strcpy(profile_directory,"u:\\two");assert(path_for_directory(NULL,path) && profile_reads==2);
    strcpy(profile_directory,"u:\\replacement");n3ds_checkpoint_profile_changed();assert(profile_reads==3);
    assert(path_for_directory(NULL,path) && !strcmp(path,"u:\\replacement\\savegame.n3ds"));
    test_profile=NONE;assert(!path_for_directory(NULL,path));test_profile=3;profile_available=0;
    n3ds_checkpoint_profile_changed();assert(!path_for_directory(NULL,path));profile_available=1;
    strcpy(profile_directory,"d:\\wrong");assert(!path_for_directory(NULL,path));
    strcpy(profile_directory,"u:\\valid");assert(path_for_directory(NULL,path));
    assert(path_for_directory("u:\\explicit",path) && !strcmp(path,"u:\\explicit\\savegame.n3ds"));
    assert(native_model_name_lookup(7,"head",0,lookup_reference)==7 && queries==1);
    assert(native_model_name_lookup(7,"head",0,lookup_reference)==7 && queries==1);
    assert(native_model_name_lookup(7,"none",0,lookup_reference)==-1 && queries==2);
    assert(native_model_name_lookup(7,"none",0,lookup_reference)==-1 && queries==2);
    ++generation;assert(native_model_name_lookup(7,"head",0,lookup_reference)==8 && queries==3);
    assert(native_model_name_lookup(7,"head",1,lookup_reference)==8 && queries==4);
    char changed[32]="head";assert(native_model_name_lookup(7,changed,0,lookup_reference)==8);
    strcpy(changed,"HEAD");assert(native_model_name_lookup(7,changed,0,lookup_reference)==-1);
    for(long m=0;m<1000;++m)assert(native_model_name_lookup(m,"head",m&1,lookup_reference)==m+generation);
    uint32_t seed=345;
    for(unsigned int i=0;i<1000000;++i){seed=1664525u*seed+1013904223u;float f;memcpy(&f,&seed,4);
        assert(bits_equal(n3ds_float_fraction(f),(float)fmod((double)f,1.0)));
        assert(bits_equal(n3ds_float_floor(f),(float)floor((double)f)));
    }
    struct pointer_record records[16384],reference[16384];
    for(int failure=0;failure<2;++failure){
        for(unsigned int i=0;i<16384;++i){seed=seed*1664525u+1013904223u;records[i]=(struct pointer_record){(seed%0xd1400u)*4u,i};}
        memcpy(reference,records,sizeof(records));qsort(reference,16384,sizeof(*reference),compare_records);
        if(failure)fail_after=0;
        checkpoint_sort_records(records,16384);fail_after=-1;
        assert(!live_allocations);
        for(unsigned int i=0;i<16384;++i)assert(records[i].offset==reference[i].offset);
    }
    for(original=0;original<2;++original){assert(n3ds_save_codec_tests());
        unsigned char *data=calloc(1,SAVE_LIMIT);assert(data);
        struct save_snapshot *old=n3ds_save_snapshot_create(data,SAVE_LIMIT);assert(old);
        for(int pass=0;pass<12;++pass){
            unsigned int offset=(pass*31u%SAVE_BLOCKS)*SAVE_BLOCK;data[offset]^=57;
            struct save_snapshot *next=n3ds_save_snapshot_create_reusing(data,SAVE_LIMIT,old);assert(next);
            assert(n3ds_save_snapshot_validate(next) && n3ds_save_snapshot_matches(next,data));
            unsigned int shared=0;for(int i=0;i<SAVE_BLOCKS;++i)shared+=next->blocks[i]==old->blocks[i];assert(shared==SAVE_BLOCKS-1);
            n3ds_save_snapshot_release(old);old=next;
        }
        n3ds_save_snapshot_release(old);free(data);
        unsigned char raw[32768*2+17]={0};old=n3ds_save_snapshot_create(raw,sizeof(raw));assert(old);
        memset(raw,7,sizeof(raw));unsigned before=live_allocations;
        for(int failure=0;failure<12;++failure){fail_after=failure;
            struct save_snapshot *attempt=n3ds_save_snapshot_create_reusing(raw,sizeof(raw),old);fail_after=-1;
            if(attempt){assert(n3ds_save_snapshot_validate(attempt) && n3ds_save_snapshot_matches(attempt,raw));n3ds_save_snapshot_release(attempt);}
            assert(live_allocations==before && n3ds_save_snapshot_validate(old));
        }
        n3ds_save_snapshot_release(old);assert(!live_allocations);
    }
    puts("PASS: 1,000,000 exact float phase/floor vectors; both-model codec, partial block, changed/unchanged shared blocks, corruption, chained ownership and restore");
}
