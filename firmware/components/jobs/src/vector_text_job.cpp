#include "jobs/vector_text_job.hpp"

#include "cJSON.h"

#include <algorithm>

namespace jobs {

namespace {

const cJSON *GetObject(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsObject(item) ? item : nullptr;
}

const cJSON *GetArray(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsArray(item) ? item : nullptr;
}

const cJSON *GetNumber(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsNumber(item) ? item : nullptr;
}

const cJSON *GetString(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsString(item) ? item : nullptr;
}

}  // namespace

esp_err_t ParseVectorTextJobManifest(const std::string &jobId, const std::string &json, VectorTextManifest &manifest) {
  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (root == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *version = GetNumber(root, "version");
  const cJSON *job_type = GetString(root, "jobType");
  const cJSON *text = GetObject(root, "text");
  const cJSON *motion = GetObject(root, "motion");
  const cJSON *laser = GetObject(root, "laser");
  const cJSON *origin = GetObject(root, "origin");

  const cJSON *value = text != nullptr ? GetString(text, "value") : nullptr;
  const cJSON *width_mm = text != nullptr ? GetNumber(text, "widthMm") : nullptr;
  const cJSON *height_mm = text != nullptr ? GetNumber(text, "heightMm") : nullptr;
  const cJSON *stroke_mm = text != nullptr ? GetNumber(text, "strokeMm") : nullptr;
  const cJSON *strokes = text != nullptr ? GetArray(text, "strokes") : nullptr;
  const cJSON *travel_speed = motion != nullptr ? GetNumber(motion, "travelSpeedMmMin") : nullptr;
  const cJSON *print_speed = motion != nullptr ? GetNumber(motion, "printSpeedMmMin") : nullptr;
  const cJSON *power = laser != nullptr ? GetNumber(laser, "power") : nullptr;
  const cJSON *origin_mode = origin != nullptr ? GetString(origin, "mode") : nullptr;

  esp_err_t err = ESP_OK;
  if (version == nullptr || job_type == nullptr || text == nullptr || motion == nullptr || laser == nullptr ||
      value == nullptr || width_mm == nullptr || height_mm == nullptr || stroke_mm == nullptr || strokes == nullptr ||
      travel_speed == nullptr || print_speed == nullptr || power == nullptr) {
    err = ESP_ERR_INVALID_ARG;
  } else if (std::string(job_type->valuestring) != "vector-text") {
    err = ESP_ERR_NOT_SUPPORTED;
  }

  if (err == ESP_OK) {
    manifest.jobId = jobId;
    manifest.version = static_cast<uint32_t>(version->valueint);
    manifest.text = value->valuestring;
    manifest.widthMm = static_cast<float>(width_mm->valuedouble);
    manifest.heightMm = static_cast<float>(height_mm->valuedouble);
    manifest.strokeMm = static_cast<float>(stroke_mm->valuedouble);
    manifest.travelSpeedMmMin = static_cast<float>(travel_speed->valuedouble);
    manifest.printSpeedMmMin = static_cast<float>(print_speed->valuedouble);
    manifest.power = static_cast<uint8_t>(std::clamp(power->valueint, 0, 255));
    manifest.originMode = origin_mode != nullptr ? origin_mode->valuestring : "top-left";

    const int stroke_count = cJSON_GetArraySize(strokes);
    for (int stroke_index = 0; stroke_index < stroke_count; ++stroke_index) {
      const cJSON *stroke = cJSON_GetArrayItem(strokes, stroke_index);
      if (!cJSON_IsArray(stroke)) {
        err = ESP_ERR_INVALID_ARG;
        break;
      }

      VectorTextStroke parsed_stroke{};
      const int point_count = cJSON_GetArraySize(stroke);
      for (int point_index = 0; point_index < point_count; ++point_index) {
        const cJSON *point = cJSON_GetArrayItem(stroke, point_index);
        if (!cJSON_IsArray(point) || cJSON_GetArraySize(point) < 2) {
          err = ESP_ERR_INVALID_ARG;
          break;
        }

        const cJSON *x = cJSON_GetArrayItem(point, 0);
        const cJSON *y = cJSON_GetArrayItem(point, 1);
        if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) {
          err = ESP_ERR_INVALID_ARG;
          break;
        }

        parsed_stroke.points.push_back({static_cast<float>(x->valuedouble), static_cast<float>(y->valuedouble)});
      }

      if (err != ESP_OK) {
        break;
      }
      if (parsed_stroke.points.size() >= 2U) {
        manifest.strokes.push_back(std::move(parsed_stroke));
      }
    }

    if (manifest.widthMm <= 0.0f || manifest.heightMm <= 0.0f || manifest.strokeMm <= 0.0f ||
        manifest.travelSpeedMmMin <= 0.0f || manifest.printSpeedMmMin <= 0.0f || manifest.power == 0U ||
        manifest.strokes.empty()) {
      err = ESP_ERR_INVALID_ARG;
    }
  }

  cJSON_Delete(root);
  return err;
}

}  // namespace jobs
