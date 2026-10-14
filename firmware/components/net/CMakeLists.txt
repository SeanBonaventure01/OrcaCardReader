set(WEB_DIST ${CMAKE_CURRENT_LIST_DIR}/../../../web/dist)
set(WEB_FILES ${WEB_DIST}/index.html.gz ${WEB_DIST}/app.js.gz ${WEB_DIST}/index.css.gz)
foreach(f ${WEB_FILES})
  if(NOT EXISTS ${f})
    message(FATAL_ERROR "Phone UI not built (${f} missing). Run: make web-build")
  endif()
endforeach()

idf_component_register(
  SRCS wifi.cpp http.cpp
  INCLUDE_DIRS include
  REQUIRES api config
  PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_server lwip espressif__mdns
  EMBED_FILES ${WEB_FILES})
