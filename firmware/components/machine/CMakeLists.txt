idf_component_register(
    SRCS "src/machine_controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES jobs laser motion shared
)

