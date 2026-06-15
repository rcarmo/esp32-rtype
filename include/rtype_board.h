#ifndef RTYPE_BOARD_H
#define RTYPE_BOARD_H

#include <stdint.h>

#define RTYPE_GAME_W 384u
#define RTYPE_GAME_H 256u

#if defined(RTYPE_BOARD_ESP32_8048S043C)
#define RTYPE_BOARD_NAME "ESP32-8048S043C / ESP32-S3"
#define RTYPE_LCD_W 800u
#define RTYPE_LCD_H 480u
#define RTYPE_VIEW_SCALE 1u
#elif defined(RTYPE_BOARD_M5STACK_TAB5_ESP32P4)
#define RTYPE_BOARD_NAME "M5Stack Tab5 ESP32-P4"
#define RTYPE_LCD_W 720u
#define RTYPE_LCD_H 1280u
#define RTYPE_VIEW_SCALE 1u
#else
#error "Select RTYPE_BOARD_ESP32_8048S043C or RTYPE_BOARD_M5STACK_TAB5_ESP32P4"
#endif

#define RTYPE_VIEW_W (RTYPE_GAME_W * RTYPE_VIEW_SCALE)
#define RTYPE_VIEW_H (RTYPE_GAME_H * RTYPE_VIEW_SCALE)
#define RTYPE_VIEW_X ((RTYPE_LCD_W - RTYPE_VIEW_W) / 2u)
#define RTYPE_VIEW_Y ((RTYPE_LCD_H - RTYPE_VIEW_H) / 2u)
#define RTYPE_STRIP_ROWS 24u

#define RTYPE_EXPECTED_ZIP_TOTAL 1048576u
#define RTYPE_EXPECTED_FILE_COUNT 20u

static inline uint16_t rtype_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                      ((uint16_t)(g & 0xfcu) << 3) |
                      ((uint16_t)b >> 3));
}

#endif
