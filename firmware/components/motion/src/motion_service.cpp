#include "motion/motion_service.hpp"

#include "laser/laser_service.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

#include <algorithm>
#include <cmath>

namespace motion {

namespace {

constexpr const char *kTag = "motion";
constexpr UBaseType_t kCommandQueueLength = 1;
constexpr uint32_t kWorkerStackSize = 4096;
constexpr UBaseType_t kWorkerPriority = 8;
constexpr float kSoftLimitEpsilonMm = 0.001f;
constexpr float kDefaultFrameFeedMmMin = 1200.0f;
constexpr uint32_t kHomingChunkSteps = 16;
constexpr float kMinHomingFeedMmMin = 1.0f;
constexpr float kMinHomingPullOffMm = 0.1f;

struct LinearMovePlan {
  uint32_t xSteps;
  uint32_t ySteps;
  uint32_t stepDelayUs;
};

esp_err_t BuildLinearMovePlan(const shared::MachineConfig &config, const float deltaXmm, const float deltaYmm,
                              const float feedMmMin, LinearMovePlan &plan) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  const uint32_t x_steps = static_cast<uint32_t>(std::lround(std::fabs(deltaXmm) * config.xAxis.stepsPerMm));
  const uint32_t y_steps = static_cast<uint32_t>(std::lround(std::fabs(deltaYmm) * config.yAxis.stepsPerMm));
  if (x_steps == 0U && y_steps == 0U) {
    plan.xSteps = 0U;
    plan.ySteps = 0U;
    plan.stepDelayUs = 0U;
    return ESP_OK;
  }

  const float distance_mm = std::sqrt((deltaXmm * deltaXmm) + (deltaYmm * deltaYmm));
  const uint32_t major_steps = x_steps > y_steps ? x_steps : y_steps;
  if (distance_mm <= 0.0f || major_steps == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  const float seconds = (distance_mm * 60.0f) / feedMmMin;
  const float step_delay = (seconds * 1000000.0f) / static_cast<float>(major_steps);
  plan.xSteps = x_steps;
  plan.ySteps = y_steps;
  plan.stepDelayUs = static_cast<uint32_t>(std::max(1.0f, step_delay));
  return ESP_OK;
}

}  // namespace

MotionService::MotionService(const shared::MachineConfig &config, hal::BoardHal &boardHal)
    : config_(config),
      boardHal_(boardHal),
      position_{0.0f, 0.0f},
      homed_(false),
      motorsHeld_(false),
      pending_(false),
      activeOperation_(shared::MotionOperation::None),
      lastError_(ESP_OK),
      commandQueue_(nullptr),
      completionSemaphore_(nullptr),
      stateMutex_(nullptr),
      workerTask_(nullptr),
      started_(false) {}

MotionService::~MotionService() {
  if (workerTask_ != nullptr) {
    vTaskDelete(workerTask_);
    workerTask_ = nullptr;
  }
  if (commandQueue_ != nullptr) {
    vQueueDelete(commandQueue_);
    commandQueue_ = nullptr;
  }
  if (completionSemaphore_ != nullptr) {
    vSemaphoreDelete(completionSemaphore_);
    completionSemaphore_ = nullptr;
  }
  if (stateMutex_ != nullptr) {
    vSemaphoreDelete(stateMutex_);
    stateMutex_ = nullptr;
  }
}

esp_err_t MotionService::start() {
  if (stateMutex_ == nullptr) {
    stateMutex_ = xSemaphoreCreateRecursiveMutex();
    if (stateMutex_ == nullptr) {
      return ESP_ERR_NO_MEM;
    }
  }

  const MotionStateSnapshot current = snapshot();
  if (current.started) {
    return ESP_OK;
  }

  commandQueue_ = xQueueCreate(kCommandQueueLength, sizeof(MotionCommand));
  if (commandQueue_ == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  completionSemaphore_ = xSemaphoreCreateBinary();
  if (completionSemaphore_ == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  BaseType_t task_created =
      xTaskCreate(&MotionService::WorkerEntry, "motion_worker", kWorkerStackSize, this, kWorkerPriority, &workerTask_);
  if (task_created != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  ESP_RETURN_ON_ERROR(boardHal_.setMotorsEnabled(true), kTag, "Failed to enable motors");
  lockState();
  motorsHeld_ = true;
  started_ = true;
  unlockState();
  ESP_LOGI(kTag, "Motion service started");
  return ESP_OK;
}

esp_err_t MotionService::home() {
  return enqueueCommand({shared::MotionOperation::Home, AxisId::X, 0.0f, 0.0f, 0.0f, 0.0f});
}

esp_err_t MotionService::frame() {
  const MotionStateSnapshot state = snapshot();
  if (!state.started || state.pending || state.activeOperation != shared::MotionOperation::None || !state.homed) {
    return ESP_ERR_INVALID_STATE;
  }

  return enqueueCommand({shared::MotionOperation::Frame, AxisId::X, 0.0f, 0.0f, 0.0f, 0.0f});
}

esp_err_t MotionService::jog(const AxisId axis, const float distanceMm, const float feedMmMin) {
  if (feedMmMin <= 0.0f || distanceMm == 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  const MotionStateSnapshot state = snapshot();
  const float target_x_mm = axis == AxisId::X ? state.position.x + distanceMm : state.position.x;
  const float target_y_mm = axis == AxisId::Y ? state.position.y + distanceMm : state.position.y;
  ESP_RETURN_ON_ERROR(validateTargetPosition(target_x_mm, target_y_mm, shared::MotionOperation::Jog), kTag,
                      "Jog target is outside work area");

  return enqueueCommand({shared::MotionOperation::Jog, axis, distanceMm, feedMmMin, 0.0f, 0.0f});
}

esp_err_t MotionService::setZero() {
  lockState();
  if (!started_ || pending_ || activeOperation_ != shared::MotionOperation::None) {
    unlockState();
    return ESP_ERR_INVALID_STATE;
  }
  position_ = {0.0f, 0.0f};
  homed_ = true;
  lastError_ = ESP_OK;
  unlockState();
  ESP_LOGI(kTag, "Work zero set at current position");
  return ESP_OK;
}

esp_err_t MotionService::goToZero(const float feedMmMin) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  const MotionStateSnapshot state = snapshot();
  if (!state.started || state.pending || state.activeOperation != shared::MotionOperation::None || !state.homed) {
    return ESP_ERR_INVALID_STATE;
  }

  return enqueueCommand({shared::MotionOperation::GoToZero, AxisId::X, 0.0f, feedMmMin, 0.0f, 0.0f});
}

esp_err_t MotionService::moveTo(const float xMm, const float yMm, const float feedMmMin) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  const MotionStateSnapshot state = snapshot();
  if (!state.started || state.pending || state.activeOperation != shared::MotionOperation::None || !state.homed) {
    return ESP_ERR_INVALID_STATE;
  }
  ESP_RETURN_ON_ERROR(validateTargetPosition(xMm, yMm, shared::MotionOperation::MoveTo), kTag,
                      "Move-to target is outside work area");

  return enqueueCommand({shared::MotionOperation::MoveTo, AxisId::X, 0.0f, feedMmMin, xMm, yMm});
}

esp_err_t MotionService::directMoveTo(const float xMm, const float yMm, const float feedMmMin,
                                      const shared::MotionOperation operation) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  ESP_RETURN_ON_ERROR(validateTargetPosition(xMm, yMm, operation), kTag, "Direct move target is outside work area");

  shared::PositionMm start_position{};
  lockState();
  if (!started_ || pending_ || activeOperation_ != shared::MotionOperation::None || !homed_) {
    unlockState();
    return ESP_ERR_INVALID_STATE;
  }
  activeOperation_ = operation;
  lastError_ = ESP_OK;
  start_position = position_;
  unlockState();

  const esp_err_t err = executeLinearMove(xMm - start_position.x, yMm - start_position.y, feedMmMin, operation);

  lockState();
  lastError_ = err;
  activeOperation_ = shared::MotionOperation::None;
  unlockState();
  return err;
}

esp_err_t MotionService::executeRasterRow(const float rowYmm, const float startXmm, const bool reverse,
                                          const RasterSegment *segments, const size_t segmentCount,
                                          const float printFeedMmMin, const float travelFeedMmMin,
                                          laser::LaserService &laser) {
  if (segments == nullptr || segmentCount == 0U || printFeedMmMin <= 0.0f || travelFeedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(validateTargetPosition(startXmm, rowYmm, shared::MotionOperation::MoveTo), kTag,
                      "Raster row start is outside work area");

  shared::PositionMm start_position{};
  bool motors_held = false;
  lockState();
  if (!started_ || pending_ || activeOperation_ != shared::MotionOperation::None || !homed_) {
    unlockState();
    return ESP_ERR_INVALID_STATE;
  }
  activeOperation_ = shared::MotionOperation::MoveTo;
  lastError_ = ESP_OK;
  start_position = position_;
  motors_held = motorsHeld_;
  unlockState();
  boardHal_.setIgnoreLimitInputs(false);

  esp_err_t err = executeLinearMove(startXmm - start_position.x, rowYmm - start_position.y, travelFeedMmMin,
                                    shared::MotionOperation::MoveTo);
  if (err != ESP_OK) {
    (void)laser.forceOff();
    lockState();
    lastError_ = err;
    activeOperation_ = shared::MotionOperation::None;
    unlockState();
    return err;
  }

  if (!motors_held) {
    err = boardHal_.setMotorsEnabled(true);
    if (err != ESP_OK) {
      lockState();
      lastError_ = err;
      activeOperation_ = shared::MotionOperation::None;
      unlockState();
      ESP_LOGE(kTag, "Failed to enable motors before raster row: %s", esp_err_to_name(err));
      return err;
    }
    lockState();
    motorsHeld_ = true;
    unlockState();
  }

  err = boardHal_.prepareXAxisMove(!reverse);
  if (err != ESP_OK) {
    lockState();
    lastError_ = err;
    activeOperation_ = shared::MotionOperation::None;
    unlockState();
    ESP_LOGE(kTag, "Failed to prepare X direction for raster row: %s", esp_err_to_name(err));
    return err;
  }

  const float x_steps_per_mm = std::max(1.0f, config_.xAxis.stepsPerMm);
  const uint32_t print_step_delay_us = static_cast<uint32_t>(
      std::max(1.0f, (60.0f * 1000000.0f) / std::max(1.0f, printFeedMmMin * x_steps_per_mm)));
  const uint32_t travel_step_delay_us = static_cast<uint32_t>(
      std::max(1.0f, (60.0f * 1000000.0f) / std::max(1.0f, travelFeedMmMin * x_steps_per_mm)));

  float delta_x_mm = 0.0f;
  for (size_t i = 0; i < segmentCount; ++i) {
    const RasterSegment &segment = segments[i];
    const uint32_t segment_steps =
        static_cast<uint32_t>(std::lround(std::fabs(segment.lengthMm) * x_steps_per_mm));
    if (segment_steps == 0U) {
      continue;
    }

    err = laser.setPower(segment.power);
    if (err != ESP_OK) {
      (void)laser.forceOff();
      lockState();
      lastError_ = err;
      activeOperation_ = shared::MotionOperation::None;
      unlockState();
      return err;
    }

    const uint32_t delay_us = segment.power == 0U ? travel_step_delay_us : print_step_delay_us;
    err = boardHal_.runXAxisSteps(segment_steps, delay_us);
    if (err != ESP_OK) {
      (void)laser.forceOff();
      (void)laser.disarm();
      lockState();
      lastError_ = err;
      activeOperation_ = shared::MotionOperation::None;
      unlockState();
      return err;
    }

    const float segment_mm = static_cast<float>(segment_steps) / x_steps_per_mm;
    delta_x_mm += reverse ? -segment_mm : segment_mm;
  }

  lockState();
  position_.x += delta_x_mm;
  position_.y = rowYmm;
  lastError_ = ESP_OK;
  activeOperation_ = shared::MotionOperation::None;
  unlockState();
  return ESP_OK;
}

esp_err_t MotionService::holdMotors() {
  lockState();
  if (!started_ || pending_ || activeOperation_ != shared::MotionOperation::None) {
    unlockState();
    return ESP_ERR_INVALID_STATE;
  }
  pending_ = true;
  lastError_ = ESP_OK;
  unlockState();

  const esp_err_t err = boardHal_.setMotorsEnabled(true);
  if (err != ESP_OK) {
    lockState();
    pending_ = false;
    lastError_ = err;
    unlockState();
    ESP_LOGE(kTag, "Failed to hold motors: %s", esp_err_to_name(err));
    return err;
  }

  lockState();
  motorsHeld_ = true;
  pending_ = false;
  lastError_ = ESP_OK;
  unlockState();
  ESP_LOGI(kTag, "Motors held");
  return ESP_OK;
}

esp_err_t MotionService::releaseMotors() {
  lockState();
  if (!started_ || pending_ || activeOperation_ != shared::MotionOperation::None) {
    unlockState();
    return ESP_ERR_INVALID_STATE;
  }
  pending_ = true;
  lastError_ = ESP_OK;
  unlockState();

  const esp_err_t err = boardHal_.setMotorsEnabled(false);
  if (err != ESP_OK) {
    lockState();
    pending_ = false;
    lastError_ = err;
    unlockState();
    ESP_LOGE(kTag, "Failed to release motors: %s", esp_err_to_name(err));
    return err;
  }

  lockState();
  motorsHeld_ = false;
  homed_ = false;
  pending_ = false;
  lastError_ = ESP_OK;
  unlockState();
  ESP_LOGI(kTag, "Motors released, work position reference invalidated");
  return ESP_OK;
}

esp_err_t MotionService::stop() {
  const MotionStateSnapshot state = snapshot();
  if (!state.started || commandQueue_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  boardHal_.requestMotionStop();
  xQueueReset(commandQueue_);

  lockState();
  pending_ = false;
  if (activeOperation_ == shared::MotionOperation::None) {
    lastError_ = ESP_OK;
  }
  unlockState();

  ESP_LOGW(kTag, "Motion stop requested");
  return ESP_OK;
}

shared::PositionMm MotionService::position() const {
  return snapshot().position;
}

MotionStateSnapshot MotionService::snapshot() const {
  MotionStateSnapshot state{};
  lockState();
  state.position = position_;
  state.homed = homed_;
  state.motorsHeld = motorsHeld_;
  state.pending = pending_;
  state.activeOperation = activeOperation_;
  state.lastError = lastError_;
  state.started = started_;
  unlockState();
  return state;
}

bool MotionService::isHomed() const {
  return snapshot().homed;
}

bool MotionService::motorsHeld() const {
  return snapshot().motorsHeld;
}

bool MotionService::isEstopActive() const {
  return boardHal_.isEstopActive();
}

bool MotionService::isLidOpen() const {
  return boardHal_.isLidOpen();
}

bool MotionService::isAnyLimitActive() const {
  return boardHal_.isAnyLimitActive();
}

bool MotionService::isBusy() const {
  const MotionStateSnapshot state = snapshot();
  return state.pending || state.activeOperation != shared::MotionOperation::None;
}

shared::MotionOperation MotionService::activeOperation() const {
  return snapshot().activeOperation;
}

esp_err_t MotionService::lastError() const {
  return snapshot().lastError;
}

esp_err_t MotionService::waitForIdle(const TickType_t timeoutTicks) {
  const MotionStateSnapshot state = snapshot();
  if (!state.started || completionSemaphore_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!state.pending && state.activeOperation == shared::MotionOperation::None) {
    return state.lastError;
  }

  if (xSemaphoreTake(completionSemaphore_, timeoutTicks) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  return snapshot().lastError;
}

void MotionService::WorkerEntry(void *context) {
  static_cast<MotionService *>(context)->workerLoop();
}

void MotionService::workerLoop() {
  MotionCommand command{};
  for (;;) {
    if (xQueueReceive(commandQueue_, &command, portMAX_DELAY) != pdPASS) {
      continue;
    }

    const esp_err_t command_err = executeCommand(command);

    lockState();
    pending_ = false;
    activeOperation_ = shared::MotionOperation::None;
    lastError_ = command_err;
    unlockState();

    boardHal_.clearMotionStop();
    if (completionSemaphore_ != nullptr) {
      xSemaphoreGive(completionSemaphore_);
    }

    if (command_err != ESP_OK) {
      ESP_LOGE(kTag, "Motion command failed: op=%s err=%s", shared::ToString(command.operation),
               esp_err_to_name(command_err));
    }
  }
}

esp_err_t MotionService::enqueueCommand(const MotionCommand &command) {
  lockState();
  if (!started_ || commandQueue_ == nullptr) {
    unlockState();
    return ESP_ERR_INVALID_STATE;
  }

  if (pending_ || activeOperation_ != shared::MotionOperation::None) {
    unlockState();
    return ESP_ERR_INVALID_STATE;
  }
  pending_ = true;
  activeOperation_ = command.operation;
  lastError_ = ESP_OK;
  unlockState();
  if (completionSemaphore_ != nullptr) {
    while (xSemaphoreTake(completionSemaphore_, 0) == pdTRUE) {
    }
  }

  if (xQueueSend(commandQueue_, &command, 0) != pdPASS) {
    lockState();
    pending_ = false;
    activeOperation_ = shared::MotionOperation::None;
    lastError_ = ESP_ERR_TIMEOUT;
    unlockState();
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}

esp_err_t MotionService::executeCommand(const MotionCommand &command) {
  switch (command.operation) {
    case shared::MotionOperation::Home:
      return executeHome();
    case shared::MotionOperation::Frame:
      return executeFrame();
    case shared::MotionOperation::Jog:
      return executeJog(command);
    case shared::MotionOperation::GoToZero:
      return executeGoToZero(command);
    case shared::MotionOperation::MoveTo:
      return executeMoveTo(command);
    case shared::MotionOperation::None:
    default:
      return ESP_ERR_INVALID_ARG;
  }
}

esp_err_t MotionService::executeHome() {
  lockState();
  homed_ = false;
  const bool motors_held = motorsHeld_;
  unlockState();

  if (!motors_held) {
    ESP_RETURN_ON_ERROR(boardHal_.setMotorsEnabled(true), kTag, "Failed to enable motors before homing");
    lockState();
    motorsHeld_ = true;
    unlockState();
  }

  if (!config_.safety.homingEnabled) {
    lockState();
    position_ = {0.0f, 0.0f};
    homed_ = true;
    unlockState();
    ESP_LOGI(kTag, "Homing disabled, work zero set at current position");
    return ESP_OK;
  }

  const float seek_feed = std::max(kMinHomingFeedMmMin, config_.safety.homingSeekFeedMmMin);
  const float latch_feed = std::max(kMinHomingFeedMmMin, config_.safety.homingLatchFeedMmMin);
  const float pull_off = std::max(kMinHomingPullOffMm, config_.safety.homingPullOffMm);
  const uint32_t timeout_ms = config_.safety.homingTimeoutMs > 0U ? config_.safety.homingTimeoutMs : 30000U;

  if (config_.safety.homeXEnabled) {
    ESP_RETURN_ON_ERROR(executeAxisHome(AxisId::X, config_.safety.homeXToMin, seek_feed, latch_feed, pull_off, timeout_ms),
                        kTag, "X homing failed");
    lockState();
    position_.x = 0.0f;
    unlockState();
  }

  if (config_.safety.homeYEnabled) {
    ESP_RETURN_ON_ERROR(executeAxisHome(AxisId::Y, config_.safety.homeYToMin, seek_feed, latch_feed, pull_off, timeout_ms),
                        kTag, "Y homing failed");
    lockState();
    position_.y = 0.0f;
    unlockState();
  }

  lockState();
  homed_ = true;
  unlockState();
  ESP_LOGI(kTag, "Homing complete");
  return ESP_OK;
}

esp_err_t MotionService::executeAxisHome(const AxisId axis, const bool towardMin, const float seekFeedMmMin,
                                         const float latchFeedMmMin, const float pullOffMm, const uint32_t timeoutMs) {
  const bool limit_pin_missing =
      (axis == AxisId::X && config_.pins.xLimit < 0) || (axis == AxisId::Y && config_.pins.yLimit < 0);
  if (limit_pin_missing) {
    ESP_LOGE(kTag, "Homing %s failed: limit pin is not configured", axis == AxisId::X ? "X" : "Y");
    return ESP_ERR_INVALID_STATE;
  }

  if (isAxisLimitActive(axis)) {
    ESP_LOGE(kTag, "Homing %s failed: limit switch is active before start", axis == AxisId::X ? "X" : "Y");
    return ESP_ERR_INVALID_STATE;
  }

  const float steps_per_mm = axis == AxisId::X ? config_.xAxis.stepsPerMm : config_.yAxis.stepsPerMm;
  if (steps_per_mm <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  const uint32_t seek_delay_us = computeStepDelayUs(seekFeedMmMin, steps_per_mm);
  const uint32_t latch_delay_us = computeStepDelayUs(latchFeedMmMin, steps_per_mm);
  const uint32_t pull_off_steps =
      std::max(static_cast<uint32_t>(1U), static_cast<uint32_t>(std::lround(pullOffMm * steps_per_mm)));
  const bool home_positive = !towardMin;
  const bool backoff_positive = towardMin;

  esp_err_t err = ESP_OK;
  boardHal_.setIgnoreLimitInputs(true);
  const int64_t seek_start_ms = esp_timer_get_time() / 1000LL;
  while (!isAxisLimitActive(axis)) {
    if ((esp_timer_get_time() / 1000LL) - seek_start_ms > static_cast<int64_t>(timeoutMs)) {
      ESP_LOGE(kTag, "Homing %s seek timeout", axis == AxisId::X ? "X" : "Y");
      err = ESP_ERR_TIMEOUT;
      break;
    }
    err = moveAxisSteps(axis, home_positive, kHomingChunkSteps, seek_delay_us);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Homing %s seek move failed: %s", axis == AxisId::X ? "X" : "Y", esp_err_to_name(err));
      break;
    }
  }
  if (err != ESP_OK) {
    boardHal_.setIgnoreLimitInputs(false);
    return err;
  }

  err = moveAxisSteps(axis, backoff_positive, pull_off_steps, seek_delay_us);
  if (err != ESP_OK) {
    boardHal_.setIgnoreLimitInputs(false);
    ESP_LOGE(kTag, "Homing %s backoff failed: %s", axis == AxisId::X ? "X" : "Y", esp_err_to_name(err));
    return err;
  }
  if (isAxisLimitActive(axis)) {
    boardHal_.setIgnoreLimitInputs(false);
    ESP_LOGE(kTag, "Homing %s failed: switch stayed active after backoff", axis == AxisId::X ? "X" : "Y");
    return ESP_ERR_INVALID_STATE;
  }

  const int64_t latch_start_ms = esp_timer_get_time() / 1000LL;
  while (!isAxisLimitActive(axis)) {
    if ((esp_timer_get_time() / 1000LL) - latch_start_ms > static_cast<int64_t>(timeoutMs)) {
      ESP_LOGE(kTag, "Homing %s latch timeout", axis == AxisId::X ? "X" : "Y");
      err = ESP_ERR_TIMEOUT;
      break;
    }
    err = moveAxisSteps(axis, home_positive, 1U, latch_delay_us);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "Homing %s latch move failed: %s", axis == AxisId::X ? "X" : "Y", esp_err_to_name(err));
      break;
    }
  }

  boardHal_.setIgnoreLimitInputs(false);
  return err;
}

bool MotionService::isAxisLimitActive(const AxisId axis) const {
  return axis == AxisId::X ? boardHal_.isXLimitActive() : boardHal_.isYLimitActive();
}

esp_err_t MotionService::moveAxisSteps(const AxisId axis, const bool positive, const uint32_t steps,
                                       const uint32_t stepDelayUs) {
  if (axis == AxisId::X) {
    ESP_RETURN_ON_ERROR(boardHal_.prepareXAxisMove(positive), kTag, "Failed to prepare X move");
    return boardHal_.runXAxisSteps(steps, stepDelayUs);
  }
  ESP_RETURN_ON_ERROR(boardHal_.prepareYAxisMove(positive), kTag, "Failed to prepare Y move");
  return boardHal_.runYAxisSteps(steps, stepDelayUs);
}

uint32_t MotionService::computeStepDelayUs(const float feedMmMin, const float stepsPerMm) {
  if (feedMmMin <= 0.0f || stepsPerMm <= 0.0f) {
    return 1U;
  }
  const float delay = (60.0f * 1000000.0f) / (feedMmMin * stepsPerMm);
  return static_cast<uint32_t>(std::max(1.0f, delay));
}

esp_err_t MotionService::executeFrame() {
  const float frame_feed_mm_min = std::fmin(kDefaultFrameFeedMmMin, std::fmin(config_.xAxis.maxRateMmMin, config_.yAxis.maxRateMmMin));
  if (frame_feed_mm_min <= 0.0f || config_.xAxis.travelMm <= 0.0f || config_.yAxis.travelMm <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  const MotionStateSnapshot state = snapshot();
  ESP_LOGI(kTag, "Frame requested: width=%.3f height=%.3f feed=%.1f", static_cast<double>(config_.xAxis.travelMm),
           static_cast<double>(config_.yAxis.travelMm), static_cast<double>(frame_feed_mm_min));

  ESP_RETURN_ON_ERROR(executeLinearMove(-state.position.x, -state.position.y, frame_feed_mm_min, shared::MotionOperation::Frame), kTag,
                      "Failed to move to frame origin");
  ESP_RETURN_ON_ERROR(executeLinearMove(config_.xAxis.travelMm, 0.0f, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 1");
  ESP_RETURN_ON_ERROR(executeLinearMove(0.0f, config_.yAxis.travelMm, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 2");
  ESP_RETURN_ON_ERROR(executeLinearMove(-config_.xAxis.travelMm, 0.0f, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 3");
  ESP_RETURN_ON_ERROR(executeLinearMove(0.0f, -config_.yAxis.travelMm, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 4");

  lockState();
  position_ = {0.0f, 0.0f};
  unlockState();
  ESP_LOGI(kTag, "Frame complete, returned to work zero");
  return ESP_OK;
}

esp_err_t MotionService::executeJog(const MotionCommand &command) {
  const float delta_x_mm = command.axis == AxisId::X ? command.distanceMm : 0.0f;
  const float delta_y_mm = command.axis == AxisId::Y ? command.distanceMm : 0.0f;
  return executeLinearMove(delta_x_mm, delta_y_mm, command.feedMmMin, shared::MotionOperation::Jog);
}

esp_err_t MotionService::executeGoToZero(const MotionCommand &command) {
  const MotionStateSnapshot state = snapshot();
  return executeLinearMove(-state.position.x, -state.position.y, command.feedMmMin, shared::MotionOperation::GoToZero);
}

esp_err_t MotionService::executeMoveTo(const MotionCommand &command) {
  const MotionStateSnapshot state = snapshot();
  return executeLinearMove(command.targetXmm - state.position.x, command.targetYmm - state.position.y, command.feedMmMin,
                           shared::MotionOperation::MoveTo);
}

esp_err_t MotionService::executeLinearMove(const float deltaXmm, const float deltaYmm, const float feedMmMin,
                                           const shared::MotionOperation operation) {
  const MotionStateSnapshot state = snapshot();
  if (!state.motorsHeld) {
    ESP_RETURN_ON_ERROR(boardHal_.setMotorsEnabled(true), kTag, "Failed to enable motors before linear move");
    lockState();
    motorsHeld_ = true;
    unlockState();
  }

  LinearMovePlan plan{};
  ESP_RETURN_ON_ERROR(BuildLinearMovePlan(config_, deltaXmm, deltaYmm, feedMmMin, plan), kTag,
                      "Failed to build linear move plan");
  if (plan.xSteps == 0U && plan.ySteps == 0U) {
    if (operation == shared::MotionOperation::GoToZero) {
      ESP_LOGI(kTag, "Already at work zero");
    }
    return ESP_OK;
  }

  const bool ignore_limits = operation == shared::MotionOperation::Home;
  boardHal_.setIgnoreLimitInputs(ignore_limits);
  const esp_err_t move_err =
      boardHal_.moveXYLinear(deltaXmm > 0.0f, plan.xSteps, deltaYmm > 0.0f, plan.ySteps, plan.stepDelayUs);
  boardHal_.setIgnoreLimitInputs(false);
  ESP_RETURN_ON_ERROR(move_err, kTag, "Linear move failed");

  lockState();
  position_.x += deltaXmm;
  position_.y += deltaYmm;

  if (operation == shared::MotionOperation::GoToZero) {
    position_ = {0.0f, 0.0f};
    ESP_LOGI(kTag, "Returned to work zero");
  }
  unlockState();

  return ESP_OK;
}

bool MotionService::isWithinSoftLimits(const float xMm, const float yMm) const {
  return xMm >= -kSoftLimitEpsilonMm && yMm >= -kSoftLimitEpsilonMm &&
         xMm <= (config_.xAxis.travelMm + kSoftLimitEpsilonMm) &&
         yMm <= (config_.yAxis.travelMm + kSoftLimitEpsilonMm);
}

esp_err_t MotionService::validateTargetPosition(const float xMm, const float yMm,
                                                const shared::MotionOperation operation) const {
  if (isWithinSoftLimits(xMm, yMm)) {
    return ESP_OK;
  }

  ESP_LOGW(kTag, "Soft limit hit: op=%s target=(%.3f, %.3f) limits=(0..%.3f, 0..%.3f)",
           shared::ToString(operation), static_cast<double>(xMm), static_cast<double>(yMm),
           static_cast<double>(config_.xAxis.travelMm), static_cast<double>(config_.yAxis.travelMm));
  return ESP_ERR_INVALID_ARG;
}

void MotionService::lockState() const {
  if (stateMutex_ != nullptr) {
    (void)xSemaphoreTakeRecursive(stateMutex_, portMAX_DELAY);
  }
}

void MotionService::unlockState() const {
  if (stateMutex_ != nullptr) {
    (void)xSemaphoreGiveRecursive(stateMutex_);
  }
}

}  // namespace motion
