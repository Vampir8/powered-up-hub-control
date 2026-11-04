cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lego_mqtt_bridge)

# IDF 5.4.1 esp_nimble_cfg.h reads CONFIG_NIMBLE_MAX_CONNECTIONS, while
# Kconfig exposes CONFIG_BT_NIMBLE_MAX_CONNECTIONS. Map the former on the
# actual bt component (and consumers), rather than changing an SDK checkout.
idf_component_get_property(bt_library bt COMPONENT_LIB)
target_compile_definitions(${bt_library} PUBLIC
    CONFIG_NIMBLE_MAX_CONNECTIONS=CONFIG_BT_NIMBLE_MAX_CONNECTIONS)
