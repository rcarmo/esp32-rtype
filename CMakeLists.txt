cmake_minimum_required(VERSION 3.16.0)

if("$ENV{PIOENV}" MATCHES "tab5")
    set(EXTRA_COMPONENT_DIRS "src" "components_tab5")
else()
    set(EXTRA_COMPONENT_DIRS "src" "lib")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rtype_tab5)
