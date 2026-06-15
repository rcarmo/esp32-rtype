#include "rtype_board.h"
#include "rtype_display.h"
#include "rtype_rom.h"
#include "rtype_video.h"

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

    uint16_t *fb = rtype_video_alloc_framebuffer();
    if (fb == NULL) {
        ESP_LOGE(TAG, "no framebuffer; leaving brightness heartbeat active");
        rtype_display_heartbeat_loop();
    }

    ESP_LOGI(TAG, "Milestone 0: animated display path only; CPU/input/audio not wired yet");
    for (unsigned frame = 0;; frame++) {
        rtype_video_render_boot_pattern(fb, frame);
        esp_err_t err = rtype_display_present_rgb565(fb, RTYPE_GAME_W, RTYPE_GAME_H);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "present failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
