#include "jobs/vector_primitive_job.hpp"

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

bool ParseShape(const std::string &value, PrimitiveShape &shape) {
  if (value == "star") {
    shape = PrimitiveShape::Star;
  } else if (value == "square") {
    shape = PrimitiveShape::Square;
  } else if (value == "circle") {
    shape = PrimitiveShape::Circle;
  } else if (value == "oval") {
    shape = PrimitiveShape::Oval;
  } else if (value == "triangle") {
    shape = PrimitiveShape::Triangle;
  } else if (value == "octagon") {
    shape = PrimitiveShape::Octagon;
  } else if (value == "diamond") {
    shape = PrimitiveShape::Diamond;
  } else {
    return false;
  }
  return true;
}

}  // namespace

esp_err_t ParseVectorPrimitiveJobManifest(const std::string &jobId, const std::string &json,
                                          VectorPrimitiveManifest &manifest) {
  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (root == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *version = GetNumber(root, "version");
  const cJSON *job_type = GetString(root, "jobType");
  const cJSON *primitive = GetObject(root, "primitive");
  const cJSON *motion = GetObject(root, "motion");
  const cJSON *laser = GetObject(root, "laser");
  const cJSON *origin = GetObject(root, "origin");

  const cJSON *shape = primitive != nullptr ? GetString(primitive, "shape") : nullptr;
  const cJSON *width_mm = primitive != nullptr ? GetNumber(primitive, "widthMm") : nullptr;
  const cJSON *height_mm = primitive != nullptr ? GetNumber(primitive, "heightMm") : nullptr;
  const cJSON *stroke_mm = primitive != nullptr ? GetNumber(primitive, "strokeMm") : nullptr;
  const cJSON *segments_per_circle = primitive != nullptr ? GetNumber(primitive, "segmentsPerCircle") : nullptr;
  const cJSON *travel_speed = motion != nullptr ? GetNumber(motion, "travelSpeedMmMin") : nullptr;
  const cJSON *print_speed = motion != nullptr ? GetNumber(motion, "printSpeedMmMin") : nullptr;
  const cJSON *power = laser != nullptr ? GetNumber(laser, "power") : nullptr;
  const cJSON *origin_mode = origin != nullptr ? GetString(origin, "mode") : nullptr;

  esp_err_t err = ESP_OK;
  if (version == nullptr || job_type == nullptr || primitive == nullptr || motion == nullptr || laser == nullptr ||
      shape == nullptr || width_mm == nullptr || height_mm == nullptr || stroke_mm == nullptr ||
      travel_speed == nullptr || print_speed == nullptr || power == nullptr) {
    err = ESP_ERR_INVALID_ARG;
  } else if (std::string(job_type->valuestring) != "vector-primitive") {
    err = ESP_ERR_NOT_SUPPORTED;
  }

  PrimitiveShape parsed_shape = PrimitiveShape::Square;
  if (err == ESP_OK && !ParseShape(shape->valuestring, parsed_shape)) {
    err = ESP_ERR_INVALID_ARG;
  }

  if (err == ESP_OK) {
    manifest.jobId = jobId;
    manifest.version = static_cast<uint32_t>(version->valueint);
    manifest.shape = parsed_shape;
    manifest.widthMm = static_cast<float>(width_mm->valuedouble);
    manifest.heightMm = static_cast<float>(height_mm->valuedouble);
    manifest.strokeMm = static_cast<float>(stroke_mm->valuedouble);
    manifest.travelSpeedMmMin = static_cast<float>(travel_speed->valuedouble);
    manifest.printSpeedMmMin = static_cast<float>(print_speed->valuedouble);
    manifest.power = static_cast<uint8_t>(std::clamp(power->valueint, 0, 255));
    manifest.segmentsPerCircle =
        segments_per_circle != nullptr ? static_cast<uint32_t>(std::max(12, segments_per_circle->valueint)) : 72U;
    manifest.originMode = origin_mode != nullptr ? origin_mode->valuestring : "top-left";

    if (manifest.widthMm <= 0.0f || manifest.heightMm <= 0.0f || manifest.strokeMm <= 0.0f ||
        manifest.travelSpeedMmMin <= 0.0f || manifest.printSpeedMmMin <= 0.0f || manifest.power == 0U) {
      err = ESP_ERR_INVALID_ARG;
    }
  }

  cJSON_Delete(root);
  return err;
}

const char *ToString(const PrimitiveShape shape) {
  switch (shape) {
    case PrimitiveShape::Star:
      return "star";
    case PrimitiveShape::Square:
      return "square";
    case PrimitiveShape::Circle:
      return "circle";
    case PrimitiveShape::Oval:
      return "oval";
    case PrimitiveShape::Triangle:
      return "triangle";
    case PrimitiveShape::Octagon:
      return "octagon";
    case PrimitiveShape::Diamond:
      return "diamond";
    default:
      return "unknown";
  }
}

}  // namespace jobs
