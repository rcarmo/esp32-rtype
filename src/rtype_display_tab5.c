#include "rtype_display.h"
#include "rtype_board.h"

#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "rtype_display";

#define TAB5_GT911_ADDR 0x14
#define TAB5_ST7123_ADDR 0x55

static bsp_lcd_handles_t s_handles;
static bool s_display_ready;

static int detect_st7123_panel(void) {
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) return 0;
    esp_err_t st7123 = i2c_master_probe(bus, TAB5_ST7123_ADDR, 50);
    esp_err_t gt911 = i2c_master_probe(bus, TAB5_GT911_ADDR, 50);
    ESP_LOGI(TAG, "panel probe: ST7123@0x%02x=%s GT911@0x%02x=%s",
             TAB5_ST7123_ADDR, esp_err_to_name(st7123), TAB5_GT911_ADDR, esp_err_to_name(gt911));
    return st7123 == ESP_OK;
}

esp_err_t rtype_display_init(void) {
    if (s_display_ready) return ESP_OK;

    ESP_LOGI(TAG, "initializing Tab5 display: physical=%ux%u viewport=%ux%u+%u,%u game=%ux%u scale=%u",
             RTYPE_LCD_W, RTYPE_LCD_H, RTYPE_VIEW_W, RTYPE_VIEW_H, RTYPE_VIEW_X, RTYPE_VIEW_Y,
             RTYPE_GAME_W, RTYPE_GAME_H, RTYPE_VIEW_SCALE);

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "bsp_i2c_init failed");
    bsp_io_expander_pi4ioe_init(bsp_i2c_get_handle());
    bsp_set_charge_qc_en(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_set_charge_en(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_reset_tp();
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_err_t err;
    if (detect_st7123_panel()) {
        ESP_LOGI(TAG, "using ST7123 Tab5 display path");
        err = bsp_display_new_with_handles_to_st7123(NULL, &s_handles);
    } else {
        ESP_LOGI(TAG, "using ILI9881C/ST7703-compatible Tab5 display path");
        err = bsp_display_new_with_handles(NULL, &s_handles);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "BSP display init failed");
    s_display_ready = true;
    return ESP_OK;
}

esp_err_t rtype_display_set_brightness(uint8_t percent) {
    if (percent > 100u) percent = 100u;
    ESP_RETURN_ON_ERROR(rtype_display_init(), TAG, "display init before brightness failed");
    return bsp_display_brightness_set(percent);
}

static uint16_t border_pixel(unsigned x, unsigned y) {
    if (x < 8u || y < 8u || x >= RTYPE_LCD_W - 8u || y >= RTYPE_LCD_H - 8u) return 0xffffu;
    if ((x % 120u) < 2u || (y % 120u) < 2u) return rtype_rgb565(24, 28, 42);
    return rtype_rgb565(3, 5, 12);
}

esp_err_t rtype_display_present_rgb565(const uint16_t *framebuffer, unsigned width, unsigned height) {
    if (framebuffer == NULL || width == 0 || height == 0) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(rtype_display_init(), TAG, "display init before present failed");

    const size_t strip_pixels = (size_t)RTYPE_LCD_W * RTYPE_STRIP_ROWS;
    uint16_t *strip = heap_caps_malloc(strip_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (strip == NULL) {
        strip = heap_caps_malloc(strip_pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (strip == NULL) {
        ESP_LOGE(TAG, "failed to allocate %u-row LCD strip", RTYPE_STRIP_ROWS);
        return ESP_ERR_NO_MEM;
    }

    for (unsigned y0 = 0; y0 < RTYPE_LCD_H; y0 += RTYPE_STRIP_ROWS) {
        const unsigned rows = (y0 + RTYPE_STRIP_ROWS <= RTYPE_LCD_H) ? RTYPE_STRIP_ROWS : (RTYPE_LCD_H - y0);
        for (unsigned row = 0; row < rows; row++) {
            const unsigned py = y0 + row;
            for (unsigned x = 0; x < RTYPE_LCD_W; x++) {
                uint16_t px = border_pixel(x, py);
                if (x >= RTYPE_VIEW_X && x < RTYPE_VIEW_X + RTYPE_VIEW_W &&
                    py >= RTYPE_VIEW_Y && py < RTYPE_VIEW_Y + RTYPE_VIEW_H) {
                    const unsigned sx = ((x - RTYPE_VIEW_X) * width) / RTYPE_VIEW_W;
                    const unsigned sy = ((py - RTYPE_VIEW_Y) * height) / RTYPE_VIEW_H;
                    px = framebuffer[(size_t)sy * width + sx];
                }
                strip[(size_t)row * RTYPE_LCD_W + x] = px;
            }
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_handles.panel, 0, y0, RTYPE_LCD_W, y0 + rows, strip),
                            TAG, "panel draw failed");
    }

    heap_caps_free(strip);
    return ESP_OK;
}

_Noreturn void rtype_display_heartbeat_loop(void) {
    uint8_t levels[] = {30, 100, 60, 100};
    unsigned i = 0;
    while (true) {
        rtype_display_set_brightness(levels[i++ % (sizeof(levels) / sizeof(levels[0]))]);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}
