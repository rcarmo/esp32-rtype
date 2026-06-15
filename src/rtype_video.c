#include "rtype_video.h"
#include "rtype_board.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stddef.h>

static const char *TAG = "rtype_video";

uint16_t *rtype_video_alloc_framebuffer(void) {
    const size_t bytes = (size_t)RTYPE_GAME_W * RTYPE_GAME_H * sizeof(uint16_t);
    uint16_t *fb = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (fb == NULL) fb = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    if (fb == NULL) {
        ESP_LOGE(TAG, "failed to allocate %zu-byte RGB565 game framebuffer", bytes);
        return NULL;
    }
    ESP_LOGI(TAG, "allocated %ux%u RGB565 game framebuffer: %zu bytes @%p",
             RTYPE_GAME_W, RTYPE_GAME_H, bytes, (void *)fb);
    return fb;
}

void rtype_video_render_boot_pattern(uint16_t *fb, unsigned frame_no) {
    if (fb == NULL) return;
    for (unsigned y = 0; y < RTYPE_GAME_H; y++) {
        for (unsigned x = 0; x < RTYPE_GAME_W; x++) {
            uint8_t r = (uint8_t)((x * 255u) / (RTYPE_GAME_W - 1u));
            uint8_t g = (uint8_t)((y * 255u) / (RTYPE_GAME_H - 1u));
            uint8_t b = (uint8_t)((frame_no * 3u + x / 8u + y / 8u) & 0xffu);
            if (x < 4 || y < 4 || x >= RTYPE_GAME_W - 4 || y >= RTYPE_GAME_H - 4) {
                r = g = b = 255;
            }
            if ((x / 16u + y / 16u + frame_no / 15u) & 1u) {
                b = (uint8_t)(255u - b);
            }
            fb[(size_t)y * RTYPE_GAME_W + x] = rtype_rgb565(r, g, b);
        }
    }
}
