# Portable (★) component: game engine, no ESP-IDF dependencies.
set(srcs engine.cpp event_scheduler.cpp game_registry.cpp params.cpp player_registry.cpp score_board.cpp
         snapshot_store.cpp theme.cpp types.cpp)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${srcs} INCLUDE_DIRS include REQUIRES orca_hal nlohmann_json)
else()
  add_library(orca_game_core STATIC ${srcs})
  target_include_directories(orca_game_core PUBLIC include)
  target_link_libraries(orca_game_core PUBLIC orca_hal nlohmann_json)
endif()
