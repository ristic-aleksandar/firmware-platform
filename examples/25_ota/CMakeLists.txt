# 25_ota — self-contained OTA demo: clone the running image into the other slot,
# boot it, confirm it (rollback), repeat. No server needed.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ota)
