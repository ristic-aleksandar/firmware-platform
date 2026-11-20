# 30_j1939_ecu_sim — a J1939 engine-ECU simulator (bench signal source for 29_edge_gateway).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(j1939_ecu_sim)
