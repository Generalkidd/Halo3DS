/* New3DS integration additions by Kevin Boggs. */
#ifndef HALO_N3DS_CAMPAIGN_OVERLAY_H
#define HALO_N3DS_CAMPAIGN_OVERLAY_H

struct n3ds_cache_view;

/* Attach owner-supplied PC Rocket Warthog definitions to Xbox b30 tags. */
int n3ds_campaign_overlay_open(struct n3ds_cache_view *xbox, const char *map_name);
void n3ds_campaign_overlay_close(void);
int n3ds_campaign_overlay_active(void);

#endif
