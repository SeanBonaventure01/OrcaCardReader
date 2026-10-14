idf_component_register(
  SRCS leds.cpp
  INCLUDE_DIRS include
  REQUIRES orca_hal config
  PRIV_REQUIRES device fx_core esp_timer espressif__led_strip)
