#pragma once

#include "jobs/vector_primitive_job.hpp"
#include "laser/laser_service.hpp"
#include "motion/motion_service.hpp"

#include "esp_err.h"

#include <atomic>

namespace jobs {

class VectorPrimitiveExecutor {
 public:
  VectorPrimitiveExecutor(motion::MotionService &motion, laser::LaserService &laser);

  esp_err_t execute(const VectorPrimitiveJob &job, std::atomic_bool &stopRequested, std::atomic_bool &pauseRequested,
                    std::atomic_uint32_t &unitsDone, std::atomic_uint32_t &unitsTotal,
                    std::atomic_uint8_t &progressPercent);

 private:
  motion::MotionService &motion_;
  laser::LaserService &laser_;
};

}  // namespace jobs
