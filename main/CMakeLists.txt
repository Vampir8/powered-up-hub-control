set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/web/index.html"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/provision.py")
execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../tools/provision.py"
    "${CMAKE_CURRENT_LIST_DIR}/.." RESULT_VARIABLE provision_result)
if(NOT provision_result EQUAL 0)
    message(FATAL_ERROR "Could not generate provisioning credentials")
endif()
idf_component_register(SRCS "app.c" "config.c" "core.c" "ble.c" "network.c" "web.c" "../generated/web_asset.c"
    INCLUDE_DIRS "." "../generated"
    REQUIRES bt esp_wifi esp_event esp_netif nvs_flash mqtt mdns esp_http_server json app_update driver esp_timer mbedtls)
