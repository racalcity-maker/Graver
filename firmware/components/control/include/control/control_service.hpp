#pragma once

#include "jobs/job_manager.hpp"
#include "jobs/raster_executor.hpp"
#include "jobs/vector_job_executor.hpp"
#include "laser/laser_service.hpp"
#include "motion/motion_service.hpp"
#include "shared/machine_config.hpp"
#include "shared/types.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <string>

namespace control {

class ControlService {
 public:
  ControlService(const shared::MachineConfig &config, motion::MotionService &motion, laser::LaserService &laser,
                 jobs::JobManager &jobs);
  ~ControlService();

  esp_err_t start();

  esp_err_t home();
  esp_err_t setZero();
  esp_err_t goToZero(float feedMmMin);
  esp_err_t moveTo(float xMm, float yMm, float feedMmMin);
  esp_err_t runJob(const std::string &jobId);
  esp_err_t pauseJob();
  esp_err_t resumeJob();
  esp_err_t abortJob();
  esp_err_t clearAlarm();
  esp_err_t holdMotors();
  esp_err_t releaseMotors();
  esp_err_t frame();
  esp_err_t stop();
  esp_err_t jog(motion::AxisId axis, float distanceMm, float feedMmMin);
  esp_err_t laserPulse(uint8_t power, uint32_t durationMs);

  shared::MachineStatus status();

 private:
  enum class CommandType {
    Home,
    SetZero,
    GoToZero,
    MoveTo,
    HoldMotors,
    ReleaseMotors,
    Frame,
    Jog,
    LaserPulse,
    RunJob,
    Pause,
    Resume,
    Abort,
    ClearAlarm,
    JobFinished,
  };

  struct Command {
    CommandType type;
    motion::AxisId axis;
    float distanceMm;
    float feedMmMin;
    float targetXmm;
    float targetYmm;
    uint8_t power;
    uint32_t durationMs;
    esp_err_t result;
    uint32_t completedUnits;
    char jobId[64];
  };

  static void WorkerEntry(void *context);
  static void JobEntry(void *context);

  void workerLoop();
  void jobLoop(jobs::LoadedJob job);
  Command makeCommand(CommandType type, motion::AxisId axis, float distanceMm, float feedMmMin, uint8_t power,
                      uint32_t durationMs) const;
  esp_err_t enqueue(const Command &command);
  esp_err_t execute(const Command &command);
  void lockState();
  void unlockState();
  void setState(shared::MachineState state, const std::string &message);
  void setAlarm(const std::string &message);
  void tripAlarm(const std::string &reason, bool estopLatched);
  bool checkSafetyAndTrip();
  bool isAlarmLikeStateLocked() const;
  bool isRuntimeCommand(const Command &command) const;
  bool isExclusiveCommand(const Command &command) const;
  void refreshMotionStateLocked();
  void resetJobProgress();
  void finalizeStoppedJob(bool aborted);
  void clearActiveJobLocked(shared::MachineState state, const std::string &message);

  shared::MachineConfig config_;
  motion::MotionService &motion_;
  laser::LaserService &laser_;
  jobs::JobManager &jobs_;
  jobs::RasterExecutor rasterExecutor_;
  jobs::VectorJobExecutor vectorExecutor_;
  QueueHandle_t commandQueue_;
  SemaphoreHandle_t stateMutex_;
  TaskHandle_t workerTask_;
  TaskHandle_t jobTask_;
  shared::MachineState state_;
  std::string activeJobId_;
  std::string message_;
  bool started_;
  std::atomic_bool stopRequested_;
  std::atomic_bool pauseRequested_;
  std::atomic_bool abortRequested_;
  std::atomic_uint32_t jobRowsDone_;
  std::atomic_uint32_t jobRowsTotal_;
  std::atomic_uint8_t jobProgressPercent_;
};

}  // namespace control
