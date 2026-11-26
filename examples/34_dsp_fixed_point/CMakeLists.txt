# 34_dsp_fixed_point: a FIR filter ported from Python float to Q15 C, checked on the chip.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dsp_fixed_point)
