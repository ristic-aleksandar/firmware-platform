# 09_dac_adc_loopback — proves e32p_dac + e32p_adc via an on-chip analog loopback.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dac_adc_loopback)
