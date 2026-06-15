#include "rtype_display.h"
#include "rtype_blit.h"
#include "rtype_board.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_cyd.h"

#include <stdbool.h>

static const char *TAG = "rtype_display_cyd";
static bool s_ready;
static uint16_t *s_strip;
static unsigned s_strip_rows;

esp_err_t rtype_display_init(void) {
    if (s_ready) return ESP_OK;
    ESP_LOGI(TAG,
             "initializing CYD ILI9341 SPI display: panel=%ux%u source=%ux%u physical_view=%ux%u+y%u RGB565 downscale=5/8",
             RTYPE_LCD_W, RTYPE_LCD_H, RTYPE_GAME_W, RTYPE_GAME_H,
             RTYPE_BLIT_CYD_VIEW_W, RTYPE_BLIT_CYD_VIEW_H, RTYPE_BLIT_CYD_VIEW_Y);
    lcd_cyd_init();
    s_strip_rows = 8;
    s_strip = (uint16_t *)heap_caps_malloc((size_t)RTYPE_BLIT_CYD_PHYS_W * s_strip_rows * sizeof(uint16_t),
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_strip == NULL) {
        s_strip = (uint16_t *)heap_caps_malloc((size_t)RTYPE_BLIT_CYD_PHYS_W * s_strip_rows * sizeof(uint16_t),
                                               MALLOC_CAP_8BIT);
    }
    if (s_strip == NULL) {
        ESP_LOGE(TAG, "failed to allocate CYD RGB565 strip buffer");
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

esp_err_t rtype_display_set_brightness(uint8_t percent) {
    (void)percent;
    return rtype_display_init();
}

esp_err_t rtype_display_present_rgb565(const uint16_t *framebuffer, unsigned width, unsigned height) {
    if (framebuffer == NULL || width != RTYPE_GAME_W || height != RTYPE_GAME_H) return ESP_ERR_INVALID_ARG;
    esp_err_t err = rtype_display_init();
    if (err != ESP_OK) return err;

    // Small CYD path: 384x256 RGB565 -> 240x160 RGB565, centered vertically
    // on the 240x320 portrait ILI9341. This preserves aspect ratio, avoids
    // rotation bandwidth, and sends 76.8KB of active game pixels per full frame.
    for (unsigned y = 0; y < RTYPE_BLIT_CYD_PHYS_H; y += s_strip_rows) {
        unsigned rows = s_strip_rows;
        if (y + rows > RTYPE_BLIT_CYD_PHYS_H) rows = RTYPE_BLIT_CYD_PHYS_H - y;
        rtype_blit_cyd_scale_strip_240x160(framebuffer, s_strip, y, rows);
        lcd_draw_bitmap(0, (uint16_t)y, RTYPE_BLIT_CYD_PHYS_W, (uint16_t)rows, (const uint8_t *)s_strip);
    }
    lcd_wait_trans_complete();
    return ESP_OK;
}

_Noreturn void rtype_display_heartbeat_loop(void) {
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
