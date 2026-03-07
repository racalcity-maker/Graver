idf_component_register(
    SRCS "src/application.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board control jobs laser motion network shared storage web
    PRIV_REQUIRES esp_event esp_netif nvs_flash
)
