#include "rtype_display.h"
#include "rtype_blit.h"
#include "rtype_board.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd_cyd.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "rtype_display_cyd";

typedef struct {
    const uint16_t *framebuffer;
    uint32_t sequence;
} rtype_display_job_t;

static bool s_ready;
static uint16_t *s_strip;
static unsigned s_strip_cols;
static QueueHandle_t s_display_queue;
static TaskHandle_t s_display_task;
static uint32_t s_present_sequence;
static volatile uint32_t s_displayed_sequence;
static volatile uint32_t s_dropped_jobs;

static void clear_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0 || s_strip == NULL) return;
    unsigned max_cols = s_strip_cols;
    while (w > 0) {
        unsigned cols = (w > max_cols) ? max_cols : w;
        for (unsigned row = 0; row < h; row++) {
            for (unsigned col = 0; col < cols; col++) s_strip[(size_t)row * cols + col] = 0;
        }
        lcd_draw_bitmap(x, y, cols, h, (const uint8_t *)s_strip);
        x += cols;
        w -= cols;
    }
}

static void rtype_display_clear_bars(void) {
    clear_rect(0, 0, RTYPE_BLIT_CYD_ACTIVE_X0, RTYPE_BLIT_CYD_PHYS_H);
    clear_rect(RTYPE_BLIT_CYD_ACTIVE_X1, 0,
               RTYPE_BLIT_CYD_PHYS_W - RTYPE_BLIT_CYD_ACTIVE_X1, RTYPE_BLIT_CYD_PHYS_H);
    lcd_wait_trans_complete();
}

static void rtype_display_flush_blocking(const uint16_t *framebuffer) {
    for (unsigned x = RTYPE_BLIT_CYD_ACTIVE_X0; x < RTYPE_BLIT_CYD_ACTIVE_X1; x += s_strip_cols) {
        unsigned cols = s_strip_cols;
        if (x + cols > RTYPE_BLIT_CYD_ACTIVE_X1) cols = RTYPE_BLIT_CYD_ACTIVE_X1 - x;
        rtype_blit_cyd_rotate_scale_columns_320x213(framebuffer, s_strip, x, cols);
        lcd_draw_bitmap((uint16_t)x, 0, (uint16_t)cols, RTYPE_BLIT_CYD_PHYS_H, (const uint8_t *)s_strip);
    }
    lcd_wait_trans_complete();
}

static void rtype_display_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "CYD display task started on core %d", xPortGetCoreID());
    rtype_display_job_t job;
    while (true) {
        if (xQueueReceive(s_display_queue, &job, portMAX_DELAY) == pdTRUE) {
            rtype_display_job_t newer;
            while (xQueueReceive(s_display_queue, &newer, 0) == pdTRUE) {
                job = newer;
                s_dropped_jobs++;
            }
            rtype_display_flush_blocking(job.framebuffer);
            s_displayed_sequence = job.sequence;
        }
    }
}

esp_err_t rtype_display_init(void) {
    if (s_ready) return ESP_OK;
    ESP_LOGI(TAG,
             "initializing CYD ILI9341 SPI display: panel=%ux%u source=%ux%u rotated_view=%ux%u active_phys_x=%u..%u RGB565 async_core=0",
             RTYPE_LCD_W, RTYPE_LCD_H, RTYPE_GAME_W, RTYPE_GAME_H,
             RTYPE_BLIT_CYD_VIEW_W, RTYPE_BLIT_CYD_VIEW_H,
             RTYPE_BLIT_CYD_ACTIVE_X0, RTYPE_BLIT_CYD_ACTIVE_X1 - 1u);
    lcd_cyd_init();
    s_strip_cols = 8;
    s_strip = (uint16_t *)heap_caps_malloc((size_t)s_strip_cols * RTYPE_BLIT_CYD_PHYS_H * sizeof(uint16_t),
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_strip == NULL) {
        s_strip = (uint16_t *)heap_caps_malloc((size_t)s_strip_cols * RTYPE_BLIT_CYD_PHYS_H * sizeof(uint16_t),
                                               MALLOC_CAP_8BIT);
    }
    if (s_strip == NULL) {
        ESP_LOGE(TAG, "failed to allocate CYD RGB565 column strip buffer");
        return ESP_ERR_NO_MEM;
    }

    rtype_display_clear_bars();

    s_display_queue = xQueueCreate(2, sizeof(rtype_display_job_t));
    if (s_display_queue == NULL) {
        ESP_LOGE(TAG, "failed to create CYD display queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(rtype_display_task, "rtype-cyd-display", 4096,
                                            NULL, tskIDLE_PRIORITY + 2, &s_display_task, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to start CYD display task; falling back to blocking present");
        s_display_task = NULL;
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

    // Rotated CYD fill path: logical 320x240 landscape, aspect-correct 320x213
    // game viewport, flushed as physical portrait columns to the ILI9341.
    if (s_display_task != NULL && s_display_queue != NULL) {
        rtype_display_job_t job = {
            .framebuffer = framebuffer,
            .sequence = ++s_present_sequence,
        };
        if (xQueueSend(s_display_queue, &job, 0) != pdTRUE) {
            rtype_display_job_t dropped;
            (void)xQueueReceive(s_display_queue, &dropped, 0);
            s_dropped_jobs++;
            (void)xQueueSend(s_display_queue, &job, 0);
        }
        return ESP_OK;
    }

    rtype_display_flush_blocking(framebuffer);
    return ESP_OK;
}

_Noreturn void rtype_display_heartbeat_loop(void) {
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
