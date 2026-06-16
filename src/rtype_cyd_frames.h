#ifndef RTYPE_CYD_FRAMES_H
#define RTYPE_CYD_FRAMES_H

#include <stdint.h>

#define RTYPE_CYD_FRAME_W 320u
#define RTYPE_CYD_FRAME_H 240u
#define RTYPE_CYD_FRAME_PIXELS (RTYPE_CYD_FRAME_W * RTYPE_CYD_FRAME_H)

extern const uint16_t rtype_cyd_frame_300m[RTYPE_CYD_FRAME_PIXELS];
extern const uint16_t rtype_cyd_frame_600m[RTYPE_CYD_FRAME_PIXELS];

#endif
