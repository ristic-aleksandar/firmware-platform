# 02_nvs_boot_count — proves e32p_nvs (persistence) + e32p_event (dispatch).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nvs_boot_count)
