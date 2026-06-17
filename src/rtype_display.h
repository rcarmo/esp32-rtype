#ifndef RTYPE_DISPLAY_H
#define RTYPE_DISPLAY_H

#include "esp_err.h"
#include "rtype_m72_core.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rtype_display_init(void);
esp_err_t rtype_display_set_brightness(uint8_t percent);
esp_err_t rtype_display_present_rgb565(const uint16_t *framebuffer, unsigned width, unsigned height);
esp_err_t rtype_display_present_boot_pattern(unsigned frame_no);
esp_err_t rtype_display_present_m72_core(rtype_m72_core_t *core);
_Noreturn void rtype_display_heartbeat_loop(void);


#ifdef __cplusplus
}
#endif
#endif
