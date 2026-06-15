#include "rtype_m72_core.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "rtype_core";

static uint16_t read16le_ptr(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void write16le_ptr(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static uint8_t pal5(uint16_t v) {
    v &= 0x1fu;
    return (uint8_t)((v << 3) | (v >> 2));
}

esp_err_t rtype_m72_core_init(rtype_m72_core_t *core) {
    if (core == NULL) return ESP_ERR_INVALID_ARG;
    memset(core, 0, sizeof(*core));
    rtype_m72_video_init(&core->video);
    core->cpu_map_size = RTYPE_M72_CPU_MAP_BYTES;
    core->cpu_map = rtype_m72_alloc_region(core->cpu_map_size, "m72-cpu-map");
    if (core->cpu_map == NULL) return ESP_ERR_NO_MEM;
    memset(core->cpu_map, 0xff, core->cpu_map_size);
    core->video.vram0 = core->cpu_map + RTYPE_M72_VRAM0_BASE;
    core->video.vram1 = core->cpu_map + RTYPE_M72_VRAM1_BASE;
    core->video.spriteram = core->cpu_map + RTYPE_M72_SPRITE_RAM_BASE;
    ESP_LOGI(TAG, "M72 core initialized: cpu_map=%u bytes", (unsigned)core->cpu_map_size);
    return ESP_OK;
}

void rtype_m72_core_free(rtype_m72_core_t *core) {
    if (core == NULL) return;
    if (core->cpu_map != NULL) heap_caps_free(core->cpu_map);
    if (core->video.sprites != NULL) heap_caps_free((void *)core->video.sprites);
    if (core->video.tiles0 != NULL) heap_caps_free((void *)core->video.tiles0);
    if (core->video.tiles1 != NULL) heap_caps_free((void *)core->video.tiles1);
    memset(core, 0, sizeof(*core));
}

esp_err_t rtype_m72_core_map_maincpu(rtype_m72_core_t *core,
                                     const uint8_t *low_pair, size_t low_pair_size,
                                     const uint8_t *high_pair, size_t high_pair_size) {
    if (core == NULL || core->cpu_map == NULL || low_pair == NULL || high_pair == NULL ||
        low_pair_size != 0x20000u || high_pair_size != 0x20000u) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(core->cpu_map, 0xff, core->cpu_map_size);
    memcpy(core->cpu_map + 0x00000u, low_pair, low_pair_size);
    memcpy(core->cpu_map + 0x20000u, high_pair, high_pair_size);
    memcpy(core->cpu_map + 0xe0000u, high_pair, high_pair_size);
    core->video.vram0 = core->cpu_map + RTYPE_M72_VRAM0_BASE;
    core->video.vram1 = core->cpu_map + RTYPE_M72_VRAM1_BASE;
    core->video.spriteram = core->cpu_map + RTYPE_M72_SPRITE_RAM_BASE;
    ESP_LOGI(TAG, "M72 main CPU map loaded with reset mirror");
    return ESP_OK;
}

uint8_t rtype_m72_core_read8(rtype_m72_core_t *core, uint32_t addr) {
    if (core == NULL || core->cpu_map == NULL) return 0xff;
    core->mem_reads++;
    return core->cpu_map[addr & 0xfffffu];
}

uint16_t rtype_m72_core_read16(rtype_m72_core_t *core, uint32_t addr) {
    uint16_t lo = rtype_m72_core_read8(core, addr);
    uint16_t hi = rtype_m72_core_read8(core, addr + 1u);
    return (uint16_t)(lo | (hi << 8));
}

static void refresh_palette_group(rtype_m72_core_t *core, unsigned group, uint32_t byte_addr) {
    uint32_t base = group ? RTYPE_M72_PALETTE1_BASE : RTYPE_M72_PALETTE0_BASE;
    uint32_t word_off = ((byte_addr - base) >> 1) & ~0x100u;
    uint32_t color = word_off & 0x0ffu;
    uint16_t r = read16le_ptr(core->cpu_map + base + ((color + 0x000u) << 1));
    uint16_t g = read16le_ptr(core->cpu_map + base + ((color + 0x200u) << 1));
    uint16_t b = read16le_ptr(core->cpu_map + base + ((color + 0x400u) << 1));
    core->video.palette[color + (group << 8)] = rtype_rgb565(pal5(r), pal5(g), pal5(b));
}

void rtype_m72_core_write8(rtype_m72_core_t *core, uint32_t addr, uint8_t value) {
    if (core == NULL || core->cpu_map == NULL) return;
    addr &= 0xfffffu;
    if (addr <= 0x3ffffu || addr >= RTYPE_M72_RESET_VECTOR_BASE) return;
    core->cpu_map[addr] = value;
    core->mem_writes++;
    if (addr >= RTYPE_M72_PALETTE0_BASE && addr <= 0xc8bffu) refresh_palette_group(core, 0, addr);
    if (addr >= RTYPE_M72_PALETTE1_BASE && addr <= 0xccbffu) refresh_palette_group(core, 1, addr);
}

void rtype_m72_core_write16(rtype_m72_core_t *core, uint32_t addr, uint16_t value) {
    rtype_m72_core_write8(core, addr, (uint8_t)(value & 0xffu));
    rtype_m72_core_write8(core, addr + 1u, (uint8_t)(value >> 8));
}

uint16_t rtype_m72_core_in16(rtype_m72_core_t *core, uint16_t port) {
    if (core) core->port_reads++;
    switch (port & 0xffu) {
    case 0x00: return 0xffffu;
    case 0x02: return 0xffffu;
    case 0x04: return 0xffffu;
    case 0x40: return 0x0000u;
    case 0x42: return 0x0000u;
    default: return 0xffffu;
    }
}

uint8_t rtype_m72_core_in8(rtype_m72_core_t *core, uint16_t port) {
    uint16_t v = rtype_m72_core_in16(core, port);
    return (port & 1u) ? (uint8_t)(v >> 8) : (uint8_t)(v & 0xffu);
}

void rtype_m72_core_out8(rtype_m72_core_t *core, uint16_t port, uint8_t value) {
    if (core == NULL) return;
    core->port_writes++;
    switch (port & 0xffu) {
    case 0x02: core->video_off = (value & 0x08u) != 0; core->video.video_off = core->video_off; break;
    default: break;
    }
}

void rtype_m72_core_out16(rtype_m72_core_t *core, uint16_t port, uint16_t value) {
    if (core == NULL) return;
    core->port_writes++;
    switch (port & 0xffu) {
    case 0x02:
        core->video_off = (value & 0x0008u) != 0;
        core->video.video_off = core->video_off;
        break;
    case 0x06:
        core->raster_irq_position = (uint16_t)((value & 0x01ffu) - 128u);
        break;
    case 0x80: core->scrolly[0] = value; core->video.scrolly[0] = value; break;
    case 0x82: core->scrollx[0] = value; core->video.scrollx[0] = value; break;
    case 0x84: core->scrolly[1] = value; core->video.scrolly[1] = value; break;
    case 0x86: core->scrollx[1] = value; core->video.scrollx[1] = value; break;
    default: break;
    }
}

void rtype_m72_core_render_frame(rtype_m72_core_t *core, uint16_t *fb) {
    if (core == NULL) return;
    rtype_m72_video_render(&core->video, fb);
}

uint32_t rtype_m72_core_count_nonzero(const rtype_m72_core_t *core, uint32_t begin, uint32_t end) {
    if (core == NULL || core->cpu_map == NULL) return 0;
    uint32_t n = 0;
    for (uint32_t a = begin; a < end; a++) if (core->cpu_map[a & 0xfffffu] != 0) n++;
    return n;
}
