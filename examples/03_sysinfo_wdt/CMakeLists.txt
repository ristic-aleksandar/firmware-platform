# 03_sysinfo_wdt — proves e32p_sysinfo (introspection) + e32p_wdt (task WDT).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sysinfo_wdt)
