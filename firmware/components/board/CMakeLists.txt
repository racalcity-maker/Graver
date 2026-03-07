idf_component_register(
    SRCS "src/board_hal.cpp"
    INCLUDE_DIRS "include"
    REQUIRES shared esp_driver_rmt
    PRIV_REQUIRES driver esp_rom
)
