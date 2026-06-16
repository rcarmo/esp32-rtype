#ifndef RTYPE_DISPLAY_H
#define RTYPE_DISPLAY_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t rtype_display_init(void);
esp_err_t rtype_display_set_brightness(uint8_t percent);
esp_err_t rtype_display_present_rgb565(const uint16_t *framebuffer, unsigned width, unsigned height);
esp_err_t rtype_display_present_boot_pattern(unsigned frame_no);
_Noreturn void rtype_display_heartbeat_loop(void);

#endif
