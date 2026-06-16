#include "rtype_board.h"
#include "rtype_display.h"
#include "rtype_m72_core.h"
#include "rtype_m72_video.h"
#include "rtype_rom.h"
#include "rtype_video.h"
#include "rtype_i86_cpu.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>

static const char *TAG = "rtype_app";

static uint32_t count_palette_nonzero(const rtype_m72_video_t *video, unsigned begin, unsigned end) {
    if (video == NULL) return 0;
    if (end > RTYPE_M72_PALETTE_COLORS) end = RTYPE_M72_PALETTE_COLORS;
    uint32_t n = 0;
    for (unsigned i = begin; i < end; i++) if (video->palette[i] != 0) n++;
    return n;
}

static void log_system_info(void) {
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "R-Type display-only bring-up starting on %s", RTYPE_BOARD_NAME);
    ESP_LOGI(TAG, "chip model=%d cores=%d revision=%d features=0x%08" PRIx32,
             chip.model, chip.cores, chip.revision, (uint32_t)chip.features);
    ESP_LOGI(TAG, "flash size=%" PRIu32 " bytes", flash_size);
#if CONFIG_SPIRAM
    ESP_LOGI(TAG, "psram size=%zu bytes", esp_psram_get_size());
#else
    ESP_LOGW(TAG, "PSRAM disabled; this target expects PSRAM");
#endif
    ESP_LOGI(TAG, "heap internal free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "heap 8-bit free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void app_main(void) {
    log_system_info();
    rtype_rom_log_expected();

    ESP_ERROR_CHECK(rtype_display_init());
    ESP_ERROR_CHECK(rtype_display_set_brightness(100));

    static rtype_m72_core_t core;
    esp_err_t core_err = rtype_m72_core_init(&core);
    esp_err_t cpu_rom_err = ESP_FAIL;
    if (core_err != ESP_OK) {
        ESP_LOGE(TAG, "no M72 core state (%s); display-only fallback active", esp_err_to_name(core_err));
    } else {
#if defined(RTYPE_BOARD_ESP32_2432S028)
        cpu_rom_err = rtype_m72_core_map_maincpu_partition(&core, "storage");
        if (cpu_rom_err != ESP_OK) {
            ESP_LOGW(TAG, "main CPU ROM partition unavailable (%s); CYD will display embedded frame only", esp_err_to_name(cpu_rom_err));
        } else {
            ESP_LOGI(TAG, "CYD sparse M72 CPU core mapped from storage partition");
        }
#else
        ESP_LOGI(TAG, "Milestone graphics: M72 core + tile/sprite renderer active");
        esp_err_t rom_err = rtype_rom_load_m72_graphics("/spiflash/rtype", &core.video);
        if (rom_err != ESP_OK) {
            ESP_LOGW(TAG, "external graphics ROMs not loaded (%s); using deterministic fallback pixels", esp_err_to_name(rom_err));
        }
#endif
    }

    uint16_t *fb = rtype_video_alloc_framebuffer();
    if (fb == NULL) {
        ESP_LOGW(TAG, "no full source framebuffer; using board-specific no-framebuffer display path");
        rtype_i86_cpu_t cpu;
        bool cpu_running = (core_err == ESP_OK && cpu_rom_err == ESP_OK);
        if (cpu_running) {
            rtype_i86_reset(&cpu, &core);
            ESP_LOGI(TAG, "CYD CPU backend starting without full framebuffer");
        }
        uint64_t next_vblank = 120000;
        bool main_loop_seen = false;
        bool frame_vector_ready = false;
        bool live_video_ready = false;
        uint64_t last_presented_irq = 0;
        uint64_t last_perf_insn = 0;
        uint64_t last_perf_irq = 0;
        int64_t last_perf_us = esp_timer_get_time();
        bool present_due = false;
        for (unsigned frame = 0;; frame++) {
            uint32_t vram0_nz = 0;
            uint32_t vram1_nz = 0;
            uint32_t spr_nz = 0;
            if (cpu_running && !frame_vector_ready) {
                frame_vector_ready = (rtype_m72_core_read16(&core, 0x20u * 4u) == 0x00fe &&
                                      rtype_m72_core_read16(&core, 0x20u * 4u + 2u) == 0x0040);
            }
            present_due = false;
            if (cpu_running) {
                uint64_t target = cpu.insn + 500000ull;
                while (!cpu.halted && cpu.insn < target) {
                    bool in_main_loop = (cpu.s[RTYPE_I86_CS] == 0x0040 && cpu.ip >= 0x00d0 && cpu.ip <= 0x00f8);
                    if (in_main_loop) {
                        main_loop_seen = true;
                        rtype_i86_complete_frame_if_idle(&cpu);
                        if (cpu.interrupt_count > 0 && cpu.interrupt_count >= last_presented_irq + 8u) {
                            present_due = true;
                            break;
                        }
                    }
                    if (main_loop_seen && in_main_loop && frame_vector_ready && cpu.iff && cpu.interrupt_depth == 0) {
                        if (cpu.insn < next_vblank) cpu.insn = next_vblank;
                        rtype_i86_interrupt(&cpu, 0x20);
                        next_vblank += 120000;
                    }
                    rtype_i86_step(&cpu);
                }
                if (cpu.halted) {
                    ESP_LOGW(TAG, "CYD CPU halted pc=0x%05" PRIx32 " opcode=0x%02x reason=%s",
                             rtype_i86_pc(&cpu), cpu.last_opcode, cpu.stop_reason);
                    cpu_running = false;
                }
            }
            if (cpu_running && (frame & 0x0fu) == 0) {
                vram0_nz = rtype_m72_core_count_nonzero(&core, RTYPE_M72_VRAM0_BASE, RTYPE_M72_VRAM0_BASE + RTYPE_M72_VRAM_BYTES);
                vram1_nz = rtype_m72_core_count_nonzero(&core, RTYPE_M72_VRAM1_BASE, RTYPE_M72_VRAM1_BASE + RTYPE_M72_VRAM_BYTES);
                spr_nz = rtype_m72_core_count_nonzero(&core, RTYPE_M72_SPRITE_RAM_BASE, RTYPE_M72_SPRITE_RAM_BASE + RTYPE_M72_SPRITERAM_BYTES);
                live_video_ready = !core.video.video_off && cpu.interrupt_count > 0 &&
                                   (vram0_nz != 0 || vram1_nz != 0 || spr_nz != 0);
            }
            esp_err_t err = ESP_OK;
            if (live_video_ready && present_due) {
                err = rtype_display_present_m72_core(&core);
                last_presented_irq = cpu.interrupt_count;
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "no-framebuffer present failed: %s", esp_err_to_name(err));
                rtype_display_heartbeat_loop();
            }
            if (cpu_running) {
                int64_t now_us = esp_timer_get_time();
                if (now_us - last_perf_us >= 5000000) {
                    uint64_t dinsn = cpu.insn - last_perf_insn;
                    uint64_t dirq = cpu.interrupt_count - last_perf_irq;
                    uint64_t dt_us = (uint64_t)(now_us - last_perf_us);
                    ESP_LOGI(TAG, "CYD PERF pc=0x%05" PRIx32 " ips=%llu irq_s=%llu game_frame=0x%04x live=%d vram=%u/%u spr=%u",
                             rtype_i86_pc(&cpu),
                             (unsigned long long)((dinsn * 1000000ull) / dt_us),
                             (unsigned long long)((dirq * 1000000ull) / dt_us),
                             (unsigned)rtype_m72_core_read16(&core, 0x42eb4u),
                             live_video_ready ? 1 : 0, (unsigned)vram0_nz, (unsigned)vram1_nz, (unsigned)spr_nz);
                    last_perf_insn = cpu.insn;
                    last_perf_irq = cpu.interrupt_count;
                    last_perf_us = now_us;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    uint16_t *fb_next = rtype_video_alloc_framebuffer();
    if (fb_next != NULL) {
        ESP_LOGI(TAG, "double framebuffer enabled for async display handoff");
    } else {
        ESP_LOGW(TAG, "single framebuffer only; display producer will throttle after present");
    }

    for (unsigned frame = 0;; frame++) {
        if (core_err == ESP_OK) {
            rtype_m72_video_seed_probe_scene(&core.video, frame);
            rtype_m72_core_render_frame(&core, fb);
        } else {
            rtype_video_render_boot_pattern(fb, frame);
        }
        esp_err_t err = rtype_display_present_rgb565(fb, RTYPE_GAME_W, RTYPE_GAME_H);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "present failed: %s", esp_err_to_name(err));
        }
        if (fb_next != NULL) {
            uint16_t *tmp = fb;
            fb = fb_next;
            fb_next = tmp;
            vTaskDelay(pdMS_TO_TICKS(33));
        } else {
            // Give the async CYD display worker time to finish reading the only
            // source buffer before the next render overwrites it.
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
