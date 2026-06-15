idf_component_register(
    SRCS "lcd_cyd.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/include"
    REQUIRES esp_lcd
    PRIV_REQUIRES driver
)
