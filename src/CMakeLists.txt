set(RTYPE_SOURCES
    "app_main.c"
    "rtype_rom.c"
    "rtype_video.c"
    "rtype_blit.c"
    "rtype_m72_video.c"
    "rtype_m72_core.c"
    "rtype_i86_cpu.c"
)
set(RTYPE_REQUIRES driver esp_psram esp_hw_support heap esp_lcd esp_timer)

if("${IDF_TARGET}" STREQUAL "esp32p4")
    list(APPEND RTYPE_SOURCES "rtype_display_tab5.c")
    list(APPEND RTYPE_REQUIRES m5stack_tab5 fatfs sdmmc)
elseif("${IDF_TARGET}" STREQUAL "esp32s3")
    list(APPEND RTYPE_SOURCES "rtype_display_s3.c")
    list(APPEND RTYPE_REQUIRES lcd_cyd fatfs wear_levelling)
elseif("${IDF_TARGET}" STREQUAL "esp32")
    list(APPEND RTYPE_SOURCES "rtype_display_cyd.c")
    list(APPEND RTYPE_REQUIRES lcd_cyd)
else()
    message(FATAL_ERROR "Unsupported IDF_TARGET=${IDF_TARGET}; expected esp32, esp32s3 or esp32p4")
endif()

idf_component_register(
    SRCS ${RTYPE_SOURCES}
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/include"
    REQUIRES ${RTYPE_REQUIRES}
)

if("${IDF_TARGET}" STREQUAL "esp32")
    target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
endif()
