cmake_minimum_required(VERSION 3.16)

# Shared espnow_pairing component
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(receiver)
