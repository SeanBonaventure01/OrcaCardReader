# Vendored nlohmann/json v3.11.3 (MIT), header-only.
if(ESP_PLATFORM)
  idf_component_register(INCLUDE_DIRS include)
else()
  add_library(nlohmann_json INTERFACE)
  target_include_directories(nlohmann_json SYSTEM INTERFACE include)
endif()
