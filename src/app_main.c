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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>

static const char *TAG = "rtype_app";

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

    rtype_m72_core_t core;
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
        for (unsigned frame = 0;; frame++) {
            if (cpu_running) {
                (void)rtype_i86_run(&cpu, 20000);
                if (cpu.halted) {
                    ESP_LOGW(TAG, "CYD CPU halted pc=0x%05" PRIx32 " opcode=0x%02x reason=%s",
                             rtype_i86_pc(&cpu), cpu.last_opcode, cpu.stop_reason);
                    cpu_running = false;
                }
            }
            esp_err_t err = rtype_display_present_boot_pattern(frame);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "no-framebuffer present failed: %s", esp_err_to_name(err));
                rtype_display_heartbeat_loop();
            }
            if ((frame & 0x3fu) == 0 && cpu_running) {
                ESP_LOGI(TAG, "CYD CPU pc=0x%05" PRIx32 " insn=%llu writes=%llu ports=%llu",
                         rtype_i86_pc(&cpu), (unsigned long long)cpu.insn,
                         (unsigned long long)core.mem_writes, (unsigned long long)core.port_writes);
            }
            vTaskDelay(pdMS_TO_TICKS(33));
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
