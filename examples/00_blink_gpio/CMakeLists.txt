# 00_blink_gpio — proves e32p_core + e32p_gpio build, link, and run.
# Boilerplate must precede project(). EXTRA_COMPONENT_DIRS points at the
# platform's shared component tree so the example cherry-picks e32p_* modules.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(blink_gpio)
