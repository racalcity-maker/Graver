#pragma once

#include "hal/board_hal.hpp"
#include "shared/machine_config.hpp"

#include "esp_err.h"

#include <cstdint>

namespace laser {

class LaserService {
 public:
  LaserService(const shared::MachineConfig &config, hal::BoardHal &boardHal);

  esp_err_t start();
  esp_err_t arm();
  esp_err_t disarm();
  esp_err_t setPower(uint8_t power);
  esp_err_t forceOff();
  esp_err_t testPulse(uint8_t power, uint32_t durationMs);

  bool isArmed() const;
  uint8_t currentPower() const;

 private:
  esp_err_t configurePwm();
  esp_err_t applyDuty(uint8_t power);

  shared::MachineConfig config_;
  hal::BoardHal &boardHal_;
  bool armed_;
  uint8_t currentPower_;
  bool pwmConfigured_;
};

}  // namespace laser
