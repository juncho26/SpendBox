set(LOGIC ${CMAKE_CURRENT_LIST_DIR}/../../core)
idf_component_register(
    SRCS main.c hal_epd.c hal_misc.c hal_store.c hal_sync.c hal_voice.c
         ${LOGIC}/budget.c ${LOGIC}/ledger.c ${LOGIC}/categories.c ${LOGIC}/ui.c ${LOGIC}/button.c
         ${LOGIC}/gfx/gfx.c ${LOGIC}/gfx/screens.c ${LOGIC}/gfx/display.c ${LOGIC}/gfx/font_data.c
         ${LOGIC}/gfx/third_party/qrcodegen.c
    INCLUDE_DIRS . ${LOGIC} ${LOGIC}/gfx
    REQUIRES lwip driver esp_adc esp_timer esp_pm esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_partition esp_hw_support esp_system freertos)
