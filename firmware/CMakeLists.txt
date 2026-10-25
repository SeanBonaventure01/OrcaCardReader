cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LVGL tracks this many changed areas per frame before giving up and redrawing the whole
# screen (default 32). Confetti/fireworks move ~100 small objects, and full-screen redraws
# are what make the RGB panel tear, so keep them as small areas. Applies to every component
# so LVGL and esp_lvgl_port agree on the display struct.
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=200" APPEND)
project(orca_reader)
