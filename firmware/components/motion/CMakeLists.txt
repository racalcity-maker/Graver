idf_component_register(
    SRCS "src/motion_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board shared
    PRIV_REQUIRES esp_rom laser
)
