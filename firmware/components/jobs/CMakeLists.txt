idf_component_register(
    SRCS "src/job_manager.cpp" "src/raster_job.cpp" "src/raster_executor.cpp" "src/vector_primitive_job.cpp" "src/vector_text_job.cpp" "src/vector_job_executor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES shared storage motion laser json
)
