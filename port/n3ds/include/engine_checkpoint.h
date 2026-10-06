#ifndef HALO_N3DS_CHECKPOINT_H
#define HALO_N3DS_CHECKPOINT_H
/* Visit known pointer fields only. clear=1 serializes a transient reference as
 * NULL without changing the running game. Integers are never guessed to be
 * pointers by their numeric value. */
typedef int (*n3ds_checkpoint_visitor)(void *field,int clear);
void n3ds_checkpoint_register(const char *name,const char *type,void *base,unsigned int size);
int n3ds_checkpoint_visit(n3ds_checkpoint_visitor visit);
int n3ds_checkpoint_hs_visit(n3ds_checkpoint_visitor visit);
int n3ds_checkpoint_hud_visit(n3ds_checkpoint_visitor visit);
int n3ds_checkpoint_detail_visit(n3ds_checkpoint_visitor visit);
unsigned long n3ds_checkpoint_callback(unsigned int index);
void n3ds_checkpoint_poll(void);
void n3ds_checkpoint_finish(void);
const void *n3ds_game_state_snapshot(void);
unsigned int n3ds_save_pack_bound(void);
unsigned int n3ds_save_pack(const void *,unsigned int,void *,unsigned int);
int n3ds_save_unpack(const void *,unsigned int,void *,unsigned int);
void *n3ds_save_snapshot_create(const void *,unsigned int);
void n3ds_save_snapshot_retain(const void *);
void n3ds_save_snapshot_release(const void *);
unsigned int n3ds_save_snapshot_size(const void *);
int n3ds_save_snapshot_read(const void *,unsigned int,void *,unsigned int);
int n3ds_save_snapshot_matches(const void *,const void *);
int n3ds_save_snapshot_validate(const void *);
int n3ds_save_codec_tests(void);
#endif
