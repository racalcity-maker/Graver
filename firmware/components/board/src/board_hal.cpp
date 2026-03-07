#include "hal/board_hal.hpp"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>

namespace hal {

namespace {

constexpr const char *kTag = "hal";
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr size_t kRmtMemBlockSymbols = 64;
constexpr size_t kRmtChunkSteps = 128;
constexpr int kRmtWaitMarginMs = 20;
constexpr uint32_t kLinearMoveYieldIntervalSteps = 32;

esp_err_t ConfigureOutput(const int pin, const int level) {
  if (pin < 0) {
    return ESP_OK;
  }

  gpio_config_t output_config{};
  output_config.pin_bit_mask = 1ULL << pin;
  output_config.mode = GPIO_MODE_OUTPUT;
  output_config.pull_up_en = GPIO_PULLUP_DISABLE;
  output_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  output_config.intr_type = GPIO_INTR_DISABLE;

  ESP_RETURN_ON_ERROR(gpio_config(&output_config), kTag, "Failed to configure output pin %d", pin);
  return gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

esp_err_t ConfigureInput(const int pin, const bool usePullup) {
  if (pin < 0) {
    return ESP_OK;
  }

  gpio_config_t input_config{};
  input_config.pin_bit_mask = 1ULL << pin;
  input_config.mode = GPIO_MODE_INPUT;
  input_config.pull_up_en = usePullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  input_config.intr_type = GPIO_INTR_DISABLE;

  return gpio_config(&input_config);
}

}  // namespace

BoardHal::BoardHal()
    : config_(),
      initialized_(false),
      copyEncoder_(nullptr),
      xAxisRmt_{},
      yAxisRmt_{},
      stopRequested_(false) {}

BoardHal::~BoardHal() {
  (void)destroyAxisRmt(xAxisRmt_);
  (void)destroyAxisRmt(yAxisRmt_);
  if (copyEncoder_ != nullptr) {
    (void)rmt_del_encoder(copyEncoder_);
    copyEncoder_ = nullptr;
  }
}

esp_err_t BoardHal::initialize(const shared::MachineConfig &config) {
  config_ = config;

  ESP_RETURN_ON_ERROR(
      ConfigureOutput(config_.pins.motorsEnable, config_.motion.invertEnable ? 1 : 0), kTag,
      "Failed to set motor enable pin");
  ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.laserGate, config_.laser.gateActiveHigh ? 0 : 1), kTag,
                      "Failed to set laser gate pin");
  ESP_RETURN_ON_ERROR(configureMotionPins(), kTag, "Failed to set motion pins");
  ESP_RETURN_ON_ERROR(ConfigureInput(config_.pins.xLimit, config_.safety.limitInputsPullup), kTag,
                      "Failed to set X limit pin");
  ESP_RETURN_ON_ERROR(ConfigureInput(config_.pins.yLimit, config_.safety.limitInputsPullup), kTag,
                      "Failed to set Y limit pin");
  ESP_RETURN_ON_ERROR(ConfigureInput(config_.pins.estop, config_.safety.estopPullup), kTag,
                      "Failed to set E-stop pin");
  ESP_RETURN_ON_ERROR(ConfigureInput(config_.pins.lidInterlock, config_.safety.lidInterlockPullup), kTag,
                      "Failed to set lid interlock pin");

  initialized_ = true;
  ESP_LOGI(kTag, "Board HAL initialized with software step engine");
  return ESP_OK;
}

esp_err_t BoardHal::setMotorsEnabled(const bool enabled) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  const bool pin_level = config_.motion.invertEnable ? !enabled : enabled;
  return gpio_set_level(static_cast<gpio_num_t>(config_.pins.motorsEnable), pin_level ? 1 : 0);
}

esp_err_t BoardHal::setLaserGate(const bool enabled) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  if (config_.pins.laserGate < 0) {
    return ESP_OK;
  }

  const bool pin_level = config_.laser.gateActiveHigh ? enabled : !enabled;
  return gpio_set_level(static_cast<gpio_num_t>(config_.pins.laserGate), pin_level ? 1 : 0);
}

esp_err_t BoardHal::stepXAxis(const bool positive, const uint32_t steps, const uint32_t stepDelayUs) {
  return moveXYLinear(positive, steps, true, 0, stepDelayUs);
}

esp_err_t BoardHal::stepYAxis(const bool positive, const uint32_t steps, const uint32_t stepDelayUs) {
  return moveXYLinear(true, 0, positive, steps, stepDelayUs);
}

esp_err_t BoardHal::prepareXAxisMove(const bool positive) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(setXAxisDirections(positive), kTag, "Failed to set X direction");
  esp_rom_delay_us(config_.motion.dirSetupUs);
  return ESP_OK;
}

esp_err_t BoardHal::runXAxisSteps(const uint32_t steps, const uint32_t stepDelayUs) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (steps == 0) {
    return ESP_OK;
  }

  const uint32_t high_us = config_.motion.stepPulseUs;
  const uint32_t low_us = stepDelayUs > high_us ? stepDelayUs - high_us : 1;

  for (uint32_t i = 0; i < steps; ++i) {
    if (stopRequested_.load()) {
      return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(pulseAxisSteps(true, false), kTag, "Failed to pulse X step");
    esp_rom_delay_us(high_us);
    ESP_RETURN_ON_ERROR(pulseAxisSteps(false, false), kTag, "Failed to clear X step");
    esp_rom_delay_us(low_us);

    if (((i + 1U) % kLinearMoveYieldIntervalSteps) == 0U) {
      vTaskDelay(1);
    }
  }

  return ESP_OK;
}

esp_err_t BoardHal::moveXYLinear(const bool xPositive, const uint32_t xSteps, const bool yPositive,
                                 const uint32_t ySteps, const uint32_t stepDelayUs) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSteps == 0 && ySteps == 0) {
    return ESP_OK;
  }

  clearMotionStop();
  ESP_RETURN_ON_ERROR(setXAxisDirections(xPositive), kTag, "Failed to set X direction");
  ESP_RETURN_ON_ERROR(setYAxisDirections(yPositive), kTag, "Failed to set Y direction");
  esp_rom_delay_us(config_.motion.dirSetupUs);

  const uint32_t major_steps = xSteps > ySteps ? xSteps : ySteps;
  uint32_t x_accumulator = 0;
  uint32_t y_accumulator = 0;
  const uint32_t high_us = config_.motion.stepPulseUs;
  const uint32_t low_us = stepDelayUs > high_us ? stepDelayUs - high_us : 1;

  for (uint32_t i = 0; i < major_steps; ++i) {
    if (stopRequested_.load()) {
      return ESP_ERR_INVALID_STATE;
    }

    bool pulse_x = false;
    bool pulse_y = false;

    x_accumulator += xSteps;
    if (x_accumulator >= major_steps) {
      x_accumulator -= major_steps;
      pulse_x = xSteps > 0;
    }

    y_accumulator += ySteps;
    if (y_accumulator >= major_steps) {
      y_accumulator -= major_steps;
      pulse_y = ySteps > 0;
    }

    ESP_RETURN_ON_ERROR(pulseAxisSteps(pulse_x, pulse_y), kTag, "Failed to pulse XY steps");
    esp_rom_delay_us(high_us);
    ESP_RETURN_ON_ERROR(pulseAxisSteps(false, false), kTag, "Failed to clear XY step pins");
    esp_rom_delay_us(low_us);

    if (((i + 1U) % kLinearMoveYieldIntervalSteps) == 0U) {
      vTaskDelay(1);
    }
  }

  return ESP_OK;
}

void BoardHal::requestMotionStop() {
  stopRequested_.store(true);
}

void BoardHal::clearMotionStop() {
  stopRequested_.store(false);
}

esp_err_t BoardHal::configureMotionPins() {
  ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.xStep, 0), kTag, "Failed to set X step");
  ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.xDir, 0), kTag, "Failed to set X dir");
  if (config_.xAxis.dualMotor && config_.xAxis.secondaryUsesSeparateDriver) {
    ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.xSecondaryStep, 0), kTag, "Failed to set X secondary step");
    ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.xSecondaryDir, 0), kTag, "Failed to set X secondary dir");
  }
  ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.yStep, 0), kTag, "Failed to set Y step");
  ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.yDir, 0), kTag, "Failed to set Y dir");
  if (config_.yAxis.dualMotor && config_.yAxis.secondaryUsesSeparateDriver) {
    ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.ySecondaryStep, 0), kTag, "Failed to set Y secondary step");
    ESP_RETURN_ON_ERROR(ConfigureOutput(config_.pins.ySecondaryDir, 0), kTag, "Failed to set Y secondary dir");
  }
  return ESP_OK;
}

esp_err_t BoardHal::initializeRmt() {
  rmt_copy_encoder_config_t copy_config{};
  ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_config, &copyEncoder_), kTag, "Failed to create RMT copy encoder");

  ESP_RETURN_ON_ERROR(createTxChannel(config_.pins.xStep, xAxisRmt_.primaryChannel), kTag, "Failed to create X channel");
  if (config_.xAxis.dualMotor && config_.xAxis.secondaryUsesSeparateDriver) {
    ESP_RETURN_ON_ERROR(createTxChannel(config_.pins.xSecondaryStep, xAxisRmt_.secondaryChannel), kTag,
                        "Failed to create X secondary channel");
    rmt_channel_handle_t channels[] = {xAxisRmt_.primaryChannel, xAxisRmt_.secondaryChannel};
    rmt_sync_manager_config_t sync_config{.tx_channel_array = channels, .array_size = 2};
    ESP_RETURN_ON_ERROR(rmt_new_sync_manager(&sync_config, &xAxisRmt_.syncManager), kTag,
                        "Failed to create X sync manager");
  }

  ESP_RETURN_ON_ERROR(createTxChannel(config_.pins.yStep, yAxisRmt_.primaryChannel), kTag, "Failed to create Y channel");
  if (config_.yAxis.dualMotor && config_.yAxis.secondaryUsesSeparateDriver) {
    ESP_RETURN_ON_ERROR(createTxChannel(config_.pins.ySecondaryStep, yAxisRmt_.secondaryChannel), kTag,
                        "Failed to create Y secondary channel");
    rmt_channel_handle_t channels[] = {yAxisRmt_.primaryChannel, yAxisRmt_.secondaryChannel};
    rmt_sync_manager_config_t sync_config{.tx_channel_array = channels, .array_size = 2};
    ESP_RETURN_ON_ERROR(rmt_new_sync_manager(&sync_config, &yAxisRmt_.syncManager), kTag,
                        "Failed to create Y sync manager");
  }

  return ESP_OK;
}

esp_err_t BoardHal::createTxChannel(const int pin, rmt_channel_handle_t &channel) {
  if (pin < 0) {
    channel = nullptr;
    return ESP_OK;
  }

  rmt_tx_channel_config_t channel_config{};
  channel_config.gpio_num = static_cast<gpio_num_t>(pin);
  channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
  channel_config.resolution_hz = kRmtResolutionHz;
  channel_config.mem_block_symbols = kRmtMemBlockSymbols;
  channel_config.trans_queue_depth = 1;
  channel_config.intr_priority = 0;

  ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel_config, &channel), kTag, "Failed to allocate RMT TX channel");
  return rmt_enable(channel);
}

esp_err_t BoardHal::destroyAxisRmt(AxisRmtResources &axis) {
  if (axis.syncManager != nullptr) {
    (void)rmt_del_sync_manager(axis.syncManager);
    axis.syncManager = nullptr;
  }
  if (axis.secondaryChannel != nullptr) {
    (void)rmt_disable(axis.secondaryChannel);
    (void)rmt_del_channel(axis.secondaryChannel);
    axis.secondaryChannel = nullptr;
  }
  if (axis.primaryChannel != nullptr) {
    (void)rmt_disable(axis.primaryChannel);
    (void)rmt_del_channel(axis.primaryChannel);
    axis.primaryChannel = nullptr;
  }
  return ESP_OK;
}

esp_err_t BoardHal::setAxisRmtEnabled(AxisRmtResources &axis, const bool useSecondary, const bool enabled) {
  if (axis.primaryChannel != nullptr) {
    if (enabled) {
      ESP_RETURN_ON_ERROR(rmt_enable(axis.primaryChannel), kTag, "Failed to enable primary RMT channel");
    } else {
      ESP_RETURN_ON_ERROR(rmt_disable(axis.primaryChannel), kTag, "Failed to disable primary RMT channel");
    }
  }

  if (useSecondary && axis.secondaryChannel != nullptr) {
    if (enabled) {
      ESP_RETURN_ON_ERROR(rmt_enable(axis.secondaryChannel), kTag, "Failed to enable secondary RMT channel");
    } else {
      ESP_RETURN_ON_ERROR(rmt_disable(axis.secondaryChannel), kTag, "Failed to disable secondary RMT channel");
    }
  }

  return ESP_OK;
}

esp_err_t BoardHal::transmitSteps(AxisRmtResources &axis, const bool useSecondary, const uint32_t steps,
                                  const uint32_t stepDelayUs) {
  if (steps == 0 || axis.primaryChannel == nullptr || copyEncoder_ == nullptr) {
    return ESP_OK;
  }

  rmt_transmit_config_t transmit_config{};
  transmit_config.loop_count = 0;
  transmit_config.flags.eot_level = 0;
  transmit_config.flags.queue_nonblocking = 0;

  std::array<rmt_symbol_word_t, kRmtChunkSteps> symbols{};
  uint32_t remaining_steps = steps;
  while (remaining_steps > 0) {
    if (stopRequested_.load()) {
      ESP_RETURN_ON_ERROR(abortAxisTransmission(axis, useSecondary), kTag, "Failed to abort axis transmission");
      return ESP_ERR_INVALID_STATE;
    }

    const size_t chunk_steps = remaining_steps > kRmtChunkSteps ? kRmtChunkSteps : remaining_steps;
    ESP_RETURN_ON_ERROR(fillStepSymbols(symbols.data(), chunk_steps, stepDelayUs), kTag, "Failed to prepare symbols");

    ESP_RETURN_ON_ERROR(rmt_encoder_reset(copyEncoder_), kTag, "Failed to reset RMT encoder");
    ESP_RETURN_ON_ERROR(rmt_transmit(axis.primaryChannel, copyEncoder_, symbols.data(),
                                     chunk_steps * sizeof(rmt_symbol_word_t), &transmit_config),
                        kTag, "Failed to transmit primary steps");
    if (useSecondary && axis.secondaryChannel != nullptr) {
      ESP_RETURN_ON_ERROR(rmt_encoder_reset(copyEncoder_), kTag, "Failed to reset RMT encoder");
      ESP_RETURN_ON_ERROR(rmt_transmit(axis.secondaryChannel, copyEncoder_, symbols.data(),
                                       chunk_steps * sizeof(rmt_symbol_word_t), &transmit_config),
                          kTag, "Failed to transmit secondary steps");
    }

    ESP_RETURN_ON_ERROR(waitForAxisTransmission(axis, useSecondary, chunk_steps, stepDelayUs), kTag,
                        "Failed waiting for RMT transmission");
    remaining_steps -= static_cast<uint32_t>(chunk_steps);
  }

  return ESP_OK;
}

esp_err_t BoardHal::abortAxisTransmission(AxisRmtResources &axis, const bool useSecondary) {
  if (axis.primaryChannel != nullptr) {
    (void)rmt_disable(axis.primaryChannel);
    (void)rmt_enable(axis.primaryChannel);
  }
  if (useSecondary && axis.secondaryChannel != nullptr) {
    (void)rmt_disable(axis.secondaryChannel);
    (void)rmt_enable(axis.secondaryChannel);
  }
  if (useSecondary && axis.syncManager != nullptr) {
    (void)rmt_sync_reset(axis.syncManager);
  }
  return ESP_OK;
}

esp_err_t BoardHal::waitForAxisTransmission(AxisRmtResources &axis, const bool useSecondary, const size_t chunkSteps,
                                            const uint32_t stepDelayUs) {
  if (stopRequested_.load()) {
    return abortAxisTransmission(axis, useSecondary);
  }

  const uint64_t expected_us = static_cast<uint64_t>(chunkSteps) * static_cast<uint64_t>(stepDelayUs);
  const int timeout_ms = static_cast<int>((expected_us + 999ULL) / 1000ULL) + kRmtWaitMarginMs;

  ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(axis.primaryChannel, timeout_ms), kTag,
                      "Primary RMT transmission timeout");
  if (useSecondary && axis.secondaryChannel != nullptr) {
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(axis.secondaryChannel, timeout_ms), kTag,
                        "Secondary RMT transmission timeout");
  }

  if (useSecondary && axis.syncManager != nullptr) {
    ESP_RETURN_ON_ERROR(rmt_sync_reset(axis.syncManager), kTag, "Failed to reset sync manager");
  }

  return ESP_OK;
}

esp_err_t BoardHal::setXAxisDirections(const bool positive) {
  const bool primary_direction = config_.xAxis.invertDirPrimary ? !positive : positive;
  const bool secondary_direction = config_.xAxis.invertDirSecondary ? !positive : positive;

  ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.xDir), primary_direction ? 1 : 0), kTag,
                      "Failed to set X dir");
  if (config_.xAxis.dualMotor && config_.xAxis.secondaryUsesSeparateDriver) {
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.xSecondaryDir), secondary_direction ? 1 : 0),
                        kTag, "Failed to set X secondary dir");
  }
  esp_rom_delay_us(config_.motion.dirHoldUs);
  return ESP_OK;
}

esp_err_t BoardHal::setYAxisDirections(const bool positive) {
  const bool primary_direction = config_.yAxis.invertDirPrimary ? !positive : positive;
  const bool secondary_direction = config_.yAxis.invertDirSecondary ? !positive : positive;

  ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.yDir), primary_direction ? 1 : 0), kTag,
                      "Failed to set Y dir");
  if (config_.yAxis.dualMotor && config_.yAxis.secondaryUsesSeparateDriver) {
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.ySecondaryDir), secondary_direction ? 1 : 0),
                        kTag, "Failed to set Y secondary dir");
  }
  esp_rom_delay_us(config_.motion.dirHoldUs);
  return ESP_OK;
}

esp_err_t BoardHal::pulseAxisSteps(const bool pulseX, const bool pulseY) {
  if (config_.pins.xStep >= 0) {
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.xStep), pulseX ? 1 : 0), kTag,
                        "Failed to set X step");
  }
  if (config_.xAxis.dualMotor && config_.xAxis.secondaryUsesSeparateDriver && config_.pins.xSecondaryStep >= 0) {
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.xSecondaryStep), pulseX ? 1 : 0), kTag,
                        "Failed to set X secondary step");
  }

  if (config_.pins.yStep >= 0) {
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.yStep), pulseY ? 1 : 0), kTag,
                        "Failed to set Y step");
  }
  if (config_.yAxis.dualMotor && config_.yAxis.secondaryUsesSeparateDriver && config_.pins.ySecondaryStep >= 0) {
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config_.pins.ySecondaryStep), pulseY ? 1 : 0), kTag,
                        "Failed to set Y secondary step");
  }

  return ESP_OK;
}

esp_err_t BoardHal::fillStepSymbols(rmt_symbol_word_t *symbols, const size_t count, const uint32_t stepDelayUs) const {
  if (symbols == nullptr || count == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  const uint32_t high_us = config_.motion.stepPulseUs;
  const uint32_t low_us = stepDelayUs > high_us ? stepDelayUs - high_us : 1;
  for (size_t i = 0; i < count; ++i) {
    symbols[i].level0 = 1;
    symbols[i].duration0 = high_us;
    symbols[i].level1 = 0;
    symbols[i].duration1 = low_us;
  }

  return ESP_OK;
}

}  // namespace hal
