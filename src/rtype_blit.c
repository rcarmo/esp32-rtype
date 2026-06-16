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

static uint16_t s_cyd_src_x_for_phys_y[RTYPE_BLIT_CYD_PHYS_H];
static uint16_t s_cyd_src_y_for_phys_x[RTYPE_BLIT_CYD_PHYS_W];
static uint8_t s_cyd_lut_ready;

static void init_cyd_rotated_luts(void) {
    if (s_cyd_lut_ready) return;

    // physical y -> logical landscape x -> source x. This is fixed for the
    // 320-wide logical landscape output and removes a divide from every pixel.
    for (unsigned py = 0; py < RTYPE_BLIT_CYD_PHYS_H; py++) {
        const unsigned logical_x = (RTYPE_BLIT_CYD_LOGICAL_W - 1u) - py;
        unsigned src_x = (logical_x * RTYPE_BLIT_SRC_W) / RTYPE_BLIT_CYD_VIEW_W;
        if (src_x >= RTYPE_BLIT_SRC_W) src_x = RTYPE_BLIT_SRC_W - 1u;
        s_cyd_src_x_for_phys_y[py] = (uint16_t)src_x;
    }

    // physical x -> logical landscape y -> source y. 0xffff marks side bars.
    for (unsigned px = 0; px < RTYPE_BLIT_CYD_PHYS_W; px++) {
        if (px < RTYPE_BLIT_CYD_ACTIVE_X0 || px >= RTYPE_BLIT_CYD_ACTIVE_X1) {
            s_cyd_src_y_for_phys_x[px] = 0xffffu;
        } else {
            const unsigned view_y = px - RTYPE_BLIT_CYD_VIEW_Y;
            unsigned src_y = (view_y * RTYPE_BLIT_SRC_H) / RTYPE_BLIT_CYD_VIEW_H;
            if (src_y >= RTYPE_BLIT_SRC_H) src_y = RTYPE_BLIT_SRC_H - 1u;
            s_cyd_src_y_for_phys_x[px] = (uint16_t)src_y;
        }
    }

    s_cyd_lut_ready = 1;
}

void rtype_blit_cyd_rotate_scale_columns_320x213(const uint16_t *src, uint16_t *dst,
                                                 unsigned phys_x, unsigned cols) {
    if (src == NULL || dst == NULL || cols == 0 || phys_x >= RTYPE_BLIT_CYD_PHYS_W) return;
    if (phys_x + cols > RTYPE_BLIT_CYD_PHYS_W) cols = RTYPE_BLIT_CYD_PHYS_W - phys_x;
    init_cyd_rotated_luts();

    // Rotated fill mode for 240x320 portrait CYD:
    //   logical landscape output: 320x240
    //   aspect-correct viewport: 320x213, centered at logical y=13
    //   physical x corresponds to logical y
    //   physical y corresponds to reversed logical x
    // Flush rectangles are physical columns [phys_x, phys_x+cols) x [0,320).
    // Hot path uses only LUTs, adds, and packed RGB565 stores.
    uint16_t sy[16];
    if (cols > (sizeof(sy) / sizeof(sy[0]))) return;
    uint8_t all_active = 1;
    for (unsigned c = 0; c < cols; c++) {
        sy[c] = s_cyd_src_y_for_phys_x[phys_x + c];
        if (sy[c] == 0xffffu) all_active = 0;
    }

    for (unsigned py = 0; py < RTYPE_BLIT_CYD_PHYS_H; py++) {
        uint16_t *out = dst + (size_t)py * cols;
        const unsigned sx = s_cyd_src_x_for_phys_y[py];

        if (!all_active) {
            for (unsigned c = 0; c < cols; c++) {
                out[c] = (sy[c] == 0xffffu) ? 0 : src[(size_t)sy[c] * RTYPE_BLIT_SRC_W + sx];
            }
            continue;
        }

        unsigned c = 0;
        for (; c + 1u < cols; c += 2u) {
            uint16_t a = src[(size_t)sy[c] * RTYPE_BLIT_SRC_W + sx];
            uint16_t b = src[(size_t)sy[c + 1u] * RTYPE_BLIT_SRC_W + sx];
            store2_rgb565(out + c, a, b);
        }
        if (c < cols) out[c] = src[(size_t)sy[c] * RTYPE_BLIT_SRC_W + sx];
    }
}

static inline uint16_t boot_pattern_pixel(unsigned sx, unsigned sy, unsigned frame_no) {
    uint8_t r = (uint8_t)((sx * 255u) / (RTYPE_BLIT_SRC_W - 1u));
    uint8_t g = (uint8_t)((sy * 255u) / (RTYPE_BLIT_SRC_H - 1u));
    uint8_t b = (uint8_t)((frame_no * 3u + sx / 8u + sy / 8u) & 0xffu);
    if (sx < 4u || sy < 4u || sx >= RTYPE_BLIT_SRC_W - 4u || sy >= RTYPE_BLIT_SRC_H - 4u) {
        r = g = b = 255;
    }
    if (((sx / 16u) + (sy / 16u) + (frame_no / 15u)) & 1u) {
        b = (uint8_t)(255u - b);
    }
    return (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                      ((uint16_t)(g & 0xfcu) << 3) |
                      ((uint16_t)b >> 3));
}

void rtype_blit_cyd_rotate_boot_pattern_columns_320x213(uint16_t *dst, unsigned phys_x,
                                                        unsigned cols, unsigned frame_no) {
    if (dst == NULL || cols == 0 || phys_x >= RTYPE_BLIT_CYD_PHYS_W) return;
    if (phys_x + cols > RTYPE_BLIT_CYD_PHYS_W) cols = RTYPE_BLIT_CYD_PHYS_W - phys_x;
    init_cyd_rotated_luts();

    uint16_t sy[16];
    if (cols > (sizeof(sy) / sizeof(sy[0]))) return;
    uint8_t all_active = 1;
    for (unsigned c = 0; c < cols; c++) {
        sy[c] = s_cyd_src_y_for_phys_x[phys_x + c];
        if (sy[c] == 0xffffu) all_active = 0;
    }

    for (unsigned py = 0; py < RTYPE_BLIT_CYD_PHYS_H; py++) {
        uint16_t *out = dst + (size_t)py * cols;
        const unsigned sx = s_cyd_src_x_for_phys_y[py];
        if (!all_active) {
            for (unsigned c = 0; c < cols; c++) {
                out[c] = (sy[c] == 0xffffu) ? 0 : boot_pattern_pixel(sx, sy[c], frame_no);
            }
            continue;
        }
        unsigned c = 0;
        for (; c + 1u < cols; c += 2u) {
            store2_rgb565(out + c, boot_pattern_pixel(sx, sy[c], frame_no),
                          boot_pattern_pixel(sx, sy[c + 1u], frame_no));
        }
        if (c < cols) out[c] = boot_pattern_pixel(sx, sy[c], frame_no);
    }
}
