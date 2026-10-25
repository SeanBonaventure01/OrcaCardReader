idf_component_register(
  SRCS nfc_device.cpp
  INCLUDE_DIRS include
  PRIV_REQUIRES nfc_core esp_driver_i2c esp_timer)
