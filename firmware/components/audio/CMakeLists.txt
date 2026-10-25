idf_component_register(
  SRCS audio.cpp
  INCLUDE_DIRS include
  EMBED_FILES assets/voice_profanity.bin
  REQUIRES orca_hal
  PRIV_REQUIRES board device fx_core esp_driver_i2s esp_timer)
