#ifndef HALO_N3DS_ENGINE_MODEL_PALETTE_H
#define HALO_N3DS_ENGINE_MODEL_PALETTE_H

/* Canonical order for an already validated compact palette. Membership is
 * unchanged. Identity palettes retain their original register assignments. */
static inline unsigned int n3ds_model_order_compact_palette(unsigned char *palette,unsigned char remap[64])
{
    unsigned int slot=0,changed=0;
    for(unsigned int bone=0;bone<64;++bone) {
        if(remap[bone]==255)continue;
        changed+=remap[bone]!=slot;
        palette[slot]=bone;
        remap[bone]=slot++;
    }
    return changed;
}
#endif
