#pragma once

#include "control/control_service.hpp"
#include "grbl/grbl_service.hpp"
#include "hal/board_hal.hpp"
#include "jobs/job_manager.hpp"
#include "laser/laser_service.hpp"
#include "motion/motion_service.hpp"
#include "network/network_service.hpp"
#include "shared/machine_config.hpp"
#include "storage/storage_service.hpp"
#include "web/web_server.hpp"

#include "esp_err.h"

#include <memory>

namespace app {

class Application {
 public:
  Application();

  esp_err_t start();

 private:
  esp_err_t initializeSystemServices();
  esp_err_t loadMachineConfig();

  shared::MachineConfig config_;
  hal::BoardHal boardHal_;
  storage::StorageService storage_;
  std::unique_ptr<network::NetworkService> network_;
  std::unique_ptr<jobs::JobManager> jobs_;
  std::unique_ptr<motion::MotionService> motion_;
  std::unique_ptr<laser::LaserService> laser_;
  std::unique_ptr<control::ControlService> control_;
  std::unique_ptr<grbl::GrblService> grbl_;
  std::unique_ptr<web::WebServer> web_;
};

}  // namespace app
