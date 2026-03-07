#pragma once

#include "esp_err.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jobs {

struct VectorTextStrokePoint {
  float xMm;
  float yMm;
};

struct VectorTextStroke {
  std::vector<VectorTextStrokePoint> points;
};

struct VectorTextManifest {
  std::string jobId;
  uint32_t version;
  std::string text;
  float widthMm;
  float heightMm;
  float strokeMm;
  float travelSpeedMmMin;
  float printSpeedMmMin;
  uint8_t power;
  std::string originMode;
  std::vector<VectorTextStroke> strokes;
};

struct VectorTextJob {
  VectorTextManifest manifest;
};

esp_err_t ParseVectorTextJobManifest(const std::string &jobId, const std::string &json, VectorTextManifest &manifest);

}  // namespace jobs
