#include "rtype_blit.h"

#include <stddef.h>
#include <stdint.h>

#ifndef RTYPE_BLIT_USE_PACKED32
#define RTYPE_BLIT_USE_PACKED32 1
#endif

uint16_t rtype_blit_rgb565_identity(uint16_t rgb565) {
    return rgb565;
}

static inline uint32_t pack2_rgb565(uint16_t a, uint16_t b) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (uint32_t)a | ((uint32_t)b << 16);
#else
    return ((uint32_t)a << 16) | (uint32_t)b;
#endif
}

static inline void store2_rgb565(uint16_t *dst, uint16_t a, uint16_t b) {
#if RTYPE_BLIT_USE_PACKED32
    // Safe on ESP-IDF malloc/DMA buffers, which are at least 32-bit aligned.
    *(uint32_t *)dst = pack2_rgb565(a, b);
#else
    dst[0] = a;
    dst[1] = b;
#endif
}

static inline void fill_row_rgb565(uint16_t *dst, unsigned count, uint16_t color) {
#if RTYPE_BLIT_USE_PACKED32
    uint32_t packed = pack2_rgb565(color, color);
    unsigned pairs = count >> 1;
    uint32_t *d32 = (uint32_t *)dst;
    for (unsigned i = 0; i < pairs; i++) d32[i] = packed;
    if (count & 1u) dst[count - 1u] = color;
#else
    for (unsigned i = 0; i < count; i++) dst[i] = color;
#endif
}

void rtype_blit_cyd_fill_border_strip(uint16_t *dst, unsigned dst_y, unsigned rows, uint16_t color) {
    (void)dst_y;
    if (dst == NULL) return;
    for (unsigned row = 0; row < rows; row++) {
        fill_row_rgb565(dst + (size_t)row * RTYPE_BLIT_CYD_PHYS_W, RTYPE_BLIT_CYD_PHYS_W, color);
    }
}

static inline void scale_5_from_8(uint16_t *out, const uint16_t *in) {
    // 8 source pixels -> 5 destination pixels for exact 384->240 5/8 scaling.
    // dst source indices: 0,1,3,4,6. Unrolled to avoid divides in the hot loop.
    out[0] = in[0];
    store2_rgb565(out + 1, in[1], in[3]);
    store2_rgb565(out + 3, in[4], in[6]);
}

void rtype_blit_cyd_scale_strip_240x160(const uint16_t *src, uint16_t *dst, unsigned dst_y, unsigned rows) {
    if (src == NULL || dst == NULL) return;

    // Exact-aspect 5/8 downscale: 384x256 -> 240x160. The source row pattern
    // repeats every 5 destination rows with source offsets: 0,1,3,4,6.
    static const uint8_t y_lut[5] = {0, 1, 3, 4, 6};

    for (unsigned row = 0; row < rows; row++) {
        const unsigned y = dst_y + row;
        uint16_t *out = dst + (size_t)row * RTYPE_BLIT_CYD_PHYS_W;
        if (y < RTYPE_BLIT_CYD_VIEW_Y || y >= RTYPE_BLIT_CYD_VIEW_Y + RTYPE_BLIT_CYD_VIEW_H) {
            fill_row_rgb565(out, RTYPE_BLIT_CYD_PHYS_W, 0);
            continue;
        }

        const unsigned view_y = y - RTYPE_BLIT_CYD_VIEW_Y;
        const unsigned src_y = (view_y / 5u) * 8u + y_lut[view_y % 5u];
        const uint16_t *src_row = src + (size_t)src_y * RTYPE_BLIT_SRC_W;

        for (unsigned block = 0; block < RTYPE_BLIT_CYD_PHYS_W / 5u; block++) {
            scale_5_from_8(out + block * 5u, src_row + block * 8u);
        }
    }
}
