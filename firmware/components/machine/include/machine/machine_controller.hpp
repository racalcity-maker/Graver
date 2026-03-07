#pragma once

#include "jobs/job_manager.hpp"
#include "laser/laser_service.hpp"
#include "motion/motion_service.hpp"
#include "shared/machine_config.hpp"
#include "shared/types.hpp"

#include "esp_err.h"

#include <string>

namespace machine {

class MachineController {
 public:
  MachineController(const shared::MachineConfig &config, motion::MotionService &motion, laser::LaserService &laser,
                    jobs::JobManager &jobs);

  esp_err_t start();
  esp_err_t home();
  esp_err_t frame();
  esp_err_t startJob(const std::string &jobId);
  esp_err_t pause();
  esp_err_t resume();
  esp_err_t stop();
  esp_err_t alarm(const std::string &message);

  shared::MachineStatus status();

 private:
  void refreshStateFromMotion();
  void setState(shared::MachineState state, const std::string &message);

  shared::MachineConfig config_;
  motion::MotionService &motion_;
  laser::LaserService &laser_;
  jobs::JobManager &jobs_;
  shared::MachineState state_;
  std::string activeJobId_;
  std::string message_;
};

}  // namespace machine
