#ifndef RTYPE_M72_VIDEO_H
#define RTYPE_M72_VIDEO_H

#include "rtype_board.h"

#include <stddef.h>
#include <stdint.h>

#define RTYPE_M72_SPRITERAM_BYTES 0x400u
#define RTYPE_M72_VRAM_BYTES 0x4000u
#define RTYPE_M72_PALETTE_COLORS 512u

typedef struct {
    const uint8_t *sprites;
    size_t sprites_size;
    const uint8_t *tiles0;
    size_t tiles0_size;
    const uint8_t *tiles1;
    size_t tiles1_size;
    uint8_t *spriteram;
    uint8_t *vram0;
    uint8_t *vram1;
    uint16_t palette[RTYPE_M72_PALETTE_COLORS];
    uint16_t scrollx[2];
    uint16_t scrolly[2];
    uint8_t video_off;
} rtype_m72_video_t;

uint8_t *rtype_m72_alloc_region(size_t bytes, const char *name);
void rtype_m72_video_init(rtype_m72_video_t *video);
void rtype_m72_video_seed_probe_scene(rtype_m72_video_t *video, unsigned frame_no);
void rtype_m72_video_render(const rtype_m72_video_t *video, uint16_t *fb);

#endif
