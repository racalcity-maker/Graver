#pragma once

#include "jobs/raster_job.hpp"
#include "jobs/vector_primitive_job.hpp"
#include "jobs/vector_text_job.hpp"

namespace jobs {

enum class JobType {
  Raster,
  VectorPrimitive,
  VectorText,
};

struct LoadedJob {
  JobType type;
  RasterJob rasterJob;
  VectorPrimitiveJob vectorPrimitiveJob;
  VectorTextJob vectorTextJob;
};

}  // namespace jobs
