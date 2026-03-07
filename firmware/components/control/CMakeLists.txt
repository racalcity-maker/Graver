idf_component_register(
    SRCS "src/control_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES jobs laser motion shared
    PRIV_REQUIRES freertos
)
