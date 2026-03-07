#pragma once

#include "esp_err.h"

#include <cstdint>
#include <string>

namespace jobs {

struct RasterJobManifest {
  std::string jobId;
  uint32_t version;
  uint32_t widthPx;
  uint32_t heightPx;
  float widthMm;
  float heightMm;
  float lineStepMm;
  float travelSpeedMmMin;
  float printSpeedMmMin;
  float overscanMm;
  uint8_t minPower;
  uint8_t maxPower;
  bool bidirectional;
  bool serpentine;
  bool binaryPower;
  std::string originMode;
};

struct RasterJob {
  RasterJobManifest manifest;
  std::string jobId;
  std::string rasterData;
};

esp_err_t ParseRasterJobManifest(const std::string &jobId, const std::string &json, RasterJobManifest &manifest);

}  // namespace jobs
