#include "machine/machine_controller.hpp"

#include "esp_check.h"
#include "esp_log.h"

namespace machine {

namespace {

constexpr const char *kTag = "machine";

}  // namespace

MachineController::MachineController(const shared::MachineConfig &config, motion::MotionService &motion,
                                     laser::LaserService &laser, jobs::JobManager &jobs)
    : config_(config),
      motion_(motion),
      laser_(laser),
      jobs_(jobs),
      state_(shared::MachineState::Boot),
      activeJobId_(),
      message_("booting") {}

esp_err_t MachineController::start() {
  ESP_RETURN_ON_ERROR(laser_.disarm(), kTag, "Failed to disarm laser");
  setState(shared::MachineState::Idle, "ready");
  ESP_LOGI(kTag, "Machine controller started");
  return ESP_OK;
}

esp_err_t MachineController::home() {
  if (state_ != shared::MachineState::Idle && state_ != shared::MachineState::Alarm) {
    return ESP_ERR_INVALID_STATE;
  }

  setState(shared::MachineState::Homing, "homing");
  return motion_.home();
}

esp_err_t MachineController::frame() {
  if (state_ != shared::MachineState::Idle) {
    return ESP_ERR_INVALID_STATE;
  }

  if (config_.safety.requireHomingBeforeRun && !motion_.isHomed()) {
    return alarm("frame rejected: machine not homed");
  }

  setState(shared::MachineState::Framing, "framing");
  return motion_.frame();
}

esp_err_t MachineController::startJob(const std::string &jobId) {
  if (state_ != shared::MachineState::Idle) {
    return ESP_ERR_INVALID_STATE;
  }

  if (config_.safety.requireHomingBeforeRun && !motion_.isHomed()) {
    return alarm("run rejected: machine not homed");
  }

  if (!jobs_.hasJob(jobId)) {
    return alarm("run rejected: job not found");
  }

  activeJobId_ = jobId;
  ESP_RETURN_ON_ERROR(laser_.arm(), kTag, "Failed to arm laser");
  setState(shared::MachineState::Running, "job started");
  return ESP_OK;
}

esp_err_t MachineController::pause() {
  if (state_ != shared::MachineState::Running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (config_.safety.stopLaserOnPause) {
    ESP_RETURN_ON_ERROR(laser_.forceOff(), kTag, "Failed to stop laser");
  }

  setState(shared::MachineState::Paused, "paused");
  return ESP_OK;
}

esp_err_t MachineController::resume() {
  if (state_ != shared::MachineState::Paused) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(laser_.arm(), kTag, "Failed to arm laser");
  setState(shared::MachineState::Running, "resumed");
  return ESP_OK;
}

esp_err_t MachineController::stop() {
  ESP_RETURN_ON_ERROR(motion_.stop(), kTag, "Failed to stop motion");
  ESP_RETURN_ON_ERROR(laser_.disarm(), kTag, "Failed to disarm laser");
  activeJobId_.clear();
  setState(shared::MachineState::Idle, "stopped");
  return ESP_OK;
}

esp_err_t MachineController::alarm(const std::string &message) {
  if (config_.safety.stopLaserOnAlarm) {
    ESP_RETURN_ON_ERROR(laser_.disarm(), kTag, "Failed to disarm laser on alarm");
  }

  activeJobId_.clear();
  setState(shared::MachineState::Alarm, message);
  ESP_LOGE(kTag, "Alarm: %s", message.c_str());
  return ESP_ERR_INVALID_STATE;
}

shared::MachineStatus MachineController::status() {
  shared::MachineStatus current_status{};
  current_status.state = state_;
  current_status.homed = motion_.isHomed();
  current_status.laserArmed = laser_.isArmed();
  current_status.motionBusy = motion_.isBusy();
  current_status.motionOperation = motion_.activeOperation();
  const motion::MotionStateSnapshot motion_state = motion_.snapshot();
  current_status.position = motion_state.position;
  current_status.machinePosition = motion_state.machinePosition;
  current_status.workOffset = motion_state.workOffset;
  current_status.activeJobId = activeJobId_;
  current_status.message = message_;
  return current_status;
}

void MachineController::refreshStateFromMotion() {
  if (state_ != shared::MachineState::Homing && state_ != shared::MachineState::Framing) {
    return;
  }

  if (motion_.isBusy()) {
    return;
  }

  if (motion_.lastError() != ESP_OK) {
    (void)alarm("motion command failed");
    return;
  }

  if (state_ == shared::MachineState::Homing) {
    setState(shared::MachineState::Idle, "homed");
  } else if (state_ == shared::MachineState::Framing) {
    setState(shared::MachineState::Idle, "frame complete");
  }
}

void MachineController::setState(const shared::MachineState state, const std::string &message) {
  state_ = state;
  message_ = message;
}

}  // namespace machine
