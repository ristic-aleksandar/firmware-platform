# 29_edge_gateway — industrial edge gateway (flagship integration demo).
# Reads a J1939/CAN field bus (or a built-in simulator), normalizes the signals,
# and bridges them to an MQTT cloud — with BLE provisioning, remote OTA, and a
# local status display. It composes ~14 e32p_* modules into one product.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(edge_gateway)
