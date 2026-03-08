#pragma once

#include "shared/machine_config.hpp"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"

#include <atomic>

namespace hal {

class BoardHal {
 public:
  BoardHal();
  ~BoardHal();

  esp_err_t initialize(const shared::MachineConfig &config);
  esp_err_t setMotorsEnabled(bool enabled);
  esp_err_t setLaserGate(bool enabled);
  esp_err_t stepXAxis(bool positive, uint32_t steps, uint32_t stepDelayUs);
  esp_err_t stepYAxis(bool positive, uint32_t steps, uint32_t stepDelayUs);
  esp_err_t prepareXAxisMove(bool positive);
  esp_err_t prepareYAxisMove(bool positive);
  esp_err_t runXAxisSteps(uint32_t steps, uint32_t stepDelayUs);
  esp_err_t runYAxisSteps(uint32_t steps, uint32_t stepDelayUs);
  esp_err_t moveXYLinear(bool xPositive, uint32_t xSteps, bool yPositive, uint32_t ySteps, uint32_t stepDelayUs);
  bool isXLimitActive() const;
  bool isYLimitActive() const;
  bool isEstopActive() const;
  bool isLidOpen() const;
  bool isAnyLimitActive() const;
  void setIgnoreLimitInputs(bool ignore);
  void requestMotionStop();
  void clearMotionStop();

 private:
  struct AxisRmtResources {
    rmt_channel_handle_t primaryChannel;
    rmt_channel_handle_t secondaryChannel;
    rmt_sync_manager_handle_t syncManager;
  };

  esp_err_t configureMotionPins();
  esp_err_t initializeRmt();
  esp_err_t createTxChannel(int pin, rmt_channel_handle_t &channel);
  esp_err_t destroyAxisRmt(AxisRmtResources &axis);
  esp_err_t setAxisRmtEnabled(AxisRmtResources &axis, bool useSecondary, bool enabled);
  esp_err_t transmitSteps(AxisRmtResources &axis, bool useSecondary, uint32_t steps, uint32_t stepDelayUs);
  esp_err_t transmitAxisSymbols(AxisRmtResources &axis, bool useSecondary, const rmt_symbol_word_t *symbols,
                                size_t count);
  esp_err_t abortAxisTransmission(AxisRmtResources &axis, bool useSecondary);
  esp_err_t waitForAxisTransmission(AxisRmtResources &axis, bool useSecondary, size_t chunkSteps,
                                    uint32_t stepDelayUs);
  size_t computeSafeChunkSteps(uint32_t stepDelayUs, size_t remainingSteps) const;
  esp_err_t setXAxisDirections(bool positive);
  esp_err_t setYAxisDirections(bool positive);
  esp_err_t fillStepSymbol(rmt_symbol_word_t &symbol, bool pulse, uint32_t stepDelayUs) const;
  esp_err_t fillStepSymbols(rmt_symbol_word_t *symbols, size_t count, uint32_t stepDelayUs) const;
  bool isInputActive(int pin, bool activeLow) const;
  bool isSafetyTripActive() const;

  shared::MachineConfig config_;
  bool initialized_;
  rmt_encoder_handle_t copyEncoder_;
  AxisRmtResources xAxisRmt_;
  AxisRmtResources yAxisRmt_;
  std::atomic<bool> stopRequested_;
  std::atomic<bool> ignoreLimitInputs_;
};

}  // namespace hal
