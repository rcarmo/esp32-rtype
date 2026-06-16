#include "rtype_m72_video.h"
#include "rtype_blit.h"

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
    const uint8_t *sprite_ram = video->sprite_buffer_valid ? video->sprite_buffer : video->spriteram;
    if (sprite_ram == NULL) return;
    for (int offs = (int)RTYPE_M72_SPRITERAM_BYTES - 8; offs >= 0; offs -= 8) {
        const uint8_t *s = sprite_ram + offs;
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

static uint16_t sample_tile_layer_pixel(const rtype_m72_video_t *video, const uint8_t *region,
                                        size_t region_size, const uint8_t *vram,
                                        unsigned palette_base, uint16_t sx_scroll,
                                        uint16_t sy_scroll, unsigned raw_x, unsigned raw_y,
                                        bool transparent, bool *hit) {
    if (hit) *hit = false;
    if (vram == NULL) return 0;
    unsigned wx = (raw_x + (sx_scroll & 0x1ffu)) & 0x1ffu;
    unsigned wy = (raw_y + (sy_scroll & 0x1ffu) + 128u) & 0x1ffu;
    unsigned tx = wx >> 3;
    unsigned ty = wy >> 3;
    unsigned px = wx & 7u;
    unsigned py = wy & 7u;
    const uint8_t *entry = vram + (ty * 64u + tx) * 4u;
    uint16_t raw_code = read16le(entry);
    uint16_t attr = read16le(entry + 2u);
    bool flipx = (raw_code & 0x4000u) != 0;
    bool flipy = (raw_code & 0x8000u) != 0;
    unsigned rx = flipx ? (7u - px) : px;
    unsigned ry = flipy ? (7u - py) : py;
    uint8_t pen = decode_tile_pixel(region, region_size, raw_code & 0x3fffu, rx, ry);
    if (pen == 0) {
        if (transparent) return 0;
        if (hit) *hit = true;
        return 0;
    }
    if (hit) *hit = true;
    unsigned color = attr & 0x0fu;
    unsigned pi = palette_base + color * 16u + pen;
    uint16_t c = video->palette[pi & 0x1ffu];
    if (c == 0 && pen != 0 && palette_base >= 256u) {
        c = video->palette[(color * 16u + pen) & 0x1ffu];
    }
    return visible_color(c, pi, pen);
}

static inline uint16_t cyd_wire_rgb565(uint16_t rgb565);

typedef struct {
    int sx;
    int sy;
    uint16_t code;
    uint16_t attr;
    uint8_t w;
    uint8_t h;
    bool flipx;
    bool flipy;
} sprite_entry_t;

static uint16_t sample_sprite_pixel(const rtype_m72_video_t *video, unsigned raw_x, unsigned raw_y, bool *hit) {
    if (hit) *hit = false;
    if (video == NULL) return 0;
    const uint8_t *sprite_ram = video->sprite_buffer_valid ? video->sprite_buffer : video->spriteram;
    if (sprite_ram == NULL) return 0;
    uint16_t out = 0;
    for (int offs = (int)RTYPE_M72_SPRITERAM_BYTES - 8; offs >= 0; offs -= 8) {
        const uint8_t *s = sprite_ram + offs;
        uint16_t syw = read16le(s + 0);
        uint16_t code = read16le(s + 2);
        uint16_t attr = read16le(s + 4);
        uint16_t sxw = read16le(s + 6);
        if ((syw | code | attr | sxw) == 0) continue;
        int sx = -256 + (int)(sxw & 0x03ffu);
        int sy = 384 - (int)(syw & 0x01ffu);
        bool flipx = (attr & 0x0800u) != 0;
        bool flipy = (attr & 0x0400u) != 0;
        unsigned w = 1u << ((attr >> 14) & 3u);
        unsigned h = 1u << ((attr >> 12) & 3u);
        sy -= (int)(16u * h);
        if ((int)raw_x < sx || (int)raw_y < sy || (int)raw_x >= sx + (int)(16u * w) ||
            (int)raw_y >= sy + (int)(16u * h)) {
            continue;
        }
        unsigned cell_x = (unsigned)((int)raw_x - sx) >> 4;
        unsigned cell_y = (unsigned)((int)raw_y - sy) >> 4;
        unsigned px = (unsigned)((int)raw_x - sx) & 15u;
        unsigned py = (unsigned)((int)raw_y - sy) & 15u;
        unsigned c = code + (flipx ? 8u * (w - 1u - cell_x) : 8u * cell_x) +
                     (flipy ? (h - 1u - cell_y) : cell_y);
        unsigned rx = flipx ? (15u - px) : px;
        unsigned ry = flipy ? (15u - py) : py;
        uint8_t pen = decode_sprite_pixel(video, c, rx, ry);
        if (pen == 0) continue;
        unsigned pi = (attr & 0x0fu) * 16u + pen;
        out = visible_color(video->palette[pi & 0x1ffu], pi, pen);
        if (hit) *hit = true;
    }
    return out;
}

static unsigned collect_visible_sprite_entries(const rtype_m72_video_t *video, sprite_entry_t *entries, unsigned max_entries) {
    if (video == NULL || entries == NULL || max_entries == 0) return 0;
    const uint8_t *sprite_ram = video->sprite_buffer_valid ? video->sprite_buffer : video->spriteram;
    if (sprite_ram == NULL) return 0;
    unsigned count = 0;
    for (int offs = (int)RTYPE_M72_SPRITERAM_BYTES - 8; offs >= 0 && count < max_entries; offs -= 8) {
        const uint8_t *s = sprite_ram + offs;
        uint16_t syw = read16le(s + 0);
        uint16_t code = read16le(s + 2);
        uint16_t attr = read16le(s + 4);
        uint16_t sxw = read16le(s + 6);
        if ((syw | code | attr | sxw) == 0) continue;
        unsigned w = 1u << ((attr >> 14) & 3u);
        unsigned h = 1u << ((attr >> 12) & 3u);
        int sx = -256 + (int)(sxw & 0x03ffu);
        int sy = 384 - (int)(syw & 0x01ffu) - (int)(16u * h);
        if (sx >= 448 || sx + (int)(16u * w) <= 64 || sy >= (int)RTYPE_GAME_H || sy + (int)(16u * h) <= 0) continue;
        entries[count++] = (sprite_entry_t){
            .sx = sx,
            .sy = sy,
            .code = code,
            .attr = attr,
            .w = (uint8_t)w,
            .h = (uint8_t)h,
            .flipx = (attr & 0x0800u) != 0,
            .flipy = (attr & 0x0400u) != 0,
        };
    }
    return count;
}

static uint16_t sample_sprite_entries(const rtype_m72_video_t *video, const sprite_entry_t *entries,
                                      unsigned count, unsigned raw_x, unsigned raw_y, bool *hit) {
    if (hit) *hit = false;
    uint16_t out = 0;
    for (unsigned i = 0; i < count; i++) {
        const sprite_entry_t *e = &entries[i];
        if ((int)raw_x < e->sx || (int)raw_y < e->sy || (int)raw_x >= e->sx + (int)(16u * e->w) ||
            (int)raw_y >= e->sy + (int)(16u * e->h)) continue;
        unsigned cell_x = (unsigned)((int)raw_x - e->sx) >> 4;
        unsigned cell_y = (unsigned)((int)raw_y - e->sy) >> 4;
        unsigned px = (unsigned)((int)raw_x - e->sx) & 15u;
        unsigned py = (unsigned)((int)raw_y - e->sy) & 15u;
        unsigned c = e->code + (e->flipx ? 8u * (e->w - 1u - cell_x) : 8u * cell_x) +
                     (e->flipy ? (e->h - 1u - cell_y) : cell_y);
        unsigned rx = e->flipx ? (15u - px) : px;
        unsigned ry = e->flipy ? (15u - py) : py;
        uint8_t pen = decode_sprite_pixel(video, c, rx, ry);
        if (pen == 0) continue;
        unsigned pi = (e->attr & 0x0fu) * 16u + pen;
        out = visible_color(video->palette[pi & 0x1ffu], pi, pen);
        if (hit) *hit = true;
    }
    return out;
}

void rtype_m72_video_render_cyd_strip(const rtype_m72_video_t *video, uint16_t *dst,
                                      unsigned logical_y, unsigned rows) {
    if (video == NULL || dst == NULL) return;
    for (unsigned row = 0; row < rows; row++) {
        unsigned y = logical_y + row;
        uint16_t *out = dst + (size_t)row * RTYPE_BLIT_CYD_LOGICAL_W;
        if (video->video_off || y < RTYPE_BLIT_CYD_VIEW_Y || y >= RTYPE_BLIT_CYD_VIEW_Y + RTYPE_BLIT_CYD_VIEW_H) {
            for (unsigned x = 0; x < RTYPE_BLIT_CYD_LOGICAL_W; x++) out[x] = 0;
            continue;
        }
        unsigned view_y = y - RTYPE_BLIT_CYD_VIEW_Y;
        unsigned src_y = (view_y * RTYPE_GAME_H) / RTYPE_BLIT_CYD_VIEW_H;
        if (src_y >= RTYPE_GAME_H) src_y = RTYPE_GAME_H - 1u;
        for (unsigned x = 0; x < RTYPE_BLIT_CYD_LOGICAL_W; x++) {
            unsigned src_x = (x * RTYPE_GAME_W) / RTYPE_BLIT_CYD_VIEW_W;
            if (src_x >= RTYPE_GAME_W) src_x = RTYPE_GAME_W - 1u;
            unsigned raw_x = src_x + 64u;
            unsigned raw_y = src_y;
            bool hit = false;
            uint16_t px = sample_tile_layer_pixel(video, video->tiles1, video->tiles1_size, video->vram1,
                                                  256u, video->scrollx[1], video->scrolly[1], raw_x, raw_y, false, &hit);
            bool fg_hit = false;
            uint16_t fg = sample_tile_layer_pixel(video, video->tiles0, video->tiles0_size, video->vram0,
                                                  256u, video->scrollx[0], video->scrolly[0], raw_x, raw_y, true, &fg_hit);
            if (fg_hit) px = fg;
            bool sp_hit = false;
            uint16_t sp = sample_sprite_pixel(video, raw_x, raw_y, &sp_hit);
            if (sp_hit) px = sp;
            out[x] = cyd_wire_rgb565(px);
        }
    }
}

static uint16_t s_cyd_src_x_for_phys_y[RTYPE_BLIT_CYD_PHYS_H];
static uint16_t s_cyd_src_y_for_phys_x[RTYPE_BLIT_CYD_PHYS_W];
static uint8_t s_cyd_column_lut_ready;

static void init_cyd_column_luts(void) {
    if (s_cyd_column_lut_ready) return;

    for (unsigned py = 0; py < RTYPE_BLIT_CYD_PHYS_H; py++) {
        const unsigned logical_x = (RTYPE_BLIT_CYD_LOGICAL_W - 1u) - py;
        unsigned src_x = (logical_x * RTYPE_GAME_W) / RTYPE_BLIT_CYD_VIEW_W;
        if (src_x >= RTYPE_GAME_W) src_x = RTYPE_GAME_W - 1u;
        s_cyd_src_x_for_phys_y[py] = (uint16_t)src_x;
    }

    for (unsigned px = 0; px < RTYPE_BLIT_CYD_PHYS_W; px++) {
        if (px < RTYPE_BLIT_CYD_ACTIVE_X0 || px >= RTYPE_BLIT_CYD_ACTIVE_X1) {
            s_cyd_src_y_for_phys_x[px] = 0xffffu;
        } else {
            unsigned src_y = ((px - RTYPE_BLIT_CYD_ACTIVE_X0) * RTYPE_GAME_H) / RTYPE_BLIT_CYD_VIEW_H;
            if (src_y >= RTYPE_GAME_H) src_y = RTYPE_GAME_H - 1u;
            s_cyd_src_y_for_phys_x[px] = (uint16_t)src_y;
        }
    }

    s_cyd_column_lut_ready = 1;
}

static bool buffer_has_nonzero(const uint8_t *p, unsigned bytes) {
    if (p == NULL) return false;
    for (unsigned i = 0; i < bytes; i++) {
        if (p[i] != 0) return true;
    }
    return false;
}

static bool sprite_ram_has_nonzero(const rtype_m72_video_t *video) {
    if (video == NULL) return false;
    const uint8_t *sprite_ram = video->sprite_buffer_valid ? video->sprite_buffer : video->spriteram;
    return buffer_has_nonzero(sprite_ram, RTYPE_M72_SPRITERAM_BYTES);
}

static inline uint16_t cyd_wire_rgb565(uint16_t rgb565) {
#if defined(RTYPE_BOARD_ESP32_2432S028) && (defined(__XTENSA__) || defined(__xtensa__))
    uint32_t out;
    uint32_t tmp = rgb565;
    __asm__ __volatile__(
        "slli %0, %1, 8\n\t"
        "srli %1, %1, 8\n\t"
        "or %0, %0, %1\n\t"
        : "=&r"(out), "+r"(tmp)
        :
        :);
    return (uint16_t)out;
#else
    return rtype_blit_rgb565_identity(rgb565);
#endif
}

static void render_cyd_columns_impl(const rtype_m72_video_t *video, uint16_t *dst,
                                    unsigned phys_x, unsigned cols, bool include_sprites) {
    if (video == NULL || dst == NULL || cols == 0 || phys_x >= RTYPE_BLIT_CYD_PHYS_W) return;
    if (phys_x + cols > RTYPE_BLIT_CYD_PHYS_W) cols = RTYPE_BLIT_CYD_PHYS_W - phys_x;
    init_cyd_column_luts();

    uint16_t sy[RTYPE_BLIT_CYD_PHYS_W];
    if (cols > (sizeof(sy) / sizeof(sy[0]))) return;
    const bool have_sprites = include_sprites && sprite_ram_has_nonzero(video);
    static sprite_entry_t sprite_entries[RTYPE_M72_SPRITERAM_BYTES / 8u];
    unsigned sprite_entry_count = have_sprites ? collect_visible_sprite_entries(video, sprite_entries, sizeof(sprite_entries) / sizeof(sprite_entries[0])) : 0;
    const bool bg1_present = buffer_has_nonzero(video->vram1, RTYPE_M72_VRAM_BYTES);
    const bool bg0_present = buffer_has_nonzero(video->vram0, RTYPE_M72_VRAM_BYTES);
    const bool use_layer0_as_bg = !bg1_present && bg0_present;
    uint8_t all_active = video->video_off ? 0 : 1;
    for (unsigned c = 0; c < cols; c++) {
        sy[c] = s_cyd_src_y_for_phys_x[phys_x + c];
        if (sy[c] == 0xffffu) all_active = 0;
    }

    for (unsigned py = 0; py < RTYPE_BLIT_CYD_PHYS_H; py++) {
        uint16_t *out = dst + (size_t)py * cols;
        if (!all_active) {
            for (unsigned c = 0; c < cols; c++) {
                if (video->video_off || sy[c] == 0xffffu) {
                    out[c] = 0;
                    continue;
                }
                const unsigned raw_x = (unsigned)s_cyd_src_x_for_phys_y[py] + 64u;
                const unsigned raw_y = sy[c];
                bool hit = false;
                uint16_t px = 0;
                if (use_layer0_as_bg) {
                    bool fg_hit = false;
                    uint16_t fg = sample_tile_layer_pixel(video, video->tiles0, video->tiles0_size, video->vram0,
                                                          256u, video->scrollx[0], video->scrolly[0], raw_x, raw_y, true, &fg_hit);
                    px = fg_hit ? fg : 0;
                } else {
                    px = sample_tile_layer_pixel(video, video->tiles1, video->tiles1_size, video->vram1,
                                                 256u, video->scrollx[1], video->scrolly[1], raw_x, raw_y, false, &hit);
                    bool fg_hit = false;
                    uint16_t fg = sample_tile_layer_pixel(video, video->tiles0, video->tiles0_size, video->vram0,
                                                          256u, video->scrollx[0], video->scrolly[0], raw_x, raw_y, true, &fg_hit);
                    if (fg_hit) px = fg;
                }
                if (sprite_entry_count != 0) {
                    bool sp_hit = false;
                    uint16_t sp = sample_sprite_entries(video, sprite_entries, sprite_entry_count, raw_x, raw_y, &sp_hit);
                    if (sp_hit) px = sp;
                }
                out[c] = cyd_wire_rgb565(px);
            }
            continue;
        }

        const unsigned raw_x = (unsigned)s_cyd_src_x_for_phys_y[py] + 64u;
        for (unsigned c = 0; c < cols; c++) {
            const unsigned raw_y = sy[c];
            bool hit = false;
            uint16_t px = 0;
            if (use_layer0_as_bg) {
                bool fg_hit = false;
                uint16_t fg = sample_tile_layer_pixel(video, video->tiles0, video->tiles0_size, video->vram0,
                                                      256u, video->scrollx[0], video->scrolly[0], raw_x, raw_y, true, &fg_hit);
                px = fg_hit ? fg : 0;
            } else {
                px = sample_tile_layer_pixel(video, video->tiles1, video->tiles1_size, video->vram1,
                                             256u, video->scrollx[1], video->scrolly[1], raw_x, raw_y, false, &hit);
                bool fg_hit = false;
                uint16_t fg = sample_tile_layer_pixel(video, video->tiles0, video->tiles0_size, video->vram0,
                                                      256u, video->scrollx[0], video->scrolly[0], raw_x, raw_y, true, &fg_hit);
                if (fg_hit) px = fg;
            }
            if (sprite_entry_count != 0) {
                bool sp_hit = false;
                uint16_t sp = sample_sprite_entries(video, sprite_entries, sprite_entry_count, raw_x, raw_y, &sp_hit);
                if (sp_hit) px = sp;
            }
            out[c] = cyd_wire_rgb565(px);
        }
    }
}

void rtype_m72_video_render_cyd_columns(const rtype_m72_video_t *video, uint16_t *dst,
                                        unsigned phys_x, unsigned cols) {
    render_cyd_columns_impl(video, dst, phys_x, cols, true);
}

void rtype_m72_video_render_cyd_background_columns(const rtype_m72_video_t *video, uint16_t *dst,
                                                   unsigned phys_x, unsigned cols) {
    render_cyd_columns_impl(video, dst, phys_x, cols, false);
}

static void overlay_sprite_to_cyd_columns(const rtype_m72_video_t *video, uint16_t *dst,
                                          unsigned chunk_x, unsigned cols, const uint8_t *s) {
    uint16_t syw = read16le(s + 0);
    uint16_t code = read16le(s + 2);
    uint16_t attr = read16le(s + 4);
    uint16_t sxw = read16le(s + 6);
    if ((syw | code | attr | sxw) == 0) return;

    int sx = -256 + (int)(sxw & 0x03ffu);
    int sy = 384 - (int)(syw & 0x01ffu);
    bool flipx = (attr & 0x0800u) != 0;
    bool flipy = (attr & 0x0400u) != 0;
    unsigned w = 1u << ((attr >> 14) & 3u);
    unsigned h = 1u << ((attr >> 12) & 3u);
    sy -= (int)(16u * h);

    int src_x0 = sx - 64;
    int src_x1 = src_x0 + (int)(16u * w) - 1;
    int src_y0 = sy;
    int src_y1 = sy + (int)(16u * h) - 1;
    if (src_x1 < 0 || src_x0 >= (int)RTYPE_GAME_W || src_y1 < 0 || src_y0 >= (int)RTYPE_GAME_H) return;
    if (src_y0 < 0) src_y0 = 0;
    if (src_y1 >= (int)RTYPE_GAME_H) src_y1 = (int)RTYPE_GAME_H - 1;
    unsigned sprite_col0 = RTYPE_BLIT_CYD_ACTIVE_X0 + ((unsigned)src_y0 * RTYPE_BLIT_CYD_VIEW_H) / RTYPE_GAME_H;
    unsigned sprite_col1 = RTYPE_BLIT_CYD_ACTIVE_X0 + (((unsigned)src_y1 + 1u) * RTYPE_BLIT_CYD_VIEW_H + RTYPE_GAME_H - 1u) / RTYPE_GAME_H;
    if (sprite_col0 >= chunk_x + cols || sprite_col1 <= chunk_x) return;

    unsigned color = attr & 0x0fu;

    for (unsigned cell_y = 0; cell_y < h; cell_y++) {
        for (unsigned cell_x = 0; cell_x < w; cell_x++) {
            unsigned c = code + (flipx ? 8u * (w - 1u - cell_x) : 8u * cell_x) +
                         (flipy ? (h - 1u - cell_y) : cell_y);
            for (unsigned py = 0; py < 16u; py++) {
                int raw_y = sy + (int)(cell_y * 16u + py);
                if (raw_y < 0 || raw_y >= (int)RTYPE_GAME_H) continue;
                unsigned phys_col = RTYPE_BLIT_CYD_ACTIVE_X0 + ((unsigned)raw_y * RTYPE_BLIT_CYD_VIEW_H) / RTYPE_GAME_H;
                if (phys_col < chunk_x || phys_col >= chunk_x + cols) continue;
                unsigned local_col = phys_col - chunk_x;
                unsigned ry = flipy ? (15u - py) : py;
                for (unsigned px = 0; px < 16u; px++) {
                    int raw_x = sx + (int)(cell_x * 16u + px) - 64;
                    if (raw_x < 0 || raw_x >= (int)RTYPE_GAME_W) continue;
                    unsigned logical_x = ((unsigned)raw_x * RTYPE_BLIT_CYD_VIEW_W) / RTYPE_GAME_W;
                    unsigned phys_y = (RTYPE_BLIT_CYD_LOGICAL_W - 1u) - logical_x;
                    if (phys_y >= RTYPE_BLIT_CYD_PHYS_H) continue;
                    unsigned rx = flipx ? (15u - px) : px;
                    uint8_t pen = decode_sprite_pixel(video, c, rx, ry);
                    if (pen == 0) continue;
                    unsigned pi = color * 16u + pen;
                    uint16_t out = cyd_wire_rgb565(visible_color(video->palette[pi & 0x1ffu], pi, pen));
                    dst[(size_t)phys_y * cols + local_col] = out;
                }
            }
        }
    }
}

void rtype_m72_video_render_cyd_composited_columns(const rtype_m72_video_t *video, uint16_t *dst,
                                                   unsigned phys_x, unsigned cols) {
    if (video == NULL || dst == NULL || cols == 0 || phys_x >= RTYPE_BLIT_CYD_PHYS_W) return;
    if (phys_x + cols > RTYPE_BLIT_CYD_PHYS_W) cols = RTYPE_BLIT_CYD_PHYS_W - phys_x;
    render_cyd_columns_impl(video, dst, phys_x, cols, false);
    const uint8_t *sprite_ram = video->sprite_buffer_valid ? video->sprite_buffer : video->spriteram;
    if (sprite_ram == NULL) return;
    for (int offs = (int)RTYPE_M72_SPRITERAM_BYTES - 8; offs >= 0; offs -= 8) {
        overlay_sprite_to_cyd_columns(video, dst, phys_x, cols, sprite_ram + offs);
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
