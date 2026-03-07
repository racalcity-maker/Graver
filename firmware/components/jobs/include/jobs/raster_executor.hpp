#pragma once

#include "jobs/raster_job.hpp"
#include "laser/laser_service.hpp"
#include "motion/motion_service.hpp"
#include "storage/storage_service.hpp"

#include "esp_err.h"

#include <atomic>

namespace jobs {

class RasterExecutor {
 public:
  RasterExecutor(motion::MotionService &motion, laser::LaserService &laser, storage::StorageService &storage);

  esp_err_t execute(const RasterJob &job, std::atomic_bool &stopRequested, std::atomic_bool &pauseRequested,
                    std::atomic_uint32_t &rowsDone, std::atomic_uint32_t &rowsTotal,
                    std::atomic_uint8_t &progressPercent);

 private:
  esp_err_t moveAndWait(float xMm, float yMm, float feedMmMin, std::atomic_bool &stopRequested,
                        std::atomic_bool &pauseRequested);
  esp_err_t waitForMotionIdle(std::atomic_bool &stopRequested);
  esp_err_t waitWhilePaused(std::atomic_bool &stopRequested, std::atomic_bool &pauseRequested, uint32_t row,
                            uint32_t logicalCol);
  uint8_t mapPower(uint8_t pixelPower, const RasterJobManifest &manifest) const;

  motion::MotionService &motion_;
  laser::LaserService &laser_;
  storage::StorageService &storage_;
};

}  // namespace jobs
