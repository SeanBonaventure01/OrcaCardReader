# Portable (★) component: interfaces only, no ESP-IDF dependencies.
# Builds as an ESP-IDF component on the device and as a plain CMake target on the host.
if(ESP_PLATFORM)
  idf_component_register(INCLUDE_DIRS include)
else()
  add_library(orca_hal INTERFACE)
  target_include_directories(orca_hal INTERFACE include)
  target_compile_features(orca_hal INTERFACE cxx_std_17)
endif()
