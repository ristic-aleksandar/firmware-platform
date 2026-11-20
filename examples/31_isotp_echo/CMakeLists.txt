# 31_isotp_echo — ISO-TP (ISO 15765-2) segmented send, reassemble and echo.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(isotp_echo)
