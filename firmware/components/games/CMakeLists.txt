# Portable (★) component: built-in game modes.
set(srcs fare_paid.cpp more_games.cpp registry.cpp)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES game_core)
else()
  add_library(orca_games STATIC ${srcs})
  target_include_directories(orca_games PUBLIC include)
  target_link_libraries(orca_games PUBLIC orca_game_core)
endif()
