#ifndef RTYPE_BLIT_H
#define RTYPE_BLIT_H

#include <stddef.h>
#include <stdint.h>

#define RTYPE_BLIT_SRC_W 384u
#define RTYPE_BLIT_SRC_H 256u
#define RTYPE_BLIT_CYD_PHYS_W 240u
#define RTYPE_BLIT_CYD_PHYS_H 320u
#define RTYPE_BLIT_CYD_VIEW_W 240u
#define RTYPE_BLIT_CYD_VIEW_H 160u
#define RTYPE_BLIT_CYD_VIEW_X 0u
#define RTYPE_BLIT_CYD_VIEW_Y ((RTYPE_BLIT_CYD_PHYS_H - RTYPE_BLIT_CYD_VIEW_H) / 2u)

// Source and destination pixels are RGB565 in native uint16_t memory order for
// esp_lcd draw_bitmap. The CYD ILI9341 init uses MADCTL BGR but panel_config
// requests LCD_RGB_ELEMENT_ORDER_RGB; no software RGB/BGR bit swap is applied.
uint16_t rtype_blit_rgb565_identity(uint16_t rgb565);
void rtype_blit_cyd_fill_border_strip(uint16_t *dst, unsigned dst_y, unsigned rows, uint16_t color);
void rtype_blit_cyd_scale_strip_240x160(const uint16_t *src, uint16_t *dst, unsigned dst_y, unsigned rows);

#endif
