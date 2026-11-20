# 18_j1939tp_bam — proves e32p_j1939tp (BAM transfer via in-process loopback).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(j1939tp_bam)
