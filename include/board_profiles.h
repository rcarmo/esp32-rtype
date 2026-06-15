#ifndef RTYPE_BOARD_PROFILES_H
#define RTYPE_BOARD_PROFILES_H

#define GPIO_UNUSED -1

#if defined(CYD_BOARD_ESP32_8048S043C) || defined(RTYPE_BOARD_ESP32_8048S043C)
#ifndef CYD_BOARD_ESP32_8048S043C
#define CYD_BOARD_ESP32_8048S043C 1
#endif
#include "boards/esp32_8048s043c.h"
#elif defined(CYD_BOARD_ESP32_2432S028) || defined(RTYPE_BOARD_ESP32_2432S028)
#ifndef CYD_BOARD_ESP32_2432S028
#define CYD_BOARD_ESP32_2432S028 1
#endif
#include "boards/esp32_2432s028.h"
#else
#error "This compatibility board profile currently supports ESP32-8048S043C and ESP32-2432S028"
#endif

#ifndef LCD_PANEL_RGB565_BYTE_SWAP
#define LCD_PANEL_RGB565_BYTE_SWAP 0
#endif

#endif
