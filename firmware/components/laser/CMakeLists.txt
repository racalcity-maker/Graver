idf_component_register(
    SRCS "src/laser_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board shared
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc freertos
)
