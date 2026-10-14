# Portable (★) component: hardware interfaces, no ESP-IDF dependencies.
# Builds as an ESP-IDF component on the device and as a plain CMake target on the host.
if(ESP_PLATFORM)
  idf_component_register(SRCS "hal.cpp" INCLUDE_DIRS include)
else()
  add_library(orca_hal STATIC hal.cpp)
  target_include_directories(orca_hal PUBLIC include)
  target_compile_features(orca_hal PUBLIC cxx_std_17)
endif()
