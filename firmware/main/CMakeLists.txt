idf_component_register(
  SRCS app_main.cpp game_task.cpp console.cpp
  PRIV_REQUIRES board nfc display leds audio storage net api config game_core games device console nvs_flash esp_timer)
