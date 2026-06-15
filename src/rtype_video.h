#ifndef RTYPE_VIDEO_H
#define RTYPE_VIDEO_H

#include <stdint.h>

#define RTYPE_FRAME_PIXELS (RTYPE_GAME_W * RTYPE_GAME_H)

uint16_t *rtype_video_alloc_framebuffer(void);
void rtype_video_render_boot_pattern(uint16_t *fb, unsigned frame_no);

#endif
