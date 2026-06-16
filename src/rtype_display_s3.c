#include "rtype_display.h"
#include "rtype_board.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd_cyd.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "rtype_display_s3";

#define S3_SNAPSHOT_COUNT 2u
#define S3_DIRECT_FRAMEBUFFER_HANDOFF 0

typedef struct {
    const uint16_t *framebuffer;
    unsigned snapshot_index;
    uint32_t sequence;
} s3_display_job_t;

static bool s_ready;
static uint16_t *s_rgb_fb;
static uint16_t *s_snapshots[S3_SNAPSHOT_COUNT];
static volatile bool s_snapshot_busy[S3_SNAPSHOT_COUNT];
static unsigned s_next_snapshot;
static QueueHandle_t s_display_queue;
static TaskHandle_t s_display_task;
static uint32_t s_present_sequence;
static volatile uint32_t s_displayed_sequence;
static volatile uint32_t s_dropped_jobs;
#define S3_VIEW_W 720u
#define S3_VIEW_H 480u
#define S3_VIEW_X ((RTYPE_LCD_W - S3_VIEW_W) / 2u)
#define S3_VIEW_Y 0u

static uint16_t s_x_run_begin[RTYPE_GAME_W];
static uint16_t s_x_run_end[RTYPE_GAME_W];
static uint16_t s_y_lut[S3_VIEW_H];
static bool s_luts_ready;

static void init_scale_luts(void) {
    if (s_luts_ready) return;
    for (unsigned sx = 0; sx < RTYPE_GAME_W; sx++) {
        s_x_run_begin[sx] = 0xffffu;
        s_x_run_end[sx] = 0;
    }
    for (unsigned x = 0; x < S3_VIEW_W; x++) {
        unsigned sx = (x * RTYPE_GAME_W) / S3_VIEW_W;
        if (sx >= RTYPE_GAME_W) sx = RTYPE_GAME_W - 1u;
        if (s_x_run_begin[sx] == 0xffffu) s_x_run_begin[sx] = (uint16_t)x;
        s_x_run_end[sx] = (uint16_t)(x + 1u);
    }
    for (unsigned y = 0; y < S3_VIEW_H; y++) {
        unsigned sy = (y * RTYPE_GAME_H) / S3_VIEW_H;
        if (sy >= RTYPE_GAME_H) sy = RTYPE_GAME_H - 1u;
        s_y_lut[y] = (uint16_t)sy;
    }
    s_luts_ready = true;
}

static void blit_game_to_panel(const uint16_t *src) {
    if (src == NULL || s_rgb_fb == NULL) return;
    init_scale_luts();
    for (unsigned y = 0; y < S3_VIEW_H; y++) {
        const uint16_t *src_row = src + (size_t)s_y_lut[y] * RTYPE_GAME_W;
        uint16_t *dst = s_rgb_fb + (size_t)(S3_VIEW_Y + y) * RTYPE_LCD_W + S3_VIEW_X;
        for (unsigned sx = 0; sx < RTYPE_GAME_W; sx++) {
            uint16_t begin = s_x_run_begin[sx];
            uint16_t end = s_x_run_end[sx];
            if (begin == 0xffffu) continue;
            uint16_t px = src_row[sx];
            for (unsigned x = begin; x < end; x++) dst[x] = px;
        }
    }
}

static void s3_display_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "S3 display task started on core %d", xPortGetCoreID());
    s3_display_job_t job;
    while (true) {
        if (xQueueReceive(s_display_queue, &job, portMAX_DELAY) == pdTRUE) {
            s3_display_job_t newer;
            while (xQueueReceive(s_display_queue, &newer, 0) == pdTRUE) {
                if (job.snapshot_index < S3_SNAPSHOT_COUNT) s_snapshot_busy[job.snapshot_index] = false;
                job = newer;
                s_dropped_jobs++;
            }
            blit_game_to_panel(job.framebuffer);
            if (job.snapshot_index < S3_SNAPSHOT_COUNT) s_snapshot_busy[job.snapshot_index] = false;
            s_displayed_sequence = job.sequence;
        }
    }
}

esp_err_t rtype_display_init(void) {
    if (s_ready) return ESP_OK;
    ESP_LOGI(TAG, "initializing S3 RGB panel: physical=%ux%u aspect-blit view=%ux%u+%u,%u source=%ux%u",
             RTYPE_LCD_W, RTYPE_LCD_H, S3_VIEW_W, S3_VIEW_H, S3_VIEW_X, S3_VIEW_Y, RTYPE_GAME_W, RTYPE_GAME_H);
    lcd_cyd_init();
    void *fb = NULL;
    lcd_get_rgb_framebuffer(&fb);
    s_rgb_fb = (uint16_t *)fb;
    if (s_rgb_fb == NULL) {
        ESP_LOGE(TAG, "RGB framebuffer unavailable after lcd_cyd_init");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "RGB panel framebuffer @%p", (void *)s_rgb_fb);
    memset(s_rgb_fb, 0, (size_t)RTYPE_LCD_W * RTYPE_LCD_H * sizeof(uint16_t));

#if !S3_DIRECT_FRAMEBUFFER_HANDOFF
    for (unsigned i = 0; i < S3_SNAPSHOT_COUNT; i++) {
        s_snapshots[i] = (uint16_t *)heap_caps_malloc((size_t)RTYPE_GAME_W * RTYPE_GAME_H * sizeof(uint16_t),
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_snapshots[i] == NULL) {
            ESP_LOGE(TAG, "failed to allocate S3 display snapshot %u", i);
            return ESP_ERR_NO_MEM;
        }
    }
#else
    ESP_LOGI(TAG, "S3 display direct framebuffer handoff enabled");
#endif

    s_display_queue = xQueueCreate(2, sizeof(s3_display_job_t));
    if (s_display_queue == NULL) {
        ESP_LOGE(TAG, "failed to create S3 display queue");
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(s3_display_task, "rtype-s3-display", 4096,
                                            NULL, tskIDLE_PRIORITY + 2, &s_display_task, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to start S3 display task; using blocking fallback");
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

    if (s_display_task == NULL || s_display_queue == NULL) {
        blit_game_to_panel(framebuffer);
        return ESP_OK;
    }

#if S3_DIRECT_FRAMEBUFFER_HANDOFF
    s3_display_job_t job = {
        .framebuffer = framebuffer,
        .snapshot_index = S3_SNAPSHOT_COUNT,
        .sequence = ++s_present_sequence,
    };
#else
    unsigned chosen = S3_SNAPSHOT_COUNT;
    for (unsigned n = 0; n < S3_SNAPSHOT_COUNT; n++) {
        unsigned i = (s_next_snapshot + n) % S3_SNAPSHOT_COUNT;
        if (!s_snapshot_busy[i]) {
            chosen = i;
            break;
        }
    }
    if (chosen == S3_SNAPSHOT_COUNT) {
        s_dropped_jobs++;
        return ESP_OK;
    }

    s_snapshot_busy[chosen] = true;
    s_next_snapshot = (chosen + 1u) % S3_SNAPSHOT_COUNT;
    memcpy(s_snapshots[chosen], framebuffer, (size_t)RTYPE_GAME_W * RTYPE_GAME_H * sizeof(uint16_t));
    s3_display_job_t job = {
        .framebuffer = s_snapshots[chosen],
        .snapshot_index = chosen,
        .sequence = ++s_present_sequence,
    };
#endif
    if (xQueueSend(s_display_queue, &job, 0) != pdTRUE) {
#if !S3_DIRECT_FRAMEBUFFER_HANDOFF
        s_snapshot_busy[chosen] = false;
#endif
        s_dropped_jobs++;
    }
    return ESP_OK;
}

esp_err_t rtype_display_present_boot_pattern(unsigned frame_no) {
    (void)frame_no;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t rtype_display_present_m72_core(rtype_m72_core_t *core) {
    (void)core;
    return ESP_ERR_NOT_SUPPORTED;
}

_Noreturn void rtype_display_heartbeat_loop(void) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
