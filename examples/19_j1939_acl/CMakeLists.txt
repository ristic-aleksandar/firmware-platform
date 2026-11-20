# 19_j1939_acl — proves e32p_j1939_acl (address-claim contention, in-process).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(j1939_acl)
