#include "control/control_service.hpp"

#include "esp_check.h"
#include "esp_log.h"

#include <cstring>

namespace control {

namespace {

constexpr const char *kTag = "control";
constexpr UBaseType_t kCommandQueueLength = 1;
constexpr uint32_t kWorkerStackSize = 4096;
constexpr UBaseType_t kWorkerPriority = 7;

}  // namespace

ControlService::ControlService(const shared::MachineConfig &config, motion::MotionService &motion,
                               laser::LaserService &laser, jobs::JobManager &jobs)
    : config_(config),
      motion_(motion),
      laser_(laser),
      jobs_(jobs),
      rasterExecutor_(motion, laser, jobs.storage()),
      vectorExecutor_(motion, laser),
      commandQueue_(nullptr),
      stateMutex_(nullptr),
      workerTask_(nullptr),
      state_(shared::MachineState::Boot),
      activeJobId_(),
      message_("booting"),
      started_(false),
      stopRequested_(false),
      pauseRequested_(false),
      abortRequested_(false),
      jobRowsDone_(0),
      jobRowsTotal_(0),
      jobProgressPercent_(0) {}

ControlService::~ControlService() {
  if (workerTask_ != nullptr) {
    vTaskDelete(workerTask_);
    workerTask_ = nullptr;
  }
  if (commandQueue_ != nullptr) {
    vQueueDelete(commandQueue_);
    commandQueue_ = nullptr;
  }
  if (stateMutex_ != nullptr) {
    vSemaphoreDelete(stateMutex_);
    stateMutex_ = nullptr;
  }
}

esp_err_t ControlService::start() {
  if (started_) {
    return ESP_OK;
  }

  stateMutex_ = xSemaphoreCreateMutex();
  if (stateMutex_ == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  commandQueue_ = xQueueCreate(kCommandQueueLength, sizeof(Command));
  if (commandQueue_ == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  const BaseType_t task_created =
      xTaskCreate(&ControlService::WorkerEntry, "control_worker", kWorkerStackSize, this, kWorkerPriority, &workerTask_);
  if (task_created != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  ESP_RETURN_ON_ERROR(laser_.disarm(), kTag, "Failed to disarm laser on startup");
  setState(shared::MachineState::Idle, "ready");
  started_ = true;
  ESP_LOGI(kTag, "Control service started");
  return ESP_OK;
}

esp_err_t ControlService::home() {
  if (!config_.safety.homingEnabled) {
    return setZero();
  }
  return enqueue(makeCommand(CommandType::Home, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::setZero() {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(motion_.setZero(), kTag, "Failed to set work zero");
  setState(shared::MachineState::Idle, "work zero set");
  return ESP_OK;
}

esp_err_t ControlService::goToZero(const float feedMmMin) {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(motion_.goToZero(feedMmMin), kTag, "Failed to go to work zero");
  setState(shared::MachineState::Idle, "go to zero queued");
  return ESP_OK;
}

esp_err_t ControlService::moveTo(const float xMm, const float yMm, const float feedMmMin) {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(motion_.moveTo(xMm, yMm, feedMmMin), kTag, "Failed to queue move-to");
  setState(shared::MachineState::Idle, "move to queued");
  return ESP_OK;
}

esp_err_t ControlService::runJob(const std::string &jobId) {
  if (!started_ || jobId.empty()) {
    return ESP_ERR_INVALID_STATE;
  }

  Command command{};
  command.type = CommandType::RunJob;
  std::strncpy(command.jobId, jobId.c_str(), sizeof(command.jobId) - 1);
  command.jobId[sizeof(command.jobId) - 1] = '\0';
  return enqueue(command);
}

esp_err_t ControlService::pauseJob() {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  lockState();
  const bool can_pause = state_ == shared::MachineState::Running;
  if (can_pause) {
    state_ = shared::MachineState::Paused;
    message_ = "job paused";
    pauseRequested_.store(true);
  }
  unlockState();

  return can_pause ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t ControlService::resumeJob() {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  lockState();
  const bool can_resume = state_ == shared::MachineState::Paused;
  if (can_resume) {
    state_ = shared::MachineState::Running;
    message_ = "job resumed";
    pauseRequested_.store(false);
  }
  unlockState();

  return can_resume ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t ControlService::abortJob() {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  lockState();
  const bool can_abort = state_ == shared::MachineState::Running || state_ == shared::MachineState::Paused;
  if (can_abort) {
    state_ = shared::MachineState::Idle;
    message_ = "job abort requested";
  }
  unlockState();
  if (!can_abort) {
    return ESP_ERR_INVALID_STATE;
  }

  abortRequested_.store(true);
  stopRequested_.store(true);
  pauseRequested_.store(false);
  ESP_RETURN_ON_ERROR(motion_.stop(), kTag, "Failed to stop motion for abort");
  ESP_RETURN_ON_ERROR(laser_.disarm(), kTag, "Failed to disarm laser for abort");
  return ESP_OK;
}

esp_err_t ControlService::frame() {
  return enqueue(makeCommand(CommandType::Frame, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::holdMotors() {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(motion_.holdMotors(), kTag, "Failed to hold motors");
  setState(shared::MachineState::Idle, "motors held");
  return ESP_OK;
}

esp_err_t ControlService::releaseMotors() {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(motion_.releaseMotors(), kTag, "Failed to release motors");
  setState(shared::MachineState::Idle, "motors released");
  return ESP_OK;
}

esp_err_t ControlService::stop() {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  stopRequested_.store(true);
  pauseRequested_.store(false);
  abortRequested_.store(false);
  ESP_RETURN_ON_ERROR(motion_.stop(), kTag, "Failed to stop motion");
  ESP_RETURN_ON_ERROR(laser_.disarm(), kTag, "Failed to disarm laser");
  setState(shared::MachineState::Idle, "stopped");
  return ESP_OK;
}

esp_err_t ControlService::jog(const motion::AxisId axis, const float distanceMm, const float feedMmMin) {
  if (feedMmMin <= 0.0f || distanceMm == 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  return enqueue(makeCommand(CommandType::Jog, axis, distanceMm, feedMmMin, 0, 0));
}

esp_err_t ControlService::laserPulse(const uint8_t power, const uint32_t durationMs) {
  if (power == 0 || durationMs == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  return enqueue(makeCommand(CommandType::LaserPulse, motion::AxisId::X, 0.0f, 0.0f, power, durationMs));
}

shared::MachineStatus ControlService::status() {
  shared::MachineStatus current{};

  lockState();
  refreshMotionStateLocked();
  current.state = state_;
  current.activeJobId = activeJobId_;
  current.message = message_;
  unlockState();

  current.homed = motion_.isHomed();
  current.laserArmed = laser_.isArmed();
  current.motorsHeld = motion_.motorsHeld();
  current.motionBusy = motion_.isBusy();
  current.motionOperation = motion_.activeOperation();
  current.position = motion_.position();
  current.jobRowsDone = jobRowsDone_.load();
  current.jobRowsTotal = jobRowsTotal_.load();
  current.jobProgressPercent = jobProgressPercent_.load();
  return current;
}

void ControlService::WorkerEntry(void *context) {
  static_cast<ControlService *>(context)->workerLoop();
}

ControlService::Command ControlService::makeCommand(const CommandType type, const motion::AxisId axis,
                                                    const float distanceMm, const float feedMmMin,
                                                    const uint8_t power, const uint32_t durationMs) const {
  Command command{};
  command.type = type;
  command.axis = axis;
  command.distanceMm = distanceMm;
  command.feedMmMin = feedMmMin;
  command.power = power;
  command.durationMs = durationMs;
  command.jobId[0] = '\0';
  return command;
}

void ControlService::workerLoop() {
  Command command{};
  for (;;) {
    if (xQueueReceive(commandQueue_, &command, portMAX_DELAY) != pdPASS) {
      continue;
    }

    const esp_err_t err = execute(command);
    if (err != ESP_OK) {
      if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT) {
        if (command.type == CommandType::RunJob && stopRequested_.load()) {
          stopRequested_.store(false);
          pauseRequested_.store(false);
          const bool aborted = abortRequested_.exchange(false);
          if (aborted && motion_.isHomed()) {
            const esp_err_t zero_err = motion_.goToZero(1200.0f);
            if (zero_err == ESP_OK) {
              while (motion_.isBusy()) {
                vTaskDelay(pdMS_TO_TICKS(5));
              }
            }
          }
          lockState();
          activeJobId_.clear();
          state_ = shared::MachineState::Idle;
          message_ = aborted ? "job aborted" : "job stopped";
          unlockState();
          jobRowsDone_.store(0);
          jobRowsTotal_.store(0);
          jobProgressPercent_.store(0);
          ESP_LOGW(kTag, "%s", aborted ? "Job aborted" : "Job stopped");
        } else if (command.type == CommandType::RunJob && !motion_.isHomed()) {
          setState(shared::MachineState::Idle, "set zero first");
          jobRowsDone_.store(0);
          jobRowsTotal_.store(0);
          jobProgressPercent_.store(0);
          ESP_LOGW(kTag, "Run job rejected: work zero is not set");
        } else {
          setState(shared::MachineState::Idle, "busy");
          ESP_LOGW(kTag, "Command rejected while busy: %s", esp_err_to_name(err));
        }
      } else {
        setAlarm("command failed");
        if (command.type == CommandType::RunJob) {
          jobRowsDone_.store(0);
          jobRowsTotal_.store(0);
          jobProgressPercent_.store(0);
        }
        ESP_LOGE(kTag, "Command failed: %s", esp_err_to_name(err));
      }
    }
  }
}

esp_err_t ControlService::enqueue(const Command &command) {
  if (!started_ || commandQueue_ == nullptr) {
    ESP_LOGW(kTag, "Rejecting command: control service not started");
    return ESP_ERR_INVALID_STATE;
  }

  lockState();
  const bool control_busy = state_ == shared::MachineState::Running || state_ == shared::MachineState::Paused;
  unlockState();
  if (control_busy) {
    ESP_LOGW(kTag, "Rejecting command: job in progress");
    return ESP_ERR_INVALID_STATE;
  }

  const bool is_exclusive_command = command.type == CommandType::Home || command.type == CommandType::Frame ||
                                    command.type == CommandType::Jog || command.type == CommandType::LaserPulse ||
                                    command.type == CommandType::RunJob;
  if (is_exclusive_command) {
    if (motion_.isBusy() || uxQueueMessagesWaiting(commandQueue_) > 0) {
      ESP_LOGW(kTag, "Rejecting command: motion/control busy");
      return ESP_ERR_INVALID_STATE;
    }
  }

  if (xQueueSend(commandQueue_, &command, 0) != pdPASS) {
    ESP_LOGW(kTag, "Rejecting command: control queue full");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(kTag, "Queued control command");
  return ESP_OK;
}

esp_err_t ControlService::execute(const Command &command) {
  switch (command.type) {
    case CommandType::Home:
      setState(shared::MachineState::Homing, "home queued");
      return motion_.home();
    case CommandType::Frame:
      if (config_.safety.homingEnabled && config_.safety.requireHomingBeforeRun && !motion_.isHomed()) {
        setAlarm("frame rejected: machine not homed");
        return ESP_ERR_INVALID_STATE;
      }
      setState(shared::MachineState::Framing, "frame queued");
      return motion_.frame();
    case CommandType::Jog: {
      setState(shared::MachineState::Idle, "jog queued");
      return motion_.jog(command.axis, command.distanceMm, command.feedMmMin);
    }
    case CommandType::LaserPulse:
      setState(shared::MachineState::Idle, "laser pulse");
      return laser_.testPulse(command.power, command.durationMs);
    case CommandType::RunJob: {
      if (!motion_.isHomed()) {
        ESP_LOGW(kTag, "Run job rejected: work zero is not set");
        return ESP_ERR_INVALID_STATE;
      }
      jobs::LoadedJob job{};
      ESP_RETURN_ON_ERROR(jobs_.loadJob(command.jobId, job), kTag, "Failed to load job");
      stopRequested_.store(false);
      pauseRequested_.store(false);
      abortRequested_.store(false);
      jobRowsDone_.store(0);
      jobRowsTotal_.store(0);
      jobProgressPercent_.store(0);
      lockState();
      state_ = shared::MachineState::Running;
      activeJobId_ = command.jobId;
      message_ = "job running";
      unlockState();

      esp_err_t run_err = ESP_ERR_NOT_SUPPORTED;
      uint32_t completed_units = 0;
      if (job.type == jobs::JobType::Raster) {
        run_err = rasterExecutor_.execute(job.rasterJob, stopRequested_, pauseRequested_, jobRowsDone_, jobRowsTotal_,
                                          jobProgressPercent_);
        completed_units = job.rasterJob.manifest.heightPx;
      } else if (job.type == jobs::JobType::VectorPrimitive) {
        run_err = vectorExecutor_.execute(job.vectorPrimitiveJob, stopRequested_, pauseRequested_, jobRowsDone_,
                                          jobRowsTotal_, jobProgressPercent_);
        completed_units = jobRowsTotal_.load();
      } else if (job.type == jobs::JobType::VectorText) {
        run_err = vectorExecutor_.execute(job.vectorTextJob, stopRequested_, pauseRequested_, jobRowsDone_,
                                          jobRowsTotal_, jobProgressPercent_);
        completed_units = jobRowsTotal_.load();
      }
      if (run_err == ESP_OK) {
        lockState();
        activeJobId_.clear();
        state_ = shared::MachineState::Idle;
        message_ = "job complete";
        unlockState();
        jobRowsDone_.store(completed_units);
        jobRowsTotal_.store(completed_units);
        jobProgressPercent_.store(100);
      } else {
        jobRowsDone_.store(0);
        jobRowsTotal_.store(0);
        jobProgressPercent_.store(0);
      }
      return run_err;
    }
    default:
      return ESP_ERR_INVALID_ARG;
  }
}

void ControlService::lockState() {
  if (stateMutex_ != nullptr) {
    xSemaphoreTake(stateMutex_, portMAX_DELAY);
  }
}

void ControlService::unlockState() {
  if (stateMutex_ != nullptr) {
    xSemaphoreGive(stateMutex_);
  }
}

void ControlService::setState(const shared::MachineState state, const std::string &message) {
  lockState();
  state_ = state;
  message_ = message;
  unlockState();
}

void ControlService::setAlarm(const std::string &message) {
  lockState();
  activeJobId_.clear();
  state_ = shared::MachineState::Alarm;
  message_ = message;
  unlockState();
}

void ControlService::refreshMotionStateLocked() {
  if (state_ != shared::MachineState::Homing && state_ != shared::MachineState::Framing) {
    return;
  }

  if (motion_.isBusy()) {
    return;
  }

  if (motion_.lastError() != ESP_OK) {
    state_ = shared::MachineState::Alarm;
    message_ = "motion command failed";
    return;
  }

  state_ = shared::MachineState::Idle;
  message_ = motion_.isHomed() ? "homed" : "ready";
}

}  // namespace control
