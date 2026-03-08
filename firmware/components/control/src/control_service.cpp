#include "control/control_service.hpp"

#include "esp_check.h"
#include "esp_log.h"

#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace control {

namespace {

constexpr const char *kTag = "control";
constexpr UBaseType_t kCommandQueueLength = 8;
constexpr uint32_t kWorkerStackSize = 4096;
constexpr UBaseType_t kWorkerPriority = 7;
constexpr uint32_t kJobStackSize = 8192;
constexpr UBaseType_t kJobPriority = 6;
constexpr TickType_t kSafetyPollInterval = pdMS_TO_TICKS(10);
constexpr const char *kReasonEstop = "e-stop active";
constexpr const char *kReasonLid = "lid interlock open";
constexpr const char *kReasonLimit = "limit switch hit";
constexpr const char *kReasonFaultNotCleared = "fault still active";

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
      jobTask_(nullptr),
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
  if (jobTask_ != nullptr) {
    vTaskDelete(jobTask_);
    jobTask_ = nullptr;
  }
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
  return enqueue(makeCommand(CommandType::SetZero, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::goToZero(const float feedMmMin) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  return enqueue(makeCommand(CommandType::GoToZero, motion::AxisId::X, 0.0f, feedMmMin, 0, 0));
}

esp_err_t ControlService::moveTo(const float xMm, const float yMm, const float feedMmMin) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  Command command = makeCommand(CommandType::MoveTo, motion::AxisId::X, 0.0f, feedMmMin, 0, 0);
  command.targetXmm = xMm;
  command.targetYmm = yMm;
  return enqueue(command);
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
  return enqueue(makeCommand(CommandType::Pause, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::resumeJob() {
  return enqueue(makeCommand(CommandType::Resume, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::abortJob() {
  return enqueue(makeCommand(CommandType::Abort, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::clearAlarm() {
  return enqueue(makeCommand(CommandType::ClearAlarm, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::frame() {
  return enqueue(makeCommand(CommandType::Frame, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::holdMotors() {
  return enqueue(makeCommand(CommandType::HoldMotors, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
}

esp_err_t ControlService::releaseMotors() {
  return enqueue(makeCommand(CommandType::ReleaseMotors, motion::AxisId::X, 0.0f, 0.0f, 0, 0));
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
  lockState();
  if (!isAlarmLikeStateLocked()) {
    state_ = shared::MachineState::Idle;
    message_ = "stopped";
  }
  unlockState();
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

  const motion::MotionStateSnapshot motion_state = motion_.snapshot();
  current.estopActive = motion_.isEstopActive();
  current.lidOpen = motion_.isLidOpen();
  current.limitActive = motion_.isAnyLimitActive();
  current.homed = motion_state.homed;
  current.laserArmed = laser_.isArmed();
  current.motorsHeld = motion_state.motorsHeld;
  current.motionBusy = motion_state.pending || motion_state.activeOperation != shared::MotionOperation::None;
  current.motionOperation = motion_state.activeOperation;
  current.position = motion_state.position;
  current.jobRowsDone = jobRowsDone_.load();
  current.jobRowsTotal = jobRowsTotal_.load();
  current.jobProgressPercent = jobProgressPercent_.load();
  return current;
}

void ControlService::WorkerEntry(void *context) {
  static_cast<ControlService *>(context)->workerLoop();
}

void ControlService::JobEntry(void *context) {
  using JobContext = std::pair<ControlService *, jobs::LoadedJob>;
  std::unique_ptr<JobContext> job_context(static_cast<JobContext *>(context));
  if (job_context == nullptr || job_context->first == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  job_context->first->jobLoop(std::move(job_context->second));
  vTaskDelete(nullptr);
}

ControlService::Command ControlService::makeCommand(const CommandType type, const motion::AxisId axis,
                                                    const float distanceMm, const float feedMmMin,
                                                    const uint8_t power, const uint32_t durationMs) const {
  Command command{};
  command.type = type;
  command.axis = axis;
  command.distanceMm = distanceMm;
  command.feedMmMin = feedMmMin;
  command.targetXmm = 0.0f;
  command.targetYmm = 0.0f;
  command.power = power;
  command.durationMs = durationMs;
  command.result = ESP_OK;
  command.completedUnits = 0U;
  command.jobId[0] = '\0';
  return command;
}

void ControlService::workerLoop() {
  Command command{};
  for (;;) {
    if (checkSafetyAndTrip()) {
      vTaskDelay(kSafetyPollInterval);
      continue;
    }

    if (xQueueReceive(commandQueue_, &command, kSafetyPollInterval) != pdPASS) {
      continue;
    }

    if (checkSafetyAndTrip()) {
      vTaskDelay(kSafetyPollInterval);
      continue;
    }

    const esp_err_t err = execute(command);
    if (err != ESP_OK && checkSafetyAndTrip()) {
      continue;
    }

    if (err != ESP_OK) {
      if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT) {
        if (command.type == CommandType::RunJob && stopRequested_.load()) {
          stopRequested_.store(false);
          pauseRequested_.store(false);
          const bool aborted = abortRequested_.exchange(false);
          const motion::MotionStateSnapshot motion_state = motion_.snapshot();
          if (aborted && motion_state.homed) {
            const esp_err_t zero_err = motion_.goToZero(1200.0f);
            if (zero_err == ESP_OK) {
              while (true) {
                const motion::MotionStateSnapshot loop_state = motion_.snapshot();
                if (!loop_state.pending && loop_state.activeOperation == shared::MotionOperation::None) {
                  break;
                }
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
        } else if (command.type == CommandType::RunJob && !motion_.snapshot().homed) {
          setState(shared::MachineState::Idle, "set zero first");
          jobRowsDone_.store(0);
          jobRowsTotal_.store(0);
          jobProgressPercent_.store(0);
          ESP_LOGW(kTag, "Run job rejected: work zero is not set");
        } else if (command.type == CommandType::Pause || command.type == CommandType::Resume ||
                   command.type == CommandType::Abort || command.type == CommandType::ClearAlarm) {
          ESP_LOGW(kTag, "Runtime command rejected: %s", esp_err_to_name(err));
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

  if (checkSafetyAndTrip()) {
    ESP_LOGW(kTag, "Rejecting command: safety fault is active");
    return ESP_ERR_INVALID_STATE;
  }

  const bool runtime_command = isRuntimeCommand(command);
  lockState();
  const bool alarm_like = isAlarmLikeStateLocked();
  const bool running_or_paused = state_ == shared::MachineState::Running || state_ == shared::MachineState::Paused;
  unlockState();

  if (alarm_like && command.type != CommandType::ClearAlarm) {
    ESP_LOGW(kTag, "Rejecting command: machine in alarm state");
    return ESP_ERR_INVALID_STATE;
  }
  if (!alarm_like && command.type == CommandType::ClearAlarm) {
    ESP_LOGW(kTag, "Rejecting clear alarm: machine is not in alarm state");
    return ESP_ERR_INVALID_STATE;
  }
  if (running_or_paused && !runtime_command) {
    ESP_LOGW(kTag, "Rejecting command: job in progress");
    return ESP_ERR_INVALID_STATE;
  }
  if (runtime_command && !running_or_paused) {
    ESP_LOGW(kTag, "Rejecting runtime command: no active job");
    return ESP_ERR_INVALID_STATE;
  }

  if (isExclusiveCommand(command)) {
    const motion::MotionStateSnapshot motion_state = motion_.snapshot();
    const bool motion_busy = motion_state.pending || motion_state.activeOperation != shared::MotionOperation::None;
    if (motion_busy || uxQueueMessagesWaiting(commandQueue_) > 0) {
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
    case CommandType::SetZero:
      setState(shared::MachineState::Idle, "set zero queued");
      return motion_.setZero();
    case CommandType::GoToZero:
      setState(shared::MachineState::Idle, "go to zero queued");
      return motion_.goToZero(command.feedMmMin);
    case CommandType::MoveTo:
      setState(shared::MachineState::Idle, "move to queued");
      return motion_.moveTo(command.targetXmm, command.targetYmm, command.feedMmMin);
    case CommandType::HoldMotors:
      setState(shared::MachineState::Idle, "hold motors queued");
      return motion_.holdMotors();
    case CommandType::ReleaseMotors:
      setState(shared::MachineState::Idle, "release motors queued");
      return motion_.releaseMotors();
    case CommandType::Frame:
      if (config_.safety.homingEnabled && config_.safety.requireHomingBeforeRun && !motion_.snapshot().homed) {
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
      if (!motion_.snapshot().homed) {
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
      if (jobTask_ != nullptr || state_ == shared::MachineState::Running || state_ == shared::MachineState::Paused) {
        unlockState();
        return ESP_ERR_INVALID_STATE;
      }
      state_ = shared::MachineState::Running;
      activeJobId_ = command.jobId;
      message_ = "job running";
      unlockState();

      using JobContext = std::pair<ControlService *, jobs::LoadedJob>;
      std::unique_ptr<JobContext> context(new (std::nothrow) JobContext{this, std::move(job)});
      if (!context) {
        lockState();
        activeJobId_.clear();
        state_ = shared::MachineState::Idle;
        message_ = "job start failed";
        unlockState();
        return ESP_ERR_NO_MEM;
      }

      TaskHandle_t task = nullptr;
      const BaseType_t created =
          xTaskCreate(&ControlService::JobEntry, "job_worker", kJobStackSize, context.get(), kJobPriority, &task);
      if (created != pdPASS) {
        lockState();
        activeJobId_.clear();
        state_ = shared::MachineState::Idle;
        message_ = "job start failed";
        unlockState();
        return ESP_ERR_NO_MEM;
      }

      lockState();
      jobTask_ = task;
      unlockState();
      context.release();
      return ESP_OK;
    }
    case CommandType::Pause: {
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
    case CommandType::Resume: {
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
    case CommandType::Abort: {
      lockState();
      const bool can_abort = state_ == shared::MachineState::Running || state_ == shared::MachineState::Paused;
      if (can_abort) {
        message_ = "job abort requested";
      }
      unlockState();
      if (!can_abort) {
        return ESP_ERR_INVALID_STATE;
      }

      abortRequested_.store(true);
      stopRequested_.store(true);
      pauseRequested_.store(false);
      (void)motion_.stop();
      (void)laser_.forceOff();
      (void)laser_.disarm();
      return ESP_OK;
    }
    case CommandType::ClearAlarm: {
      lockState();
      const bool alarm_state = state_ == shared::MachineState::Alarm || state_ == shared::MachineState::EstopLatched;
      unlockState();
      if (!alarm_state) {
        return ESP_ERR_INVALID_STATE;
      }

      if (motion_.isEstopActive() || motion_.isLidOpen() || motion_.isAnyLimitActive()) {
        lockState();
        message_ = kReasonFaultNotCleared;
        unlockState();
        return ESP_ERR_INVALID_STATE;
      }

      stopRequested_.store(false);
      pauseRequested_.store(false);
      abortRequested_.store(false);
      (void)motion_.stop();
      ESP_RETURN_ON_ERROR(laser_.disarm(), kTag, "Failed to disarm laser while clearing alarm");

      lockState();
      state_ = shared::MachineState::Idle;
      message_ = "alarm cleared";
      activeJobId_.clear();
      unlockState();
      ESP_LOGI(kTag, "Alarm cleared");
      return ESP_OK;
    }
    case CommandType::JobFinished: {
      lockState();
      jobTask_ = nullptr;
      unlockState();

      if (command.result == ESP_OK) {
        lockState();
        activeJobId_.clear();
        state_ = shared::MachineState::Idle;
        message_ = "job complete";
        unlockState();
        jobRowsDone_.store(command.completedUnits);
        jobRowsTotal_.store(command.completedUnits);
        jobProgressPercent_.store(100);
        stopRequested_.store(false);
        pauseRequested_.store(false);
        abortRequested_.store(false);
        return ESP_OK;
      }

      if (stopRequested_.load()) {
        stopRequested_.store(false);
        pauseRequested_.store(false);
        const bool aborted = abortRequested_.exchange(false);
        const motion::MotionStateSnapshot motion_state = motion_.snapshot();
        if (aborted && motion_state.homed) {
          const esp_err_t zero_err = motion_.goToZero(1200.0f);
          if (zero_err == ESP_OK) {
            while (true) {
              const motion::MotionStateSnapshot loop_state = motion_.snapshot();
              if (!loop_state.pending && loop_state.activeOperation == shared::MotionOperation::None) {
                break;
              }
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
        return ESP_OK;
      }

      setAlarm("command failed");
      jobRowsDone_.store(0);
      jobRowsTotal_.store(0);
      jobProgressPercent_.store(0);
      ESP_LOGE(kTag, "Job failed: %s", esp_err_to_name(command.result));
      return command.result;
    }
    default:
      return ESP_ERR_INVALID_ARG;
  }
}

void ControlService::jobLoop(jobs::LoadedJob job) {
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
    run_err = vectorExecutor_.execute(job.vectorTextJob, stopRequested_, pauseRequested_, jobRowsDone_, jobRowsTotal_,
                                      jobProgressPercent_);
    completed_units = jobRowsTotal_.load();
  }

  Command finished{};
  finished.type = CommandType::JobFinished;
  finished.axis = motion::AxisId::X;
  finished.distanceMm = 0.0f;
  finished.feedMmMin = 0.0f;
  finished.targetXmm = 0.0f;
  finished.targetYmm = 0.0f;
  finished.power = 0U;
  finished.durationMs = 0U;
  finished.result = run_err;
  finished.completedUnits = completed_units;
  finished.jobId[0] = '\0';
  if (commandQueue_ != nullptr) {
    while (xQueueSend(commandQueue_, &finished, pdMS_TO_TICKS(100)) != pdPASS) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  } else {
    lockState();
    jobTask_ = nullptr;
    activeJobId_.clear();
    if (run_err == ESP_OK) {
      state_ = shared::MachineState::Idle;
      message_ = "job complete";
      jobRowsDone_.store(completed_units);
      jobRowsTotal_.store(completed_units);
      jobProgressPercent_.store(100);
    } else {
      state_ = shared::MachineState::Alarm;
      message_ = "job task queue unavailable";
      jobRowsDone_.store(0);
      jobRowsTotal_.store(0);
      jobProgressPercent_.store(0);
    }
    unlockState();
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

void ControlService::tripAlarm(const std::string &reason, const bool estopLatched) {
  stopRequested_.store(true);
  pauseRequested_.store(false);
  abortRequested_.store(false);
  if (commandQueue_ != nullptr) {
    xQueueReset(commandQueue_);
  }
  (void)motion_.stop();
  (void)laser_.forceOff();
  (void)laser_.disarm();

  lockState();
  const bool same_state =
      (estopLatched && state_ == shared::MachineState::EstopLatched && message_ == reason) ||
      (!estopLatched && state_ == shared::MachineState::Alarm && message_ == reason);
  state_ = estopLatched ? shared::MachineState::EstopLatched : shared::MachineState::Alarm;
  message_ = reason;
  activeJobId_.clear();
  unlockState();

  jobRowsDone_.store(0);
  jobRowsTotal_.store(0);
  jobProgressPercent_.store(0);

  if (!same_state) {
    ESP_LOGE(kTag, "Safety trip: %s", reason.c_str());
  }
}

bool ControlService::checkSafetyAndTrip() {
  if (motion_.isEstopActive()) {
    tripAlarm(kReasonEstop, true);
    return true;
  }

  if (motion_.isLidOpen()) {
    tripAlarm(kReasonLid, false);
    return true;
  }

  const motion::MotionStateSnapshot motion_state = motion_.snapshot();
  const bool homing_in_progress = motion_state.activeOperation == shared::MotionOperation::Home;
  if (!homing_in_progress && motion_.isAnyLimitActive()) {
    tripAlarm(kReasonLimit, false);
    return true;
  }

  return false;
}

bool ControlService::isAlarmLikeStateLocked() const {
  return state_ == shared::MachineState::Alarm || state_ == shared::MachineState::EstopLatched;
}

bool ControlService::isRuntimeCommand(const Command &command) const {
  return command.type == CommandType::Pause || command.type == CommandType::Resume || command.type == CommandType::Abort;
}

bool ControlService::isExclusiveCommand(const Command &command) const {
  switch (command.type) {
    case CommandType::Home:
    case CommandType::SetZero:
    case CommandType::GoToZero:
    case CommandType::MoveTo:
    case CommandType::HoldMotors:
    case CommandType::ReleaseMotors:
    case CommandType::Frame:
    case CommandType::Jog:
    case CommandType::LaserPulse:
    case CommandType::RunJob:
      return true;
    case CommandType::Pause:
    case CommandType::Resume:
    case CommandType::Abort:
    case CommandType::ClearAlarm:
    case CommandType::JobFinished:
      return false;
    default:
      return false;
  }
}

void ControlService::refreshMotionStateLocked() {
  if (state_ != shared::MachineState::Homing && state_ != shared::MachineState::Framing) {
    return;
  }

  const motion::MotionStateSnapshot motion_state = motion_.snapshot();
  const bool motion_busy = motion_state.pending || motion_state.activeOperation != shared::MotionOperation::None;
  if (motion_busy) {
    return;
  }

  if (motion_state.lastError != ESP_OK) {
    state_ = shared::MachineState::Alarm;
    message_ = "motion command failed";
    return;
  }

  state_ = shared::MachineState::Idle;
  message_ = motion_state.homed ? "homed" : "ready";
}

}  // namespace control
