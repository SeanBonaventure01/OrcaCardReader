# Portable (★) component: config.json parsing and runtime settings validation.
if(ESP_PLATFORM)
  idf_component_register(SRCS config.cpp INCLUDE_DIRS include REQUIRES nlohmann_json)
else()
  add_library(orca_config STATIC config.cpp)
  target_include_directories(orca_config PUBLIC include)
  target_link_libraries(orca_config PUBLIC nlohmann_json)
endif()
