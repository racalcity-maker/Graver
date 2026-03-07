#include "motion/motion_service.hpp"

#include "laser/laser_service.hpp"

#include "esp_check.h"
#include "esp_log.h"

#include <cmath>

namespace motion {

namespace {

constexpr const char *kTag = "motion";
constexpr UBaseType_t kCommandQueueLength = 1;
constexpr uint32_t kWorkerStackSize = 4096;
constexpr UBaseType_t kWorkerPriority = 8;
constexpr float kSoftLimitEpsilonMm = 0.001f;
constexpr float kDefaultFrameFeedMmMin = 1200.0f;

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
}

esp_err_t MotionService::start() {
  if (started_) {
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
  motorsHeld_ = true;
  started_ = true;
  ESP_LOGI(kTag, "Motion service started");
  return ESP_OK;
}

esp_err_t MotionService::home() {
  return enqueueCommand({shared::MotionOperation::Home, AxisId::X, 0.0f, 0.0f, 0.0f, 0.0f});
}

esp_err_t MotionService::frame() {
  if (!started_ || isBusy() || !homed_) {
    return ESP_ERR_INVALID_STATE;
  }

  return enqueueCommand({shared::MotionOperation::Frame, AxisId::X, 0.0f, 0.0f, 0.0f, 0.0f});
}

esp_err_t MotionService::jog(const AxisId axis, const float distanceMm, const float feedMmMin) {
  if (feedMmMin <= 0.0f || distanceMm == 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  const float target_x_mm = axis == AxisId::X ? position_.x + distanceMm : position_.x;
  const float target_y_mm = axis == AxisId::Y ? position_.y + distanceMm : position_.y;
  ESP_RETURN_ON_ERROR(validateTargetPosition(target_x_mm, target_y_mm, shared::MotionOperation::Jog), kTag,
                      "Jog target is outside work area");

  return enqueueCommand({shared::MotionOperation::Jog, axis, distanceMm, feedMmMin, 0.0f, 0.0f});
}

esp_err_t MotionService::setZero() {
  if (!started_ || isBusy()) {
    return ESP_ERR_INVALID_STATE;
  }

  position_ = {0.0f, 0.0f};
  homed_ = true;
  ESP_LOGI(kTag, "Work zero set at current position");
  return ESP_OK;
}

esp_err_t MotionService::goToZero(const float feedMmMin) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!started_ || isBusy() || !homed_) {
    return ESP_ERR_INVALID_STATE;
  }

  return enqueueCommand({shared::MotionOperation::GoToZero, AxisId::X, 0.0f, feedMmMin, 0.0f, 0.0f});
}

esp_err_t MotionService::moveTo(const float xMm, const float yMm, const float feedMmMin) {
  if (feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!started_ || isBusy() || !homed_) {
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
  if (!started_ || pending_ || activeOperation_ != shared::MotionOperation::None || !homed_) {
    return ESP_ERR_INVALID_STATE;
  }
  ESP_RETURN_ON_ERROR(validateTargetPosition(xMm, yMm, operation), kTag, "Direct move target is outside work area");

  activeOperation_ = operation;
  lastError_ = ESP_OK;
  const esp_err_t err = executeLinearMove(xMm - position_.x, yMm - position_.y, feedMmMin, operation);
  lastError_ = err;
  activeOperation_ = shared::MotionOperation::None;
  return err;
}

esp_err_t MotionService::executeRasterRow(const float rowYmm, const float startXmm, const bool reverse,
                                          const RasterSegment *segments, const size_t segmentCount,
                                          const float printFeedMmMin, const float travelFeedMmMin,
                                          laser::LaserService &laser) {
  if (!started_ || pending_ || activeOperation_ != shared::MotionOperation::None || !homed_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (segments == nullptr || segmentCount == 0U || printFeedMmMin <= 0.0f || travelFeedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(validateTargetPosition(startXmm, rowYmm, shared::MotionOperation::MoveTo), kTag,
                      "Raster row start is outside work area");

  activeOperation_ = shared::MotionOperation::MoveTo;
  lastError_ = ESP_OK;

  esp_err_t err = executeLinearMove(startXmm - position_.x, rowYmm - position_.y, travelFeedMmMin,
                                    shared::MotionOperation::MoveTo);
  if (err != ESP_OK) {
    lastError_ = err;
    activeOperation_ = shared::MotionOperation::None;
    return err;
  }

  if (!motorsHeld_) {
    ESP_RETURN_ON_ERROR(boardHal_.setMotorsEnabled(true), kTag, "Failed to enable motors before raster row");
    motorsHeld_ = true;
  }

  ESP_RETURN_ON_ERROR(boardHal_.prepareXAxisMove(!reverse), kTag, "Failed to prepare X direction for raster row");

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
      lastError_ = err;
      activeOperation_ = shared::MotionOperation::None;
      return err;
    }

    const uint32_t delay_us = segment.power == 0U ? travel_step_delay_us : print_step_delay_us;
    err = boardHal_.runXAxisSteps(segment_steps, delay_us);
    if (err != ESP_OK) {
      lastError_ = err;
      activeOperation_ = shared::MotionOperation::None;
      return err;
    }

    const float segment_mm = static_cast<float>(segment_steps) / x_steps_per_mm;
    delta_x_mm += reverse ? -segment_mm : segment_mm;
  }

  position_.x += delta_x_mm;
  position_.y = rowYmm;
  lastError_ = ESP_OK;
  activeOperation_ = shared::MotionOperation::None;
  return ESP_OK;
}

esp_err_t MotionService::holdMotors() {
  if (!started_ || isBusy()) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(boardHal_.setMotorsEnabled(true), kTag, "Failed to hold motors");
  motorsHeld_ = true;
  ESP_LOGI(kTag, "Motors held");
  return ESP_OK;
}

esp_err_t MotionService::releaseMotors() {
  if (!started_ || isBusy()) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(boardHal_.setMotorsEnabled(false), kTag, "Failed to release motors");
  motorsHeld_ = false;
  homed_ = false;
  ESP_LOGI(kTag, "Motors released, work position reference invalidated");
  return ESP_OK;
}

esp_err_t MotionService::stop() {
  if (!started_ || commandQueue_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  boardHal_.requestMotionStop();
  xQueueReset(commandQueue_);

  pending_ = false;
  if (activeOperation_ == shared::MotionOperation::None) {
    lastError_ = ESP_OK;
  }

  ESP_LOGW(kTag, "Motion stop requested");
  return ESP_OK;
}

shared::PositionMm MotionService::position() const {
  return position_;
}

bool MotionService::isHomed() const {
  return homed_;
}

bool MotionService::motorsHeld() const {
  return motorsHeld_;
}

bool MotionService::isBusy() const {
  return pending_ || activeOperation_ != shared::MotionOperation::None;
}

shared::MotionOperation MotionService::activeOperation() const {
  return activeOperation_;
}

esp_err_t MotionService::lastError() const {
  return lastError_;
}

esp_err_t MotionService::waitForIdle(const TickType_t timeoutTicks) {
  if (!started_ || completionSemaphore_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!isBusy()) {
    return lastError_;
  }

  if (xSemaphoreTake(completionSemaphore_, timeoutTicks) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  return lastError_;
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

    pending_ = false;
    activeOperation_ = shared::MotionOperation::None;
    lastError_ = command_err;

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
  if (!started_ || commandQueue_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  if (pending_ || activeOperation_ != shared::MotionOperation::None) {
    return ESP_ERR_INVALID_STATE;
  }
  pending_ = true;
  activeOperation_ = command.operation;
  lastError_ = ESP_OK;
  if (completionSemaphore_ != nullptr) {
    while (xSemaphoreTake(completionSemaphore_, 0) == pdTRUE) {
    }
  }

  if (xQueueSend(commandQueue_, &command, 0) != pdPASS) {
    pending_ = false;
    activeOperation_ = shared::MotionOperation::None;
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
  position_ = {0.0f, 0.0f};
  homed_ = true;

  ESP_LOGI(kTag, "Homing complete");
  return ESP_OK;
}

esp_err_t MotionService::executeFrame() {
  const float frame_feed_mm_min = std::fmin(kDefaultFrameFeedMmMin, std::fmin(config_.xAxis.maxRateMmMin, config_.yAxis.maxRateMmMin));
  if (frame_feed_mm_min <= 0.0f || config_.xAxis.travelMm <= 0.0f || config_.yAxis.travelMm <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(kTag, "Frame requested: width=%.3f height=%.3f feed=%.1f", static_cast<double>(config_.xAxis.travelMm),
           static_cast<double>(config_.yAxis.travelMm), static_cast<double>(frame_feed_mm_min));

  ESP_RETURN_ON_ERROR(executeLinearMove(-position_.x, -position_.y, frame_feed_mm_min, shared::MotionOperation::Frame), kTag,
                      "Failed to move to frame origin");
  ESP_RETURN_ON_ERROR(executeLinearMove(config_.xAxis.travelMm, 0.0f, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 1");
  ESP_RETURN_ON_ERROR(executeLinearMove(0.0f, config_.yAxis.travelMm, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 2");
  ESP_RETURN_ON_ERROR(executeLinearMove(-config_.xAxis.travelMm, 0.0f, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 3");
  ESP_RETURN_ON_ERROR(executeLinearMove(0.0f, -config_.yAxis.travelMm, frame_feed_mm_min, shared::MotionOperation::Frame),
                      kTag, "Failed to trace frame edge 4");

  position_ = {0.0f, 0.0f};
  ESP_LOGI(kTag, "Frame complete, returned to work zero");
  return ESP_OK;
}

esp_err_t MotionService::executeJog(const MotionCommand &command) {
  const float delta_x_mm = command.axis == AxisId::X ? command.distanceMm : 0.0f;
  const float delta_y_mm = command.axis == AxisId::Y ? command.distanceMm : 0.0f;
  return executeLinearMove(delta_x_mm, delta_y_mm, command.feedMmMin, shared::MotionOperation::Jog);
}

esp_err_t MotionService::executeGoToZero(const MotionCommand &command) {
  return executeLinearMove(-position_.x, -position_.y, command.feedMmMin, shared::MotionOperation::GoToZero);
}

esp_err_t MotionService::executeMoveTo(const MotionCommand &command) {
  return executeLinearMove(command.targetXmm - position_.x, command.targetYmm - position_.y, command.feedMmMin,
                           shared::MotionOperation::MoveTo);
}

esp_err_t MotionService::executeLinearMove(const float deltaXmm, const float deltaYmm, const float feedMmMin,
                                           const shared::MotionOperation operation) {
  if (!motorsHeld_) {
    ESP_RETURN_ON_ERROR(boardHal_.setMotorsEnabled(true), kTag, "Failed to enable motors before linear move");
    motorsHeld_ = true;
  }

  const uint32_t x_steps = static_cast<uint32_t>(std::lround(std::fabs(deltaXmm) * config_.xAxis.stepsPerMm));
  const uint32_t y_steps = static_cast<uint32_t>(std::lround(std::fabs(deltaYmm) * config_.yAxis.stepsPerMm));
  if (x_steps == 0 && y_steps == 0) {
    if (operation == shared::MotionOperation::GoToZero) {
      ESP_LOGI(kTag, "Already at work zero");
    }
    return ESP_OK;
  }

  const float distance_mm = std::sqrt((deltaXmm * deltaXmm) + (deltaYmm * deltaYmm));
  const uint32_t major_steps = x_steps > y_steps ? x_steps : y_steps;
  if (distance_mm <= 0.0f || major_steps == 0 || feedMmMin <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }

  const float seconds = (distance_mm * 60.0f) / feedMmMin;
  const float step_delay = (seconds * 1000000.0f) / static_cast<float>(major_steps);
  const uint32_t step_delay_us = static_cast<uint32_t>(std::max(1.0f, step_delay));

  ESP_RETURN_ON_ERROR(boardHal_.moveXYLinear(deltaXmm > 0.0f, x_steps, deltaYmm > 0.0f, y_steps, step_delay_us), kTag,
                      "Linear move failed");

  position_.x += deltaXmm;
  position_.y += deltaYmm;

  if (operation == shared::MotionOperation::GoToZero) {
    position_ = {0.0f, 0.0f};
    ESP_LOGI(kTag, "Returned to work zero");
  }

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

}  // namespace motion
