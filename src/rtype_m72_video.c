#include "rtype_m72_video.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "rtype_m72";

static uint16_t read16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void write16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

uint8_t *rtype_m72_alloc_region(size_t bytes, const char *name) {
    uint8_t *p = (uint8_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = (uint8_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    if (p == NULL) {
        ESP_LOGE(TAG, "failed to allocate %s region: %u bytes", name ? name : "unnamed", (unsigned)bytes);
        return NULL;
    }
    ESP_LOGI(TAG, "allocated %s region: %u bytes @%p", name ? name : "unnamed", (unsigned)bytes, (void *)p);
    return p;
}

static uint16_t visible_color(uint16_t palette_entry, unsigned palette_index, uint8_t pen) {
    (void)palette_index;
    (void)pen;
    return palette_entry;
}

void rtype_m72_video_init(rtype_m72_video_t *video) {
    if (video == NULL) return;
    memset(video, 0, sizeof(*video));
    for (unsigned i = 0; i < RTYPE_M72_PALETTE_COLORS; i++) {
        uint8_t r = (uint8_t)((i * 37u) & 0xffu);
        uint8_t g = (uint8_t)((i * 73u) & 0xffu);
        uint8_t b = (uint8_t)((i * 17u) & 0xffu);
        if ((i & 15u) == 0) r = g = b = 0;
        video->palette[i] = rtype_rgb565(r, g, b);
    }
}

void rtype_m72_video_seed_probe_scene(rtype_m72_video_t *video, unsigned frame_no) {
    if (video == NULL || video->vram0 == NULL || video->vram1 == NULL || video->spriteram == NULL) return;

    for (unsigned i = 0; i < 64u * 64u; i++) {
        write16le(video->vram1 + i * 4u, (uint16_t)((i + frame_no / 8u) & 0x0fffu));
        write16le(video->vram1 + i * 4u + 2u, (uint16_t)((i >> 8) & 0x0fu));
        write16le(video->vram0 + i * 4u, (uint16_t)((i + 0x80u + frame_no / 4u) & 0x0fffu));
        write16le(video->vram0 + i * 4u + 2u, (uint16_t)(((i >> 7) + 2u) & 0x0fu));
    }

    memset(video->spriteram, 0, RTYPE_M72_SPRITERAM_BYTES);
    uint16_t sx = (uint16_t)(256u + ((frame_no * 2u) % RTYPE_GAME_W));
    uint16_t sy = (uint16_t)(384u - (96u + ((frame_no / 2u) % 96u)));
    write16le(video->spriteram + 0, sy);
    write16le(video->spriteram + 2, (uint16_t)((frame_no / 3u) & 0x03ffu));
    write16le(video->spriteram + 4, 0x1003u); // 2-high, palette 3
    write16le(video->spriteram + 6, sx);
}

static uint8_t fallback_tile_pixel(unsigned code, unsigned x, unsigned y) {
    unsigned v = (code * 13u) ^ (x * 17u) ^ (y * 29u) ^ ((code >> 3) * 7u);
    return (uint8_t)((v >> 3) & 0x0fu);
}

static uint8_t decode_tile_pixel(const uint8_t *region, size_t size, unsigned code, unsigned x, unsigned y) {
    if (region == NULL || size < 4u || code * 8u >= size / 4u) return fallback_tile_pixel(code, x, y);
    const unsigned quarter = (unsigned)(size / 4u);
    const unsigned plane_offsets[4] = {quarter * 3u, quarter * 2u, quarter, 0u};
    const unsigned base = code * 8u;
    uint8_t pix = 0;
    for (unsigned p = 0; p < 4; p++) {
        unsigned off = plane_offsets[p] + base + y;
        uint8_t b = (off < size) ? region[off] : 0;
        pix |= (uint8_t)(((b >> (7u - x)) & 1u) << (3u - p));
    }
    return pix;
}

static uint8_t fallback_sprite_pixel(unsigned code, unsigned x, unsigned y) {
    unsigned v = (code * 11u) ^ (x * 19u) ^ (y * 23u);
    return (uint8_t)((v >> 2) & 0x0fu);
}

static uint8_t decode_sprite_pixel(const rtype_m72_video_t *video, unsigned code, unsigned x, unsigned y) {
    if (video->sprites == NULL || video->sprites_size < 4u || code * 32u >= video->sprites_size / 4u) {
        return fallback_sprite_pixel(code, x, y);
    }
    const unsigned quarter = (unsigned)(video->sprites_size / 4u);
    const unsigned plane_offsets[4] = {quarter * 3u, quarter * 2u, quarter, 0u};
    const unsigned col = x & 15u;
    const unsigned bit_off = (y & 15u) * 8u + ((col >= 8u) ? (16u * 8u + (col - 8u)) : col);
    const unsigned byte_in_char = bit_off >> 3;
    const unsigned bit_in_byte = 7u - (bit_off & 7u);
    const unsigned base = code * 32u;
    uint8_t pix = 0;
    for (unsigned p = 0; p < 4; p++) {
        unsigned off = plane_offsets[p] + base + byte_in_char;
        uint8_t b = (off < video->sprites_size) ? video->sprites[off] : 0;
        pix |= (uint8_t)(((b >> bit_in_byte) & 1u) << (3u - p));
    }
    return pix;
}

static void put_pixel(uint16_t *fb, int x, int y, uint16_t c) {
    // M72 raw screen is 512 pixels wide with visible X range 64..447.
    // Convert raw hardware X into the 384-wide output framebuffer.
    x -= 64;
    if (x >= 0 && x < (int)RTYPE_GAME_W && y >= 0 && y < (int)RTYPE_GAME_H) {
        fb[(size_t)y * RTYPE_GAME_W + x] = c;
    }
}

static void draw_tile_layer(const rtype_m72_video_t *video, uint16_t *fb, const uint8_t *region,
                            size_t region_size, const uint8_t *vram, unsigned palette_base,
                            uint16_t sx_scroll, uint16_t sy_scroll, bool transparent) {
    if (vram == NULL) return;
    for (unsigned ty = 0; ty < 64u; ty++) {
        for (unsigned tx = 0; tx < 64u; tx++) {
            const uint8_t *entry = vram + (ty * 64u + tx) * 4u;
            uint16_t raw_code = read16le(entry);
            uint16_t attr = read16le(entry + 2u);
            unsigned code = raw_code & 0x3fffu;
            unsigned color = attr & 0x0fu;
            bool flipx = (raw_code & 0x4000u) != 0;
            bool flipy = (raw_code & 0x8000u) != 0;
            int base_x = (int)(tx * 8u) - (int)(sx_scroll & 0x1ffu);
            int base_y = (int)(ty * 8u) - (int)(sy_scroll & 0x1ffu) - 128;
            while (base_x < -8) base_x += 512;
            while (base_y < -8) base_y += 512;
            if (base_x >= (int)RTYPE_GAME_W || base_y >= (int)RTYPE_GAME_H) continue;
            for (unsigned py = 0; py < 8u; py++) {
                for (unsigned px = 0; px < 8u; px++) {
                    unsigned rx = flipx ? (7u - px) : px;
                    unsigned ry = flipy ? (7u - py) : py;
                    uint8_t pen = decode_tile_pixel(region, region_size, code, rx, ry);
                    if (transparent && pen == 0) continue;
                    unsigned pi = palette_base + color * 16u + pen;
                    put_pixel(fb, base_x + (int)px, base_y + (int)py,
                              visible_color(video->palette[pi & 0x1ffu], pi, pen));
                }
            }
        }
    }
}

static void draw_sprites(const rtype_m72_video_t *video, uint16_t *fb) {
    if (video->spriteram == NULL) return;
    for (int offs = (int)RTYPE_M72_SPRITERAM_BYTES - 8; offs >= 0; offs -= 8) {
        const uint8_t *s = video->spriteram + offs;
        uint16_t syw = read16le(s + 0);
        uint16_t code = read16le(s + 2);
        uint16_t attr = read16le(s + 4);
        uint16_t sxw = read16le(s + 6);
        if ((syw | code | attr | sxw) == 0) continue;
        unsigned color = attr & 0x0fu;
        int sx = -256 + (int)(sxw & 0x03ffu);
        int sy = 384 - (int)(syw & 0x01ffu);
        bool flipx = (attr & 0x0800u) != 0;
        bool flipy = (attr & 0x0400u) != 0;
        unsigned w = 1u << ((attr >> 14) & 3u);
        unsigned h = 1u << ((attr >> 12) & 3u);
        sy -= (int)(16u * h);
        for (unsigned cx = 0; cx < w; cx++) {
            for (unsigned cy = 0; cy < h; cy++) {
                unsigned c = code + (flipx ? 8u * (w - 1u - cx) : 8u * cx) + (flipy ? (h - 1u - cy) : cy);
                for (unsigned py = 0; py < 16u; py++) {
                    for (unsigned px = 0; px < 16u; px++) {
                        unsigned rx = flipx ? (15u - px) : px;
                        unsigned ry = flipy ? (15u - py) : py;
                        uint8_t pen = decode_sprite_pixel(video, c, rx, ry);
                        if (pen == 0) continue;
                        unsigned pi = color * 16u + pen;
                        put_pixel(fb, sx + (int)(cx * 16u + px), sy + (int)(cy * 16u + py),
                                  visible_color(video->palette[pi & 0x1ffu], pi, pen));
                    }
                }
            }
        }
    }
}

void rtype_m72_video_render(const rtype_m72_video_t *video, uint16_t *fb) {
    if (video == NULL || fb == NULL) return;
    uint16_t bg = video->video_off ? 0 : rtype_rgb565(4, 8, 16);
    for (size_t i = 0; i < (size_t)RTYPE_GAME_W * RTYPE_GAME_H; i++) fb[i] = bg;
    if (video->video_off) return;
    draw_tile_layer(video, fb, video->tiles1, video->tiles1_size, video->vram1, 256u,
                    video->scrollx[1], video->scrolly[1], false);
    draw_tile_layer(video, fb, video->tiles0, video->tiles0_size, video->vram0, 256u,
                    video->scrollx[0], video->scrolly[0], true);
    draw_sprites(video, fb);
}
