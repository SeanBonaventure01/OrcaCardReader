# Portable (★) component: PN532 protocol + driver over an abstract transport, card payload
# codec, tap debouncing.
set(srcs pn532.cpp card_payload.cpp tap_debouncer.cpp)
if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
else()
  add_library(orca_nfc_core STATIC ${srcs})
  target_include_directories(orca_nfc_core PUBLIC include)
endif()
