/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_ENGINE_MAP_FORMAT_H
#define HALO_N3DS_ENGINE_MAP_FORMAT_H
#include <string.h>
#include "pc_diagnostic_maps.h"

/* A format-qualified path travels in Halo's existing reliable map settings.
 * An unqualified path always means Xbox, even when PC data is installed. */
/* Kevin Boggs: n3ds_map_uses_pc - explicit map format shared by host and clients. */
static inline int n3ds_map_uses_pc(const char *path)
{ return path && !strncmp(path,"pc\\levels\\test\\",15); }
/* Kevin Boggs: n3ds_pc_map_selection - validate the entire qualified path. */
static inline int n3ds_pc_map_selection(const char *path)
{
 if(!n3ds_map_uses_pc(path))return -1;
 const char *name=path+15;
 for(int i=0;i<19;++i){
  size_t length=strlen(n3ds_pc_diagnostic_maps[i]);
  if(!strncmp(name,n3ds_pc_diagnostic_maps[i],length) && name[length]=='\\' &&
     !strcmp(name+length+1,n3ds_pc_diagnostic_maps[i]))return i;
 }
 return -1;
}
static const char *const n3ds_pc_map_labels[19]={
 "Beaver Creek (PC)","Blood Gulch (PC)","Boarding Action (PC)","Derelict (PC)","Chill Out (PC)",
 "Damnation (PC)","Danger Canyon (PC)","Death Island (PC)","Gephyrophobia (PC)",
 "Hang 'Em High (PC)","Ice Fields (PC)","Infinity (PC)","Longest (PC)","Prisoner (PC)",
 "Chiron TL-34 (PC)","Rat Race (PC)","Sidewinder (PC)","Timberland (PC)","Wizard (PC)"
};
/* Existing Xbox UI thumbnails for shared maps; no unrelated image for PC-only maps. */
static const short n3ds_pc_map_thumbnails[19]={0,9,8,7,6,2,-1,-1,-1,5,-1,-1,12,4,11,3,1,-1,10};
int n3ds_pc_maps_present(void);
unsigned long n3ds_pc_map_identity(const char *base);
#endif
