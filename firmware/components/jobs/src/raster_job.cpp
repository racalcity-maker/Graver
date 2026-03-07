#include "jobs/raster_job.hpp"

#include "cJSON.h"

#include <algorithm>

namespace jobs {

namespace {

const cJSON *GetObject(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsObject(item) ? item : nullptr;
}

const cJSON *GetNumber(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsNumber(item) ? item : nullptr;
}

const cJSON *GetString(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsString(item) ? item : nullptr;
}

bool ParseBoolOrDefault(const cJSON *parent, const char *name, const bool fallback) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

}  // namespace

esp_err_t ParseRasterJobManifest(const std::string &jobId, const std::string &json, RasterJobManifest &manifest) {
  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (root == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *version = GetNumber(root, "version");
  const cJSON *job_type = GetString(root, "jobType");
  const cJSON *output = GetObject(root, "output");
  const cJSON *raster = GetObject(root, "raster");
  const cJSON *motion = GetObject(root, "motion");
  const cJSON *laser = GetObject(root, "laser");
  const cJSON *origin = GetObject(root, "origin");

  const cJSON *width_mm = output != nullptr ? GetNumber(output, "widthMm") : nullptr;
  const cJSON *height_mm = output != nullptr ? GetNumber(output, "heightMm") : nullptr;
  const cJSON *width_px = raster != nullptr ? GetNumber(raster, "widthPx") : nullptr;
  const cJSON *height_px = raster != nullptr ? GetNumber(raster, "heightPx") : nullptr;
  const cJSON *bytes_per_pixel = raster != nullptr ? GetNumber(raster, "bytesPerPixel") : nullptr;
  const cJSON *encoding = raster != nullptr ? GetString(raster, "encoding") : nullptr;
  const cJSON *travel_speed = motion != nullptr ? GetNumber(motion, "travelSpeedMmMin") : nullptr;
  const cJSON *print_speed = motion != nullptr ? GetNumber(motion, "printSpeedMmMin") : nullptr;
  const cJSON *overscan = motion != nullptr ? GetNumber(motion, "overscanMm") : nullptr;
  const cJSON *line_step = motion != nullptr ? GetNumber(motion, "lineStepMm") : nullptr;
  const cJSON *min_power = laser != nullptr ? GetNumber(laser, "minPower") : nullptr;
  const cJSON *max_power = laser != nullptr ? GetNumber(laser, "maxPower") : nullptr;
  const cJSON *pwm_mode = laser != nullptr ? GetString(laser, "pwmMode") : nullptr;
  const cJSON *origin_mode = origin != nullptr ? GetString(origin, "mode") : nullptr;

  esp_err_t err = ESP_OK;
  if (version == nullptr || job_type == nullptr || output == nullptr || raster == nullptr || motion == nullptr ||
      laser == nullptr || width_mm == nullptr || height_mm == nullptr || width_px == nullptr || height_px == nullptr ||
      bytes_per_pixel == nullptr || encoding == nullptr || travel_speed == nullptr || print_speed == nullptr ||
      overscan == nullptr || line_step == nullptr || min_power == nullptr || max_power == nullptr) {
    err = ESP_ERR_INVALID_ARG;
  } else if (std::string(job_type->valuestring) != "raster") {
    err = ESP_ERR_NOT_SUPPORTED;
  } else if (bytes_per_pixel->valueint != 1 || std::string(encoding->valuestring) != "gray8-row-major") {
    err = ESP_ERR_NOT_SUPPORTED;
  }

  if (err == ESP_OK) {
    manifest.jobId = jobId;
    manifest.version = static_cast<uint32_t>(version->valueint);
    manifest.widthPx = static_cast<uint32_t>(width_px->valueint);
    manifest.heightPx = static_cast<uint32_t>(height_px->valueint);
    manifest.widthMm = static_cast<float>(width_mm->valuedouble);
    manifest.heightMm = static_cast<float>(height_mm->valuedouble);
    manifest.lineStepMm = static_cast<float>(line_step->valuedouble);
    manifest.travelSpeedMmMin = static_cast<float>(travel_speed->valuedouble);
    manifest.printSpeedMmMin = static_cast<float>(print_speed->valuedouble);
    manifest.overscanMm = static_cast<float>(overscan->valuedouble);
    manifest.minPower = static_cast<uint8_t>(std::clamp(min_power->valueint, 0, 255));
    manifest.maxPower = static_cast<uint8_t>(std::clamp(max_power->valueint, 0, 255));
    manifest.bidirectional = ParseBoolOrDefault(raster, "bidirectional", false);
    manifest.serpentine = ParseBoolOrDefault(raster, "serpentine", false);
    manifest.binaryPower = pwm_mode != nullptr && std::string(pwm_mode->valuestring) == "binary";
    manifest.originMode = origin_mode != nullptr ? origin_mode->valuestring : "top-left";

    if (manifest.widthPx == 0 || manifest.heightPx == 0 || manifest.widthMm <= 0.0f || manifest.heightMm <= 0.0f ||
        manifest.lineStepMm <= 0.0f || manifest.travelSpeedMmMin <= 0.0f || manifest.printSpeedMmMin <= 0.0f) {
      err = ESP_ERR_INVALID_ARG;
    }
  }

  cJSON_Delete(root);
  return err;
}

}  // namespace jobs
