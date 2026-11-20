# 14_twai_loopback — proves e32p_twai (CAN controller, NO_ACK self-test).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(twai_loopback)
