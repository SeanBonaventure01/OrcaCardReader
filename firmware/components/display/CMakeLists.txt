idf_component_register(
  SRCS display.cpp
  INCLUDE_DIRS include
  REQUIRES esp_lcd orca_hal
  PRIV_REQUIRES board device esp_timer espressif__esp_lvgl_port lvgl__lvgl)
