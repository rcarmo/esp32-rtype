#include "rtype_m72_core.h"

#include "esp_check.h"
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

static esp_err_t alloc_sparse_region(uint8_t **ptr, size_t bytes, const char *name) {
    *ptr = rtype_m72_alloc_region(bytes, name);
    if (*ptr == NULL) return ESP_ERR_NO_MEM;
    memset(*ptr, 0, bytes);
    return ESP_OK;
}

esp_err_t rtype_m72_core_init(rtype_m72_core_t *core) {
    if (core == NULL) return ESP_ERR_INVALID_ARG;
    memset(core, 0, sizeof(*core));
    rtype_m72_video_init(&core->video);
    core->cpu_map_size = RTYPE_M72_CPU_MAP_BYTES;
    core->cpu_map = rtype_m72_alloc_region(core->cpu_map_size, "m72-cpu-map");
    if (core->cpu_map != NULL) {
        memset(core->cpu_map, 0xff, core->cpu_map_size);
        core->video.vram0 = core->cpu_map + RTYPE_M72_VRAM0_BASE;
        core->video.vram1 = core->cpu_map + RTYPE_M72_VRAM1_BASE;
        core->video.spriteram = core->cpu_map + RTYPE_M72_SPRITE_RAM_BASE;
        ESP_LOGI(TAG, "M72 core initialized: flat cpu_map=%u bytes", (unsigned)core->cpu_map_size);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "flat 1MB CPU map unavailable; using sparse no-PSRAM M72 memory map");
    core->sparse_mode = 1;
    ESP_RETURN_ON_ERROR(alloc_sparse_region(&core->work_ram, RTYPE_M72_WORK_RAM_BYTES, "m72-work"), TAG, "work alloc");
    ESP_RETURN_ON_ERROR(alloc_sparse_region(&core->sprite_ram, RTYPE_M72_SPRITERAM_BYTES, "m72-sprite"), TAG, "sprite alloc");
    ESP_RETURN_ON_ERROR(alloc_sparse_region(&core->palette0_ram, 0x0c00u, "m72-pal0"), TAG, "pal0 alloc");
    ESP_RETURN_ON_ERROR(alloc_sparse_region(&core->palette1_ram, 0x0c00u, "m72-pal1"), TAG, "pal1 alloc");
    ESP_RETURN_ON_ERROR(alloc_sparse_region(&core->vram0, RTYPE_M72_VRAM_BYTES, "m72-vram0"), TAG, "vram0 alloc");
    ESP_RETURN_ON_ERROR(alloc_sparse_region(&core->vram1, RTYPE_M72_VRAM_BYTES, "m72-vram1"), TAG, "vram1 alloc");
    ESP_RETURN_ON_ERROR(alloc_sparse_region(&core->sound_ram, 0x10000u, "m72-sound"), TAG, "sound alloc");
    core->video.vram0 = core->vram0;
    core->video.vram1 = core->vram1;
    core->video.spriteram = core->sprite_ram;
    ESP_LOGI(TAG, "M72 sparse core initialized");
    return ESP_OK;
}

void rtype_m72_core_free(rtype_m72_core_t *core) {
    if (core == NULL) return;
    if (core->rom_mmap_handle) esp_partition_munmap(core->rom_mmap_handle);
    if (core->cpu_map != NULL) heap_caps_free(core->cpu_map);
    if (core->work_ram != NULL) heap_caps_free(core->work_ram);
    if (core->sprite_ram != NULL) heap_caps_free(core->sprite_ram);
    if (core->palette0_ram != NULL) heap_caps_free(core->palette0_ram);
    if (core->palette1_ram != NULL) heap_caps_free(core->palette1_ram);
    if (core->vram0 != NULL) heap_caps_free(core->vram0);
    if (core->vram1 != NULL) heap_caps_free(core->vram1);
    if (core->sound_ram != NULL) heap_caps_free(core->sound_ram);
    if (!core->sparse_mode && core->video.sprites != NULL) heap_caps_free((void *)core->video.sprites);
    if (!core->sparse_mode && core->video.tiles0 != NULL) heap_caps_free((void *)core->video.tiles0);
    if (!core->sparse_mode && core->video.tiles1 != NULL) heap_caps_free((void *)core->video.tiles1);
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

esp_err_t rtype_m72_core_map_maincpu_partition(rtype_m72_core_t *core, const char *label) {
    if (core == NULL || label == NULL) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part == NULL) {
        ESP_LOGW(TAG, "maincpu partition '%s' not found", label);
        return ESP_ERR_NOT_FOUND;
    }
    if (part->size < RTYPE_M72_CPU_MAP_BYTES) {
        ESP_LOGE(TAG, "partition '%s' too small: %u", label, (unsigned)part->size);
        return ESP_ERR_INVALID_SIZE;
    }
    if (core->rom_mmap_handle) {
        esp_partition_munmap(core->rom_mmap_handle);
        core->rom_mmap_handle = 0;
        core->rom_map = NULL;
    }
    const void *mapped = NULL;
    ESP_RETURN_ON_ERROR(esp_partition_mmap(part, 0, RTYPE_M72_CPU_MAP_BYTES,
                                           ESP_PARTITION_MMAP_DATA, &mapped,
                                           &core->rom_mmap_handle), TAG, "mmap maincpu");
    core->rom_map = (const uint8_t *)mapped;
    core->rom_map_size = RTYPE_M72_CPU_MAP_BYTES;
    if (core->sparse_mode) {
        core->video.sprites = core->rom_map + 0x40000u;
        core->video.sprites_size = 0x80000u;
        core->video.tiles0 = core->rom_map + 0xc0000u;
        core->video.tiles0_size = 0x20000u;
        core->video.tiles1 = core->rom_map + 0xe0000u;
        core->video.tiles1_size = 0x20000u;
    }
    ESP_LOGI(TAG, "mapped CYD packed ROM partition '%s' @%p", label, mapped);
    return ESP_OK;
}

static uint8_t sparse_read8(rtype_m72_core_t *core, uint32_t addr) {
    addr &= 0xfffffu;
    if (core->rom_map != NULL) {
        if (addr <= 0x3ffffu) return core->rom_map[addr];
        if (addr >= RTYPE_M72_RESET_VECTOR_BASE) return core->rom_map[0x20000u + (addr - 0xe0000u)];
    }
    if (addr >= RTYPE_M72_WORK_RAM_BASE && addr < RTYPE_M72_WORK_RAM_BASE + RTYPE_M72_WORK_RAM_BYTES) return core->work_ram[addr - RTYPE_M72_WORK_RAM_BASE];
    if (addr >= RTYPE_M72_SPRITE_RAM_BASE && addr < RTYPE_M72_SPRITE_RAM_BASE + RTYPE_M72_SPRITERAM_BYTES) return core->sprite_ram[addr - RTYPE_M72_SPRITE_RAM_BASE];
    if (addr >= RTYPE_M72_PALETTE0_BASE && addr < RTYPE_M72_PALETTE0_BASE + 0x0c00u) return core->palette0_ram[addr - RTYPE_M72_PALETTE0_BASE];
    if (addr >= RTYPE_M72_PALETTE1_BASE && addr < RTYPE_M72_PALETTE1_BASE + 0x0c00u) return core->palette1_ram[addr - RTYPE_M72_PALETTE1_BASE];
    if (addr >= RTYPE_M72_VRAM0_BASE && addr < RTYPE_M72_VRAM0_BASE + RTYPE_M72_VRAM_BYTES) return core->vram0[addr - RTYPE_M72_VRAM0_BASE];
    if (addr >= RTYPE_M72_VRAM1_BASE && addr < RTYPE_M72_VRAM1_BASE + RTYPE_M72_VRAM_BYTES) return core->vram1[addr - RTYPE_M72_VRAM1_BASE];
    if (addr >= RTYPE_M72_SOUND_RAM_BASE && addr < RTYPE_M72_SOUND_RAM_BASE + 0x10000u) return core->sound_ram[addr - RTYPE_M72_SOUND_RAM_BASE];
    return 0xff;
}

uint8_t rtype_m72_core_read8(rtype_m72_core_t *core, uint32_t addr) {
    if (core == NULL) return 0xff;
    if (core->cpu_map != NULL) return core->cpu_map[addr & 0xfffffu];
    if (core->sparse_mode) return sparse_read8(core, addr);
    return 0xff;
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
    uint16_t r = rtype_m72_core_read16(core, base + ((color + 0x000u) << 1));
    uint16_t g = rtype_m72_core_read16(core, base + ((color + 0x200u) << 1));
    uint16_t b = rtype_m72_core_read16(core, base + ((color + 0x400u) << 1));
    core->video.palette[color + (group << 8)] = rtype_rgb565(pal5(r), pal5(g), pal5(b));
}

static uint8_t *sparse_ptr_for_write(rtype_m72_core_t *core, uint32_t addr) {
    if (addr >= RTYPE_M72_WORK_RAM_BASE && addr < RTYPE_M72_WORK_RAM_BASE + RTYPE_M72_WORK_RAM_BYTES) return core->work_ram + (addr - RTYPE_M72_WORK_RAM_BASE);
    if (addr >= RTYPE_M72_SPRITE_RAM_BASE && addr < RTYPE_M72_SPRITE_RAM_BASE + RTYPE_M72_SPRITERAM_BYTES) return core->sprite_ram + (addr - RTYPE_M72_SPRITE_RAM_BASE);
    if (addr >= RTYPE_M72_PALETTE0_BASE && addr < RTYPE_M72_PALETTE0_BASE + 0x0c00u) return core->palette0_ram + (addr - RTYPE_M72_PALETTE0_BASE);
    if (addr >= RTYPE_M72_PALETTE1_BASE && addr < RTYPE_M72_PALETTE1_BASE + 0x0c00u) return core->palette1_ram + (addr - RTYPE_M72_PALETTE1_BASE);
    if (addr >= RTYPE_M72_VRAM0_BASE && addr < RTYPE_M72_VRAM0_BASE + RTYPE_M72_VRAM_BYTES) return core->vram0 + (addr - RTYPE_M72_VRAM0_BASE);
    if (addr >= RTYPE_M72_VRAM1_BASE && addr < RTYPE_M72_VRAM1_BASE + RTYPE_M72_VRAM_BYTES) return core->vram1 + (addr - RTYPE_M72_VRAM1_BASE);
    if (addr >= RTYPE_M72_SOUND_RAM_BASE && addr < RTYPE_M72_SOUND_RAM_BASE + 0x10000u) return core->sound_ram + (addr - RTYPE_M72_SOUND_RAM_BASE);
    return NULL;
}

void rtype_m72_core_write8(rtype_m72_core_t *core, uint32_t addr, uint8_t value) {
    if (core == NULL) return;
    addr &= 0xfffffu;
    if (addr <= 0x3ffffu || addr >= RTYPE_M72_RESET_VECTOR_BASE) return;
    if (core->cpu_map != NULL) {
        core->cpu_map[addr] = value;
    } else if (core->sparse_mode) {
        uint8_t *p = sparse_ptr_for_write(core, addr);
        if (p == NULL) return;
        *p = value;
    } else {
        return;
    }
    if (addr >= RTYPE_M72_PALETTE0_BASE && addr <= 0xc8bffu) refresh_palette_group(core, 0, addr);
    if (addr >= RTYPE_M72_PALETTE1_BASE && addr <= 0xccbffu) refresh_palette_group(core, 1, addr);
}

void rtype_m72_core_write16(rtype_m72_core_t *core, uint32_t addr, uint16_t value) {
    rtype_m72_core_write8(core, addr, (uint8_t)(value & 0xffu));
    rtype_m72_core_write8(core, addr + 1u, (uint8_t)(value >> 8));
}

uint16_t rtype_m72_core_in16(rtype_m72_core_t *core, uint16_t port) {
    switch (port & 0xffu) {
    case 0x00: return 0xffffu;
    case 0x02: return 0xffffu;
    case 0x04: return 0xfdfbu; // R-Type MAME default DSW
    case 0x40: return 0x0000u;
    case 0x42: return 0x0000u;
    default: return 0xffffu;
    }
}

uint8_t rtype_m72_core_in8(rtype_m72_core_t *core, uint16_t port) {
    uint16_t v = rtype_m72_core_in16(core, port);
    return (port & 1u) ? (uint8_t)(v >> 8) : (uint8_t)(v & 0xffu);
}

static void latch_sprites(rtype_m72_core_t *core) {
    if (core == NULL || core->video.spriteram == NULL) return;
    memcpy(core->video.sprite_buffer, core->video.spriteram, RTYPE_M72_SPRITERAM_BYTES);
    core->video.sprite_buffer_valid = 1;
}

void rtype_m72_core_out8(rtype_m72_core_t *core, uint16_t port, uint8_t value) {
    if (core == NULL) return;
    switch (port & 0xffu) {
    case 0x02: core->video_off = (value & 0x08u) != 0; core->video.video_off = core->video_off; break;
    case 0x04: latch_sprites(core); break;
    default: break;
    }
}

void rtype_m72_core_out16(rtype_m72_core_t *core, uint16_t port, uint16_t value) {
    if (core == NULL) return;
    switch (port & 0xffu) {
    case 0x02:
        core->video_off = (value & 0x0008u) != 0;
        core->video.video_off = core->video_off;
        break;
    case 0x04:
        latch_sprites(core);
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
    if (core == NULL) return 0;
    uint32_t n = 0;
    for (uint32_t a = begin; a < end; a++) {
        uint8_t v = 0xff;
        if (core->cpu_map != NULL) v = core->cpu_map[a & 0xfffffu];
        else if (core->sparse_mode) v = sparse_read8((rtype_m72_core_t *)core, a);
        if (v != 0) n++;
    }
    return n;
}
