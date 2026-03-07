#include "jobs/vector_job_executor.hpp"

#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace jobs {

namespace {

constexpr const char *kTag = "vector";
constexpr TickType_t kMotionWaitSlice = pdMS_TO_TICKS(100);
constexpr TickType_t kPausePollInterval = pdMS_TO_TICKS(20);
constexpr float kDefaultContourSpacingMm = 0.25f;
constexpr float kPi = 3.14159265358979323846f;

struct PointMm {
  float x;
  float y;
};

using Path = std::vector<PointMm>;

float ClampPositive(const float value, const float minimum) {
  return value < minimum ? minimum : value;
}

std::vector<PointMm> makeRegularPolygon(const uint32_t sides, const float centerX, const float centerY, const float rx,
                                        const float ry, const float rotationRad) {
  std::vector<PointMm> points;
  points.reserve(sides);
  for (uint32_t index = 0; index < sides; ++index) {
    const float angle = rotationRad + ((2.0f * kPi * static_cast<float>(index)) / sides);
    points.push_back({centerX + std::cos(angle) * rx, centerY + std::sin(angle) * ry});
  }
  return points;
}

std::vector<PointMm> makeStar(const float centerX, const float centerY, const float outerRx, const float outerRy,
                              const float innerRatio) {
  std::vector<PointMm> points;
  points.reserve(10);
  for (uint32_t index = 0; index < 10U; ++index) {
    const float angle = (-kPi / 2.0f) + ((kPi * static_cast<float>(index)) / 5.0f);
    const bool outer = (index % 2U) == 0U;
    const float rx = outer ? outerRx : outerRx * innerRatio;
    const float ry = outer ? outerRy : outerRy * innerRatio;
    points.push_back({centerX + std::cos(angle) * rx, centerY + std::sin(angle) * ry});
  }
  return points;
}

std::vector<PointMm> makeEllipse(const float centerX, const float centerY, const float rx, const float ry,
                                 const uint32_t segments) {
  std::vector<PointMm> points;
  points.reserve(segments);
  for (uint32_t index = 0; index < segments; ++index) {
    const float angle = (2.0f * kPi * static_cast<float>(index)) / static_cast<float>(segments);
    points.push_back({centerX + std::cos(angle) * rx, centerY + std::sin(angle) * ry});
  }
  return points;
}

std::vector<PointMm> makeContour(const VectorPrimitiveManifest &manifest, const float insetMm) {
  const float width = manifest.widthMm - (insetMm * 2.0f);
  const float height = manifest.heightMm - (insetMm * 2.0f);
  if (width <= 0.0f || height <= 0.0f) {
    return {};
  }

  const float left = insetMm;
  const float top = insetMm;
  const float right = left + width;
  const float bottom = top + height;
  const float centerX = manifest.widthMm / 2.0f;
  const float centerY = manifest.heightMm / 2.0f;
  const float rx = width / 2.0f;
  const float ry = height / 2.0f;

  switch (manifest.shape) {
    case PrimitiveShape::Square:
      return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    case PrimitiveShape::Triangle:
      return makeRegularPolygon(3, centerX, centerY, rx, ry, -kPi / 2.0f);
    case PrimitiveShape::Octagon:
      return makeRegularPolygon(8, centerX, centerY, rx, ry, kPi / 8.0f);
    case PrimitiveShape::Diamond:
      return makeRegularPolygon(4, centerX, centerY, rx, ry, 0.0f);
    case PrimitiveShape::Star:
      return makeStar(centerX, centerY, rx, ry, 0.45f);
    case PrimitiveShape::Circle:
    case PrimitiveShape::Oval:
      return makeEllipse(centerX, centerY, rx, ry, manifest.segmentsPerCircle);
    default:
      return {};
  }
}

esp_err_t waitForMotionIdle(motion::MotionService &motion, std::atomic_bool &stopRequested) {
  for (;;) {
    const esp_err_t wait_err = motion.waitForIdle(kMotionWaitSlice);
    if (wait_err == ESP_OK) {
      return motion.lastError();
    }
    if (wait_err != ESP_ERR_TIMEOUT) {
      return wait_err;
    }
    if (stopRequested.load()) {
      motion.stop();
      return ESP_ERR_INVALID_STATE;
    }
  }
}

esp_err_t moveAndWait(motion::MotionService &motion, const float xMm, const float yMm, const float feedMmMin,
                      std::atomic_bool &stopRequested) {
  ESP_RETURN_ON_ERROR(motion.moveTo(xMm, yMm, feedMmMin), kTag, "Failed to queue vector move");
  return waitForMotionIdle(motion, stopRequested);
}

esp_err_t waitWhilePaused(laser::LaserService &laser, motion::MotionService &motion, std::atomic_bool &stopRequested,
                          std::atomic_bool &pauseRequested) {
  if (!pauseRequested.load()) {
    return ESP_OK;
  }
  (void)laser.forceOff();
  while (pauseRequested.load()) {
    if (stopRequested.load()) {
      motion.stop();
      return ESP_ERR_INVALID_STATE;
    }
    vTaskDelay(kPausePollInterval);
  }
  return ESP_OK;
}

esp_err_t executePaths(motion::MotionService &motion, laser::LaserService &laser, const std::vector<Path> &paths,
                       const bool closePaths, const float travelSpeedMmMin, const float printSpeedMmMin,
                       const uint8_t power, std::atomic_bool &stopRequested, std::atomic_bool &pauseRequested,
                       std::atomic_uint32_t &unitsDone, std::atomic_uint32_t &unitsTotal,
                       std::atomic_uint8_t &progressPercent) {
  const auto cleanup_laser = [&laser]() {
    (void)laser.forceOff();
    (void)laser.disarm();
  };

  uint32_t total_segments = 0;
  for (const Path &path : paths) {
    if (path.size() >= 2U) {
      total_segments += static_cast<uint32_t>(path.size() - 1U);
      if (closePaths) {
        total_segments += 1U;
      }
    }
  }
  if (total_segments == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  unitsDone.store(0);
  unitsTotal.store(total_segments);
  progressPercent.store(0);

  esp_err_t err = laser.arm();
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }
  err = laser.forceOff();
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }
  err = moveAndWait(motion, 0.0f, 0.0f, travelSpeedMmMin, stopRequested);
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }

  uint32_t completed_segments = 0;
  for (const Path &path : paths) {
    if (path.size() < 2U) {
      continue;
    }
    if (stopRequested.load()) {
      cleanup_laser();
      return ESP_ERR_INVALID_STATE;
    }

    err = laser.forceOff();
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }
    err = moveAndWait(motion, path.front().x, path.front().y, travelSpeedMmMin, stopRequested);
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }
    err = waitWhilePaused(laser, motion, stopRequested, pauseRequested);
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }
    err = laser.setPower(power);
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }

    for (size_t point_index = 1; point_index < path.size(); ++point_index) {
      const PointMm &point = path[point_index];
      err = moveAndWait(motion, point.x, point.y, printSpeedMmMin, stopRequested);
      if (err != ESP_OK) {
        cleanup_laser();
        return err;
      }
      ++completed_segments;
      unitsDone.store(completed_segments);
      progressPercent.store(static_cast<uint8_t>((completed_segments * 100U) / std::max<uint32_t>(1U, total_segments)));
      err = waitWhilePaused(laser, motion, stopRequested, pauseRequested);
      if (err != ESP_OK) {
        cleanup_laser();
        return err;
      }
    }

    if (closePaths) {
      const PointMm &point = path.front();
      err = moveAndWait(motion, point.x, point.y, printSpeedMmMin, stopRequested);
      if (err != ESP_OK) {
        cleanup_laser();
        return err;
      }
      ++completed_segments;
      unitsDone.store(completed_segments);
      progressPercent.store(static_cast<uint8_t>((completed_segments * 100U) / std::max<uint32_t>(1U, total_segments)));
      err = waitWhilePaused(laser, motion, stopRequested, pauseRequested);
      if (err != ESP_OK) {
        cleanup_laser();
        return err;
      }
    }

    err = laser.forceOff();
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }
  }

  err = laser.disarm();
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }
  err = moveAndWait(motion, 0.0f, 0.0f, travelSpeedMmMin, stopRequested);
  if (err == ESP_OK) {
    unitsDone.store(total_segments);
    progressPercent.store(100);
  }
  return err;
}

}  // namespace

VectorJobExecutor::VectorJobExecutor(motion::MotionService &motion, laser::LaserService &laser)
    : motion_(motion), laser_(laser) {}

esp_err_t VectorJobExecutor::execute(const VectorPrimitiveJob &job, std::atomic_bool &stopRequested,
                                     std::atomic_bool &pauseRequested, std::atomic_uint32_t &unitsDone,
                                     std::atomic_uint32_t &unitsTotal, std::atomic_uint8_t &progressPercent) {
  if (!motion_.isHomed()) {
    return ESP_ERR_INVALID_STATE;
  }

  const float spacing = ClampPositive(std::min(job.manifest.strokeMm, kDefaultContourSpacingMm), 0.1f);
  const uint32_t contour_count = std::max<uint32_t>(1U, static_cast<uint32_t>(std::ceil(job.manifest.strokeMm / spacing)));
  std::vector<Path> paths;
  paths.reserve(contour_count);
  for (uint32_t contour_index = 0; contour_index < contour_count; ++contour_index) {
    const float inset = (static_cast<float>(contour_index) * spacing) + (spacing * 0.5f);
    Path path = makeContour(job.manifest, inset);
    if (path.size() < 2U) {
      break;
    }
    paths.push_back(std::move(path));
  }
  ESP_LOGI(kTag, "Run vector primitive id=%s shape=%s size=%.3fx%.3f stroke=%.3f contours=%u power=%u print=%.1f travel=%.1f",
           job.manifest.jobId.c_str(), ToString(job.manifest.shape), static_cast<double>(job.manifest.widthMm),
           static_cast<double>(job.manifest.heightMm), static_cast<double>(job.manifest.strokeMm),
           static_cast<unsigned>(paths.size()), static_cast<unsigned>(job.manifest.power),
           static_cast<double>(job.manifest.printSpeedMmMin), static_cast<double>(job.manifest.travelSpeedMmMin));
  return executePaths(motion_, laser_, paths, true, job.manifest.travelSpeedMmMin, job.manifest.printSpeedMmMin,
                      job.manifest.power, stopRequested, pauseRequested, unitsDone, unitsTotal, progressPercent);
}

esp_err_t VectorJobExecutor::execute(const VectorTextJob &job, std::atomic_bool &stopRequested,
                                     std::atomic_bool &pauseRequested, std::atomic_uint32_t &unitsDone,
                                     std::atomic_uint32_t &unitsTotal, std::atomic_uint8_t &progressPercent) {
  if (!motion_.isHomed()) {
    return ESP_ERR_INVALID_STATE;
  }

  std::vector<Path> paths;
  paths.reserve(job.manifest.strokes.size());
  for (const VectorTextStroke &stroke : job.manifest.strokes) {
    Path path;
    path.reserve(stroke.points.size());
    for (const VectorTextStrokePoint &point : stroke.points) {
      path.push_back({point.xMm, point.yMm});
    }
    if (path.size() >= 2U) {
      paths.push_back(std::move(path));
    }
  }
  ESP_LOGI(kTag, "Run vector text id=%s text=\"%s\" size=%.3fx%.3f stroke=%.3f paths=%u power=%u print=%.1f travel=%.1f",
           job.manifest.jobId.c_str(), job.manifest.text.c_str(), static_cast<double>(job.manifest.widthMm),
           static_cast<double>(job.manifest.heightMm), static_cast<double>(job.manifest.strokeMm),
           static_cast<unsigned>(paths.size()), static_cast<unsigned>(job.manifest.power),
           static_cast<double>(job.manifest.printSpeedMmMin), static_cast<double>(job.manifest.travelSpeedMmMin));
  return executePaths(motion_, laser_, paths, false, job.manifest.travelSpeedMmMin, job.manifest.printSpeedMmMin,
                      job.manifest.power, stopRequested, pauseRequested, unitsDone, unitsTotal, progressPercent);
}

}  // namespace jobs
