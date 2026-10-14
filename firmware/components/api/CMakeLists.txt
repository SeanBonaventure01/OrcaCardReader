# Portable (★) component: transport-agnostic web API.
if(ESP_PLATFORM)
  idf_component_register(SRCS api.cpp INCLUDE_DIRS include REQUIRES game_core config)
else()
  add_library(orca_api STATIC api.cpp)
  target_include_directories(orca_api PUBLIC include)
  target_link_libraries(orca_api PUBLIC orca_game_core orca_config)
endif()
