#include "rtype_display.h"
#include "rtype_board.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_cyd.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "rtype_display_s3";
static bool s_ready;
static uint16_t *s_rgb_fb;

esp_err_t rtype_display_init(void) {
    if (s_ready) return ESP_OK;
    ESP_LOGI(TAG, "initializing S3 RGB panel: physical=%ux%u viewport=%ux%u+%u,%u game=%ux%u scale=%u",
             RTYPE_LCD_W, RTYPE_LCD_H, RTYPE_VIEW_W, RTYPE_VIEW_H, RTYPE_VIEW_X, RTYPE_VIEW_Y,
             RTYPE_GAME_W, RTYPE_GAME_H, RTYPE_VIEW_SCALE);
    lcd_cyd_init();
    void *fb = NULL;
    lcd_get_rgb_framebuffer(&fb);
    s_rgb_fb = (uint16_t *)fb;
    if (s_rgb_fb == NULL) {
        ESP_LOGE(TAG, "RGB framebuffer unavailable after lcd_cyd_init");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "RGB panel framebuffer @%p", (void *)s_rgb_fb);
    s_ready = true;
    return ESP_OK;
}

esp_err_t rtype_display_set_brightness(uint8_t percent) {
    (void)percent;
    return rtype_display_init();
}

static inline uint16_t background_pixel(unsigned x, unsigned y) {
    if (x < 4u || y < 4u || x >= RTYPE_LCD_W - 4u || y >= RTYPE_LCD_H - 4u) return 0xffffu;
    if ((x % 80u) < 2u || (y % 80u) < 2u) return rtype_rgb565(20, 24, 38);
    return rtype_rgb565(2, 4, 10);
}

esp_err_t rtype_display_present_rgb565(const uint16_t *framebuffer, unsigned width, unsigned height) {
    if (framebuffer == NULL || width == 0 || height == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = rtype_display_init();
    if (err != ESP_OK) return err;

    for (unsigned y = 0; y < RTYPE_LCD_H; y++) {
        uint16_t *dst = s_rgb_fb + (size_t)y * RTYPE_LCD_W;
        for (unsigned x = 0; x < RTYPE_LCD_W; x++) {
            uint16_t px = background_pixel(x, y);
            if (x >= RTYPE_VIEW_X && x < RTYPE_VIEW_X + RTYPE_VIEW_W &&
                y >= RTYPE_VIEW_Y && y < RTYPE_VIEW_Y + RTYPE_VIEW_H) {
                const unsigned sx = ((x - RTYPE_VIEW_X) * width) / RTYPE_VIEW_W;
                const unsigned sy = ((y - RTYPE_VIEW_Y) * height) / RTYPE_VIEW_H;
                px = framebuffer[(size_t)sy * width + sx];
            }
            dst[x] = px;
        }
    }
    return ESP_OK;
}

esp_err_t rtype_display_present_boot_pattern(unsigned frame_no) {
    (void)frame_no;
    return ESP_ERR_NOT_SUPPORTED;
}

_Noreturn void rtype_display_heartbeat_loop(void) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
