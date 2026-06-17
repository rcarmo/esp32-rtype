cmake_minimum_required(VERSION 3.16.0)

if(RTYPE_TARGET_TAB5)
    set(EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${CMAKE_CURRENT_LIST_DIR}/components_tab5/m5stack_tab5"
        "${CMAKE_CURRENT_LIST_DIR}/managed_components")
else()
    set(EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${CMAKE_CURRENT_LIST_DIR}/lib"
        "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__cmake_utilities"
        "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__esp_lcd_ili9341")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_rtype)
