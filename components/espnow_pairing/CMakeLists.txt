idf_component_register(
    SRCS "espnow_pairing.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash freertos esp_timer
)
