idf_component_register(
  SRCS audio.cpp
  INCLUDE_DIRS include
  REQUIRES orca_hal
  PRIV_REQUIRES board device fx_core esp_driver_i2s)
