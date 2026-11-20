# 15_i2s_tone — proves e32p_i2s (streams a sine tone over I2S TX).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(i2s_tone)
