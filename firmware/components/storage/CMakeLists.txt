idf_component_register(
  SRCS storage.cpp
  INCLUDE_DIRS include
  REQUIRES orca_hal
  PRIV_REQUIRES joltwallet__littlefs)
