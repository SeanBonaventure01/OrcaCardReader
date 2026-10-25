idf_component_register(
  SRCS display.cpp
  INCLUDE_DIRS include
  PRIV_INCLUDE_DIRS .
  REQUIRES esp_lcd orca_hal
  PRIV_REQUIRES board device fx_core esp_timer espressif__esp_lvgl_port lvgl__lvgl
  # Idle artwork generated from images/orca_logo.jpg by tools/assets/make_splash.py
  EMBED_FILES assets/orca_splash.rgb565)
