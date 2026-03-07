idf_component_register(
    SRCS "src/network_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_netif esp_wifi shared
)

