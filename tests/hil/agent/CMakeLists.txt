# HIL test agent — one binary for both boards (role decided at runtime from NVS).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hil_agent)
