#pragma once

#include "hal/board_hal.hpp"
#include "shared/machine_config.hpp"
#include "shared/types.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstddef>

namespace laser {
class LaserService;
}

namespace motion {

enum class AxisId {
  X,
  Y,
};

struct RasterSegment {
  float lengthMm;
  uint8_t power;
};

struct MotionStateSnapshot {
  shared::PositionMm position;
  bool homed;
  bool motorsHeld;
  bool pending;
  shared::MotionOperation activeOperation;
  esp_err_t lastError;
  bool started;
};

class MotionService {
 public:
  MotionService(const shared::MachineConfig &config, hal::BoardHal &boardHal);
  ~MotionService();

  esp_err_t start();
  esp_err_t home();
  esp_err_t frame();
  esp_err_t jog(AxisId axis, float distanceMm, float feedMmMin);
  esp_err_t setZero();
  esp_err_t goToZero(float feedMmMin);
  esp_err_t moveTo(float xMm, float yMm, float feedMmMin);
  esp_err_t directMoveTo(float xMm, float yMm, float feedMmMin, shared::MotionOperation operation);
  esp_err_t executeRasterRow(float rowYmm, float startXmm, bool reverse, const RasterSegment *segments,
                             size_t segmentCount, float printFeedMmMin, float travelFeedMmMin,
                             laser::LaserService &laser);
  esp_err_t holdMotors();
  esp_err_t releaseMotors();
  esp_err_t stop();
  shared::PositionMm position() const;
  MotionStateSnapshot snapshot() const;
  bool isHomed() const;
  bool motorsHeld() const;
  bool isEstopActive() const;
  bool isLidOpen() const;
  bool isAnyLimitActive() const;
  bool isBusy() const;
  shared::MotionOperation activeOperation() const;
  esp_err_t lastError() const;
  esp_err_t waitForIdle(TickType_t timeoutTicks);

 private:
  struct MotionCommand {
    shared::MotionOperation operation;
    AxisId axis;
    float distanceMm;
    float feedMmMin;
    float targetXmm;
    float targetYmm;
  };

  static void WorkerEntry(void *context);
  void workerLoop();
  esp_err_t enqueueCommand(const MotionCommand &command);
  esp_err_t executeCommand(const MotionCommand &command);
  esp_err_t executeHome();
  esp_err_t executeFrame();
  esp_err_t executeJog(const MotionCommand &command);
  esp_err_t executeGoToZero(const MotionCommand &command);
  esp_err_t executeMoveTo(const MotionCommand &command);
  esp_err_t executeAxisHome(AxisId axis, bool towardMin, float seekFeedMmMin, float latchFeedMmMin, float pullOffMm,
                            uint32_t timeoutMs);
  bool isAxisLimitActive(AxisId axis) const;
  esp_err_t moveAxisSteps(AxisId axis, bool positive, uint32_t steps, uint32_t stepDelayUs);
  static uint32_t computeStepDelayUs(float feedMmMin, float stepsPerMm);
  esp_err_t executeLinearMove(float deltaXmm, float deltaYmm, float feedMmMin, shared::MotionOperation operation);
  bool isWithinSoftLimits(float xMm, float yMm) const;
  esp_err_t validateTargetPosition(float xMm, float yMm, shared::MotionOperation operation) const;
  void lockState() const;
  void unlockState() const;

  shared::MachineConfig config_;
  hal::BoardHal &boardHal_;
  shared::PositionMm position_;
  bool homed_;
  bool motorsHeld_;
  bool pending_;
  shared::MotionOperation activeOperation_;
  esp_err_t lastError_;
  QueueHandle_t commandQueue_;
  SemaphoreHandle_t completionSemaphore_;
  mutable SemaphoreHandle_t stateMutex_;
  TaskHandle_t workerTask_;
  bool started_;
};

}  // namespace motion
