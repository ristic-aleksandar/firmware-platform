# 16_ws2812_rainbow — proves e32p_ws2812 (scrolls a rainbow over RMT).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ws2812_rainbow)
