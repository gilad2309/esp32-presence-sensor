cmake_minimum_required(VERSION 3.16)

# esp_lcd is unused by this project (WS2812 LED is driven via RMT, not the
# LCD peripheral). Excluded because its esp32s3 esp_lcd_panel_rgb.c triggers
# a GCC internal compiler error (segfault) with this toolchain build.
set(EXCLUDE_COMPONENTS "esp_lcd")

# Shared espnow_pairing component
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(receiver_esp32_s3)
