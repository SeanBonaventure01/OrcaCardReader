# Portable (★) component: LED effect rendering + current limiting, audio helpers, voice lines
# and confetti/fireworks particles.
set(srcs leds.cpp audio.cpp voice.cpp particles.cpp rainbow.cpp)
if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES nlohmann_json)
else()
  add_library(orca_fx_core STATIC ${srcs})
  target_include_directories(orca_fx_core PUBLIC include)
  target_link_libraries(orca_fx_core PUBLIC nlohmann_json)
endif()
