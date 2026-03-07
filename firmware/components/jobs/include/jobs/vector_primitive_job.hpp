#pragma once

#include "esp_err.h"

#include <cstdint>
#include <string>

namespace jobs {

enum class PrimitiveShape {
  Star,
  Square,
  Circle,
  Oval,
  Triangle,
  Octagon,
  Diamond,
};

struct VectorPrimitiveManifest {
  std::string jobId;
  uint32_t version;
  PrimitiveShape shape;
  float widthMm;
  float heightMm;
  float strokeMm;
  float travelSpeedMmMin;
  float printSpeedMmMin;
  uint8_t power;
  uint32_t segmentsPerCircle;
  std::string originMode;
};

struct VectorPrimitiveJob {
  VectorPrimitiveManifest manifest;
};

esp_err_t ParseVectorPrimitiveJobManifest(const std::string &jobId, const std::string &json,
                                          VectorPrimitiveManifest &manifest);

const char *ToString(PrimitiveShape shape);

}  // namespace jobs
