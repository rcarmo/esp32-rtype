#include "rtype_display.h"
#include "rtype_blit.h"
#include "rtype_board.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd_cyd.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "rtype_display_cyd";

typedef enum {
    RTYPE_DISPLAY_JOB_FRAMEBUFFER = 0,
    RTYPE_DISPLAY_JOB_M72_CORE = 1,
} rtype_display_job_kind_t;

typedef struct {
    const uint16_t *framebuffer;
    const rtype_m72_video_t *video;
    rtype_display_job_kind_t kind;
    uint32_t sequence;
} rtype_display_job_t;

typedef struct {
    uint16_t x0;
    uint16_t x1;
} column_range_t;

typedef struct {
    rtype_m72_video_t video;
    uint8_t *vram0;
    uint8_t *vram1;
    bool ready;
} rtype_video_snapshot_t;

static bool s_ready;
static uint16_t *s_strip;
static unsigned s_strip_cols;
static QueueHandle_t s_display_queue;
static TaskHandle_t s_display_task;
static uint32_t s_present_sequence;
static volatile uint32_t s_displayed_sequence;
static volatile uint32_t s_dropped_jobs;
static uint32_t s_live_present_count;
static rtype_video_snapshot_t s_snapshot;
static column_range_t s_prev_sprite_ranges[RTYPE_M72_SPRITERAM_BYTES / 8u];
static unsigned s_prev_sprite_range_count;

static void clear_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0 || s_strip == NULL) return;
    while (w > 0) {
        unsigned cols = (w > s_strip_cols) ? s_strip_cols : w;
        for (unsigned row = 0; row < h; row++) {
            for (unsigned col = 0; col < cols; col++) {
                s_strip[(size_t)row * cols + col] = 0;
            }
        }
        lcd_draw_bitmap(x, y, (uint16_t)cols, h, (const uint8_t *)s_strip);
        lcd_wait_trans_complete();
        x = (uint16_t)(x + cols);
        w = (uint16_t)(w - cols);
    }
}

static void clear_portrait_black(void) {
    clear_rect(0, 0, RTYPE_BLIT_CYD_PHYS_W, RTYPE_BLIT_CYD_PHYS_H);
}

static void rtype_display_flush_blocking(const uint16_t *framebuffer) {
    for (unsigned x = 0; x < RTYPE_BLIT_CYD_PHYS_W; x += s_strip_cols) {
        unsigned cols = s_strip_cols;
        if (x + cols > RTYPE_BLIT_CYD_PHYS_W) cols = RTYPE_BLIT_CYD_PHYS_W - x;
        rtype_blit_cyd_rotate_scale_columns_320x213(framebuffer, s_strip, x, cols);
        lcd_draw_bitmap((uint16_t)x, 0, (uint16_t)cols, RTYPE_BLIT_CYD_PHYS_H,
                        (const uint8_t *)s_strip);
        lcd_wait_trans_complete();
    }
}

static esp_err_t rtype_display_flush_m72_video_blocking(const rtype_m72_video_t *video);

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
            if (job.kind == RTYPE_DISPLAY_JOB_M72_CORE) {
                (void)rtype_display_flush_m72_video_blocking(job.video);
            } else {
                rtype_display_flush_blocking(job.framebuffer);
            }
            s_displayed_sequence = job.sequence;
        }
    }
}

esp_err_t rtype_display_init(void) {
    if (s_ready) return ESP_OK;
    ESP_LOGI(TAG,
             "initializing CYD ILI9341 SPI display: physical=%ux%u source=%ux%u rotated_view=%ux%u active_phys_x=%u..%u RGB565 async_core=0",
             RTYPE_BLIT_CYD_PHYS_W, RTYPE_BLIT_CYD_PHYS_H, RTYPE_GAME_W, RTYPE_GAME_H,
             RTYPE_BLIT_CYD_VIEW_W, RTYPE_BLIT_CYD_VIEW_H,
             RTYPE_BLIT_CYD_ACTIVE_X0, RTYPE_BLIT_CYD_ACTIVE_X1 - 1u);
    lcd_cyd_init();

    // Full 240x320 (153KB) can starve the no-PSRAM sparse emulator heap on CYD.
    // Try 120 columns (two DMA chunks) with the safe compositor; fall back if
    // heap fragmentation prevents the allocation.
    static const unsigned preferred_cols[] = {120u, 80u, 40u, 16u, 8u};
    for (unsigned i = 0; i < sizeof(preferred_cols) / sizeof(preferred_cols[0]); i++) {
        s_strip_cols = preferred_cols[i];
        s_strip = (uint16_t *)heap_caps_malloc((size_t)s_strip_cols * RTYPE_BLIT_CYD_PHYS_H * sizeof(uint16_t),
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_strip != NULL) break;
    }
    if (s_strip == NULL) {
        s_strip_cols = 8;
        s_strip = (uint16_t *)heap_caps_malloc((size_t)s_strip_cols * RTYPE_BLIT_CYD_PHYS_H * sizeof(uint16_t),
                                               MALLOC_CAP_8BIT);
    }
    if (s_strip == NULL) {
        ESP_LOGE(TAG, "failed to allocate CYD RGB565 column strip buffer");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "CYD live DMA buffer: %u columns x %u rows (%u bytes)",
             s_strip_cols, RTYPE_BLIT_CYD_PHYS_H,
             (unsigned)((size_t)s_strip_cols * RTYPE_BLIT_CYD_PHYS_H * sizeof(uint16_t)));

    clear_portrait_black();

    s_display_queue = xQueueCreate(2, sizeof(rtype_display_job_t));
    if (s_display_queue == NULL) {
        ESP_LOGE(TAG, "failed to create CYD display queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(rtype_display_task, "rtype-cyd-display", 4096,
                                            NULL, tskIDLE_PRIORITY + 2, &s_display_task, 1);
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

static uint16_t read16le_local(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static unsigned collect_sprite_column_ranges(const rtype_m72_video_t *video, column_range_t *ranges, unsigned max_ranges) {
    if (video == NULL || ranges == NULL || max_ranges == 0) return 0;
    const uint8_t *sprite_ram = video->sprite_buffer_valid ? video->sprite_buffer : video->spriteram;
    if (sprite_ram == NULL) return 0;
    unsigned count = 0;
    for (int offs = (int)RTYPE_M72_SPRITERAM_BYTES - 8; offs >= 0; offs -= 8) {
        const uint8_t *s = sprite_ram + offs;
        uint16_t syw = read16le_local(s + 0);
        uint16_t code = read16le_local(s + 2);
        uint16_t attr = read16le_local(s + 4);
        uint16_t sxw = read16le_local(s + 6);
        if ((syw | code | attr | sxw) == 0) continue;
        unsigned w = 1u << ((attr >> 14) & 3u);
        unsigned h = 1u << ((attr >> 12) & 3u);
        int raw_x0 = -256 + (int)(sxw & 0x03ffu);
        int raw_y0 = 384 - (int)(syw & 0x01ffu) - (int)(16u * h);
        int src_x0 = raw_x0 - 64;
        int src_x1 = src_x0 + (int)(16u * w) - 1;
        int src_y0 = raw_y0;
        int src_y1 = raw_y0 + (int)(16u * h) - 1;
        if (src_x1 < 0 || src_x0 >= (int)RTYPE_GAME_W || src_y1 < 0 || src_y0 >= (int)RTYPE_GAME_H) continue;
        if (src_y0 < 0) src_y0 = 0;
        if (src_y1 >= (int)RTYPE_GAME_H) src_y1 = (int)RTYPE_GAME_H - 1;
        unsigned px0 = RTYPE_BLIT_CYD_ACTIVE_X0 + ((unsigned)src_y0 * RTYPE_BLIT_CYD_VIEW_H) / RTYPE_GAME_H;
        unsigned px1 = RTYPE_BLIT_CYD_ACTIVE_X0 + (((unsigned)src_y1 + 1u) * RTYPE_BLIT_CYD_VIEW_H + RTYPE_GAME_H - 1u) / RTYPE_GAME_H;
        if (px0 < RTYPE_BLIT_CYD_ACTIVE_X0) px0 = RTYPE_BLIT_CYD_ACTIVE_X0;
        if (px1 > RTYPE_BLIT_CYD_ACTIVE_X1) px1 = RTYPE_BLIT_CYD_ACTIVE_X1;
        px0 &= ~1u;
        px1 = (px1 + 1u) & ~1u;
        if (px1 > RTYPE_BLIT_CYD_PHYS_W) px1 = RTYPE_BLIT_CYD_PHYS_W;
        if (px0 >= px1) continue;
        if (count < max_ranges) {
            ranges[count++] = (column_range_t){.x0 = (uint16_t)px0, .x1 = (uint16_t)px1};
        }
    }
    return count;
}

static esp_err_t ensure_video_snapshot(void) {
    if (s_snapshot.ready) return ESP_OK;
    s_snapshot.vram0 = (uint8_t *)heap_caps_malloc(RTYPE_M72_VRAM_BYTES, MALLOC_CAP_8BIT);
    s_snapshot.vram1 = (uint8_t *)heap_caps_malloc(RTYPE_M72_VRAM_BYTES, MALLOC_CAP_8BIT);
    if (s_snapshot.vram0 == NULL || s_snapshot.vram1 == NULL) {
        ESP_LOGE(TAG, "failed to allocate CYD video snapshot buffers");
        return ESP_ERR_NO_MEM;
    }
    s_snapshot.ready = true;
    ESP_LOGI(TAG, "CYD frame snapshot: %u bytes", (unsigned)(RTYPE_M72_VRAM_BYTES * 2u + RTYPE_M72_SPRITERAM_BYTES));
    return ESP_OK;
}

static esp_err_t copy_video_snapshot(rtype_m72_core_t *core) {
    if (core == NULL) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ensure_video_snapshot(), TAG, "snapshot alloc");
    s_snapshot.video = core->video;
    if (core->video.vram0 != NULL) memcpy(s_snapshot.vram0, core->video.vram0, RTYPE_M72_VRAM_BYTES);
    else memset(s_snapshot.vram0, 0, RTYPE_M72_VRAM_BYTES);
    if (core->video.vram1 != NULL) memcpy(s_snapshot.vram1, core->video.vram1, RTYPE_M72_VRAM_BYTES);
    else memset(s_snapshot.vram1, 0, RTYPE_M72_VRAM_BYTES);
    const uint8_t *sprite_ram = core->video.sprite_buffer_valid ? core->video.sprite_buffer : core->video.spriteram;
    if (sprite_ram != NULL) memcpy(s_snapshot.video.sprite_buffer, sprite_ram, RTYPE_M72_SPRITERAM_BYTES);
    else memset(s_snapshot.video.sprite_buffer, 0, RTYPE_M72_SPRITERAM_BYTES);
    s_snapshot.video.vram0 = s_snapshot.vram0;
    s_snapshot.video.vram1 = s_snapshot.vram1;
    s_snapshot.video.spriteram = s_snapshot.video.sprite_buffer;
    s_snapshot.video.sprite_buffer_valid = 1;
    return ESP_OK;
}

static unsigned sort_merge_ranges(column_range_t *ranges, unsigned count) {
    for (unsigned i = 1; i < count; i++) {
        column_range_t r = ranges[i];
        unsigned j = i;
        while (j > 0 && ranges[j - 1].x0 > r.x0) {
            ranges[j] = ranges[j - 1];
            j--;
        }
        ranges[j] = r;
    }
    unsigned out = 0;
    for (unsigned i = 0; i < count; i++) {
        if (ranges[i].x0 >= ranges[i].x1) continue;
        if (out == 0 || ranges[i].x0 > ranges[out - 1].x1) {
            ranges[out++] = ranges[i];
        } else if (ranges[i].x1 > ranges[out - 1].x1) {
            ranges[out - 1].x1 = ranges[i].x1;
        }
    }
    return out;
}

esp_err_t rtype_display_present_boot_pattern(unsigned frame_no) {
    (void)frame_no;
    esp_err_t err = rtype_display_init();
    if (err != ESP_OK) return err;
    clear_portrait_black();
    return ESP_OK;
}

static void draw_columns(const rtype_m72_video_t *video, unsigned x0, unsigned x1,
                         bool include_sprites, uint32_t *checksum) {
    if (x1 > RTYPE_BLIT_CYD_PHYS_W) x1 = RTYPE_BLIT_CYD_PHYS_W;
    if (x0 >= x1) return;
    for (unsigned x = x0; x < x1; x += s_strip_cols) {
        unsigned cols = s_strip_cols;
        if (x + cols > x1) cols = x1 - x;
        if (include_sprites) rtype_m72_video_render_cyd_composited_columns(video, s_strip, x, cols);
        else rtype_m72_video_render_cyd_background_columns(video, s_strip, x, cols);
        if (checksum != NULL) {
            for (unsigned i = 0; i < cols * RTYPE_BLIT_CYD_PHYS_H; i++) {
                *checksum = (*checksum * 33u) ^ s_strip[i];
            }
        }
        lcd_draw_bitmap((uint16_t)x, 0, (uint16_t)cols, RTYPE_BLIT_CYD_PHYS_H,
                        (const uint8_t *)s_strip);
        lcd_wait_trans_complete();
    }
}

static esp_err_t rtype_display_flush_m72_video_blocking(const rtype_m72_video_t *video) {
    if (video == NULL) return ESP_ERR_INVALID_ARG;
    uint32_t checksum = 0;
    draw_columns(video, 0, RTYPE_BLIT_CYD_PHYS_W, true, &checksum);

    if ((s_live_present_count++ & 0x1fu) == 0) {
        ESP_LOGI(TAG, "CYD live snapshot updated_cols=%u crc=0x%08" PRIx32 " strip_cols=%u dropped=%" PRIu32,
                 RTYPE_BLIT_CYD_PHYS_W, checksum, s_strip_cols, (uint32_t)s_dropped_jobs);
    }
    return ESP_OK;
}

esp_err_t rtype_display_present_m72_core(rtype_m72_core_t *core) {
    if (core == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = rtype_display_init();
    if (err != ESP_OK) return err;
    if (s_display_task != NULL && s_display_queue != NULL) {
        if (s_present_sequence != s_displayed_sequence) {
            s_dropped_jobs++;
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(copy_video_snapshot(core), TAG, "copy snapshot");
        rtype_display_job_t job = {
            .framebuffer = NULL,
            .video = &s_snapshot.video,
            .kind = RTYPE_DISPLAY_JOB_M72_CORE,
            .sequence = ++s_present_sequence,
        };
        if (xQueueSend(s_display_queue, &job, 0) != pdTRUE) {
            s_dropped_jobs++;
            s_displayed_sequence = s_present_sequence;
        }
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(copy_video_snapshot(core), TAG, "copy snapshot");
    return rtype_display_flush_m72_video_blocking(&s_snapshot.video);
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
            .video = NULL,
            .kind = RTYPE_DISPLAY_JOB_FRAMEBUFFER,
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
