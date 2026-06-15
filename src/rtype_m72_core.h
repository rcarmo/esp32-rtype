#ifndef RTYPE_M72_CORE_H
#define RTYPE_M72_CORE_H

#include "esp_err.h"
#include "rtype_m72_video.h"

#include <stddef.h>
#include <stdint.h>

#define RTYPE_M72_CPU_MAP_BYTES 0x100000u
#define RTYPE_M72_WORK_RAM_BASE 0x40000u
#define RTYPE_M72_WORK_RAM_BYTES 0x4000u
#define RTYPE_M72_SPRITE_RAM_BASE 0xc0000u
#define RTYPE_M72_PALETTE0_BASE 0xc8000u
#define RTYPE_M72_PALETTE1_BASE 0xcc000u
#define RTYPE_M72_VRAM0_BASE 0xd0000u
#define RTYPE_M72_VRAM1_BASE 0xd8000u
#define RTYPE_M72_SOUND_RAM_BASE 0xe0000u
#define RTYPE_M72_RESET_VECTOR_BASE 0xffff0u

typedef struct {
    uint8_t *cpu_map;
    size_t cpu_map_size;
    rtype_m72_video_t video;
    uint64_t mem_reads;
    uint64_t mem_writes;
    uint64_t port_reads;
    uint64_t port_writes;
    uint16_t scrollx[2];
    uint16_t scrolly[2];
    uint16_t raster_irq_position;
    uint8_t video_off;
} rtype_m72_core_t;

esp_err_t rtype_m72_core_init(rtype_m72_core_t *core);
void rtype_m72_core_free(rtype_m72_core_t *core);
esp_err_t rtype_m72_core_map_maincpu(rtype_m72_core_t *core,
                                     const uint8_t *low_pair, size_t low_pair_size,
                                     const uint8_t *high_pair, size_t high_pair_size);
uint8_t rtype_m72_core_read8(rtype_m72_core_t *core, uint32_t addr);
uint16_t rtype_m72_core_read16(rtype_m72_core_t *core, uint32_t addr);
void rtype_m72_core_write8(rtype_m72_core_t *core, uint32_t addr, uint8_t value);
void rtype_m72_core_write16(rtype_m72_core_t *core, uint32_t addr, uint16_t value);
uint8_t rtype_m72_core_in8(rtype_m72_core_t *core, uint16_t port);
uint16_t rtype_m72_core_in16(rtype_m72_core_t *core, uint16_t port);
void rtype_m72_core_out8(rtype_m72_core_t *core, uint16_t port, uint8_t value);
void rtype_m72_core_out16(rtype_m72_core_t *core, uint16_t port, uint16_t value);
void rtype_m72_core_render_frame(rtype_m72_core_t *core, uint16_t *fb);
uint32_t rtype_m72_core_count_nonzero(const rtype_m72_core_t *core, uint32_t begin, uint32_t end);

#endif
