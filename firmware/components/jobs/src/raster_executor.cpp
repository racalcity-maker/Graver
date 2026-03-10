#include "jobs/raster_executor.hpp"

#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace jobs {

namespace {

constexpr const char *kTag = "raster";
constexpr TickType_t kMotionWaitSlice = pdMS_TO_TICKS(100);
constexpr TickType_t kPausePollInterval = pdMS_TO_TICKS(20);
constexpr uint32_t kExecutorYieldEverySegments = 32;
constexpr uint32_t kRowLogInterval = 16;
constexpr uint8_t kMinVisibleRasterPower = 4;

}  // namespace

RasterExecutor::RasterExecutor(motion::MotionService &motion, laser::LaserService &laser, storage::StorageService &storage)
    : motion_(motion), laser_(laser), storage_(storage) {}

esp_err_t RasterExecutor::execute(const RasterJob &job, std::atomic_bool &stopRequested, std::atomic_bool &pauseRequested,
                                  std::atomic_uint32_t &rowsDone, std::atomic_uint32_t &rowsTotal,
                                  std::atomic_uint8_t &progressPercent) {
  if (!motion_.isHomed()) {
    return ESP_ERR_INVALID_STATE;
  }

  const auto cleanup_laser = [this]() {
    (void)laser_.forceOff();
    (void)laser_.disarm();
  };

  const float pixel_width_mm = job.manifest.widthMm / static_cast<float>(job.manifest.widthPx);
  std::string row_data;
  const bool serpentine = job.manifest.bidirectional || job.manifest.serpentine;
  uint32_t segment_counter = 0;
  rowsDone.store(0);
  rowsTotal.store(job.manifest.heightPx);
  progressPercent.store(0);

  ESP_LOGI(kTag,
           "Run job id=%s size=%.3fx%.3fmm raster=%ux%u lineStep=%.4f print=%.1f travel=%.1f power=%u..%u serpentine=%s",
           job.manifest.jobId.c_str(), static_cast<double>(job.manifest.widthMm), static_cast<double>(job.manifest.heightMm),
           static_cast<unsigned>(job.manifest.widthPx), static_cast<unsigned>(job.manifest.heightPx),
           static_cast<double>(job.manifest.lineStepMm), static_cast<double>(job.manifest.printSpeedMmMin),
           static_cast<double>(job.manifest.travelSpeedMmMin), static_cast<unsigned>(job.manifest.minPower),
           static_cast<unsigned>(job.manifest.maxPower), serpentine ? "true" : "false");

  esp_err_t err = laser_.arm();
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }
  err = laser_.forceOff();
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }
  err = moveAndWait(0.0f, 0.0f, job.manifest.travelSpeedMmMin, stopRequested, pauseRequested);
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }

  for (uint32_t row = 0; row < job.manifest.heightPx; ++row) {
    if (stopRequested.load()) {
      cleanup_laser();
      return ESP_ERR_INVALID_STATE;
    }

    const float row_y_mm = static_cast<float>(row) * job.manifest.lineStepMm;

    err = storage_.loadJobRasterRow(job.jobId, row, job.manifest.widthPx, row_data);
    if (err != ESP_OK || row_data.size() != job.manifest.widthPx) {
      cleanup_laser();
      ESP_LOGE(kTag, "Failed to load raster row %u", static_cast<unsigned>(row));
      return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }

    std::vector<uint8_t> effective_row_power;
    effective_row_power.resize(job.manifest.widthPx);

    uint32_t first_active = job.manifest.widthPx;
    uint32_t last_active = 0;
    for (uint32_t col = 0; col < job.manifest.widthPx; ++col) {
      uint8_t mapped_power = mapPower(static_cast<uint8_t>(row_data[col]), job.manifest);
      if (!job.manifest.binaryPower && mapped_power < kMinVisibleRasterPower) {
        mapped_power = 0;
      }
      effective_row_power[col] = mapped_power;
      if (mapped_power != 0U) {
        first_active = std::min(first_active, col);
        last_active = col;
      }
    }

    if ((row % kRowLogInterval) == 0U || row == 0U || row + 1U == job.manifest.heightPx) {
      ESP_LOGI(kTag, "Raster row %u/%u y=%.3f active=%s", static_cast<unsigned>(row + 1U),
               static_cast<unsigned>(job.manifest.heightPx), static_cast<double>(row_y_mm),
               first_active < job.manifest.widthPx ? "yes" : "no");
    }

    if (first_active >= job.manifest.widthPx) {
      err = laser_.forceOff();
      if (err != ESP_OK) {
        cleanup_laser();
        return err;
      }
      const uint32_t completed_rows = row + 1U;
      rowsDone.store(completed_rows);
      progressPercent.store(static_cast<uint8_t>((completed_rows * 100U) / std::max<uint32_t>(1U, job.manifest.heightPx)));
      continue;
    }

    const bool reverse = serpentine && ((row & 1U) == 1U);
    const float row_start_x_mm = reverse ? ((static_cast<float>(last_active) + 1.0f) * pixel_width_mm)
                                         : (static_cast<float>(first_active) * pixel_width_mm);

    err = laser_.forceOff();
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }
    err = moveAndWait(row_start_x_mm, row_y_mm, job.manifest.travelSpeedMmMin, stopRequested, pauseRequested);
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }

    err = waitWhilePaused(stopRequested, pauseRequested, row, 0);
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }

    std::vector<motion::RasterSegment> segments;
    segments.reserve(32);
    uint32_t logical_col = reverse ? last_active : first_active;
    const uint32_t logical_end = reverse ? first_active : last_active;
    while (reverse ? (logical_col >= logical_end) : (logical_col <= logical_end)) {
      const uint32_t source_col = logical_col;
      const uint8_t mapped_power = effective_row_power[source_col];

      uint32_t segment_end = logical_col;
      while (true) {
        const uint32_t next_source_col = reverse ? (segment_end == 0U ? UINT32_MAX : segment_end - 1U) : (segment_end + 1U);
        const bool in_bounds = reverse ? (segment_end > logical_end) : (segment_end < logical_end);
        if (!in_bounds) {
          break;
        }
        if (effective_row_power[next_source_col] != mapped_power) {
          break;
        }
        segment_end = next_source_col;
      }

      const uint32_t segment_pixels = reverse ? (logical_col - segment_end + 1U) : (segment_end - logical_col + 1U);
      const float segment_mm = static_cast<float>(segment_pixels) * pixel_width_mm;
      if (segment_mm > 0.0f) {
        segments.push_back({segment_mm, mapped_power});
      }
      if (reverse) {
        if (segment_end == 0U || segment_end <= logical_end) {
          break;
        }
        logical_col = segment_end - 1U;
      } else {
        if (segment_end >= logical_end) {
          break;
        }
        logical_col = segment_end + 1U;
      }

      ++segment_counter;
      if ((segment_counter % kExecutorYieldEverySegments) == 0U) {
        vTaskDelay(1);
      }
    }

    if (!segments.empty()) {
      err = motion_.executeRasterRow(row_y_mm, row_start_x_mm, reverse, segments.data(), segments.size(),
                                     job.manifest.printSpeedMmMin, job.manifest.travelSpeedMmMin, laser_);
    } else {
      err = ESP_OK;
    }
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }

    err = laser_.forceOff();
    if (err != ESP_OK) {
      cleanup_laser();
      return err;
    }
    if (!serpentine) {
      err = moveAndWait(static_cast<float>(first_active) * pixel_width_mm, row_y_mm, job.manifest.travelSpeedMmMin,
                        stopRequested, pauseRequested);
      if (err != ESP_OK) {
        cleanup_laser();
        return err;
      }
    }

    const uint32_t completed_rows = row + 1U;
    rowsDone.store(completed_rows);
    progressPercent.store(static_cast<uint8_t>((completed_rows * 100U) / std::max<uint32_t>(1U, job.manifest.heightPx)));
  }

  err = laser_.forceOff();
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }
  err = laser_.disarm();
  if (err != ESP_OK) {
    cleanup_laser();
    return err;
  }
  err = moveAndWait(0.0f, 0.0f, job.manifest.travelSpeedMmMin, stopRequested, pauseRequested);
  if (err == ESP_OK) {
    rowsDone.store(job.manifest.heightPx);
    progressPercent.store(100);
  }
  return err;
}

esp_err_t RasterExecutor::moveAndWait(const float xMm, const float yMm, const float feedMmMin,
                                      std::atomic_bool &stopRequested, std::atomic_bool &pauseRequested) {
  if (stopRequested.load()) {
    motion_.stop();
    return ESP_ERR_INVALID_STATE;
  }
  ESP_RETURN_ON_ERROR(motion_.moveTo(xMm, yMm, feedMmMin), kTag, "Failed to queue raster move");
  ESP_RETURN_ON_ERROR(waitForMotionIdle(stopRequested), kTag, "Failed while waiting for raster move to finish");
  return waitWhilePaused(stopRequested, pauseRequested, 0, 0);
}

esp_err_t RasterExecutor::waitForMotionIdle(std::atomic_bool &stopRequested) {
  for (;;) {
    const esp_err_t wait_err = motion_.waitForIdle(kMotionWaitSlice);
    if (wait_err == ESP_OK) {
      return motion_.lastError();
    }
    if (wait_err != ESP_ERR_TIMEOUT) {
      return wait_err;
    }

    if (stopRequested.load()) {
      motion_.stop();
      return ESP_ERR_INVALID_STATE;
    }
  }
}

esp_err_t RasterExecutor::waitWhilePaused(std::atomic_bool &stopRequested, std::atomic_bool &pauseRequested,
                                          const uint32_t row, const uint32_t logicalCol) {
  if (!pauseRequested.load()) {
    return ESP_OK;
  }

  ESP_LOGW(kTag, "Raster paused at row=%u col=%u", static_cast<unsigned>(row), static_cast<unsigned>(logicalCol));
  (void)laser_.forceOff();
  while (pauseRequested.load()) {
    if (stopRequested.load()) {
      motion_.stop();
      return ESP_ERR_INVALID_STATE;
    }
    vTaskDelay(kPausePollInterval);
  }
  ESP_LOGI(kTag, "Raster resumed");
  return ESP_OK;
}

uint8_t RasterExecutor::mapPower(const uint8_t pixelPower, const RasterJobManifest &manifest) const {
  if (pixelPower == 0 || manifest.maxPower == 0) {
    return 0;
  }
  if (manifest.binaryPower) {
    return manifest.maxPower;
  }

  const uint32_t min_power = std::min<uint32_t>(manifest.minPower, manifest.maxPower);
  const uint32_t max_power = std::max<uint32_t>(manifest.minPower, manifest.maxPower);
  const uint32_t range = max_power - min_power;
  return static_cast<uint8_t>(min_power + ((static_cast<uint32_t>(pixelPower) * range) / 255U));
}

}  // namespace jobs
