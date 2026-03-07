#include "laser/laser_service.hpp"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

namespace laser {

namespace {

constexpr const char *kTag = "laser";

}  // namespace

namespace {

constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_0;

ledc_timer_bit_t ResolveDutyResolution(const uint32_t bits) {
  switch (bits) {
    case 1:
      return LEDC_TIMER_1_BIT;
    case 2:
      return LEDC_TIMER_2_BIT;
    case 3:
      return LEDC_TIMER_3_BIT;
    case 4:
      return LEDC_TIMER_4_BIT;
    case 5:
      return LEDC_TIMER_5_BIT;
    case 6:
      return LEDC_TIMER_6_BIT;
    case 7:
      return LEDC_TIMER_7_BIT;
    case 8:
      return LEDC_TIMER_8_BIT;
    case 9:
      return LEDC_TIMER_9_BIT;
    case 10:
      return LEDC_TIMER_10_BIT;
    case 11:
      return LEDC_TIMER_11_BIT;
    case 12:
      return LEDC_TIMER_12_BIT;
    case 13:
      return LEDC_TIMER_13_BIT;
    case 14:
      return LEDC_TIMER_14_BIT;
    default:
      return LEDC_TIMER_8_BIT;
  }
}

}  // namespace

LaserService::LaserService(const shared::MachineConfig &config, hal::BoardHal &boardHal)
    : config_(config), boardHal_(boardHal), armed_(false), currentPower_(0), pwmConfigured_(false) {}

esp_err_t LaserService::start() {
  ESP_RETURN_ON_ERROR(configurePwm(), kTag, "Failed to configure laser PWM");
  ESP_RETURN_ON_ERROR(forceOff(), kTag, "Failed to force laser off");
  ESP_LOGI(kTag, "Laser service started");
  return ESP_OK;
}

esp_err_t LaserService::arm() {
  armed_ = true;
  ESP_LOGI(kTag, "Laser armed");
  return forceOff();
}

esp_err_t LaserService::disarm() {
  armed_ = false;
  ESP_LOGI(kTag, "Laser disarmed");
  return forceOff();
}

esp_err_t LaserService::setPower(const uint8_t power) {
  if (!armed_ || power == 0) {
    currentPower_ = 0;
    ESP_RETURN_ON_ERROR(applyDuty(0), kTag, "Failed to clear laser duty");
    return boardHal_.setLaserGate(false);
  }

  currentPower_ = power;
  ESP_RETURN_ON_ERROR(applyDuty(power), kTag, "Failed to set laser duty");
  return boardHal_.setLaserGate(true);
}

esp_err_t LaserService::forceOff() {
  currentPower_ = 0;
  ESP_RETURN_ON_ERROR(applyDuty(0), kTag, "Failed to clear laser duty");
  return boardHal_.setLaserGate(false);
}

esp_err_t LaserService::testPulse(const uint8_t power, const uint32_t durationMs) {
  if (power == 0 || durationMs == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(arm(), kTag, "Failed to arm laser");
  ESP_RETURN_ON_ERROR(setPower(power), kTag, "Failed to start test pulse");
  vTaskDelay(pdMS_TO_TICKS(durationMs));
  ESP_RETURN_ON_ERROR(forceOff(), kTag, "Failed to stop test pulse");
  return disarm();
}

bool LaserService::isArmed() const {
  return armed_;
}

uint8_t LaserService::currentPower() const {
  return currentPower_;
}

esp_err_t LaserService::configurePwm() {
  if (pwmConfigured_) {
    return ESP_OK;
  }

  ledc_timer_config_t timer_config{};
  timer_config.speed_mode = kLedcMode;
  timer_config.timer_num = kLedcTimer;
  timer_config.duty_resolution = ResolveDutyResolution(config_.laser.pwmResolutionBits);
  timer_config.freq_hz = static_cast<uint32_t>(config_.laser.pwmFreqHz);
  timer_config.clk_cfg = LEDC_AUTO_CLK;
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), kTag, "LEDC timer config failed");

  ledc_channel_config_t channel_config{};
  channel_config.gpio_num = config_.pins.laserPwm;
  channel_config.speed_mode = kLedcMode;
  channel_config.channel = kLedcChannel;
  channel_config.intr_type = LEDC_INTR_DISABLE;
  channel_config.timer_sel = kLedcTimer;
  channel_config.duty = 0;
  channel_config.hpoint = 0;
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), kTag, "LEDC channel config failed");

  pwmConfigured_ = true;
  return ESP_OK;
}

esp_err_t LaserService::applyDuty(const uint8_t power) {
  if (!pwmConfigured_) {
    return ESP_ERR_INVALID_STATE;
  }

  const uint32_t resolution_max = (1U << config_.laser.pwmResolutionBits) - 1U;
  const uint32_t config_max = std::max<uint32_t>(1U, config_.laser.pwmMax);
  const uint32_t pwm_min = std::min(config_.laser.pwmMin, config_.laser.pwmMax);
  const uint32_t pwm_max = std::max(config_.laser.pwmMin, config_.laser.pwmMax);
  const uint32_t clamped = std::min(static_cast<uint32_t>(power), config_max);

  uint32_t logical_pwm = 0;
  if (clamped > 0U) {
    const uint32_t range = pwm_max - pwm_min;
    logical_pwm = pwm_min + ((clamped * range) / config_max);
  }

  uint32_t duty = (logical_pwm * resolution_max) / config_max;
  if (!config_.laser.activeHigh) {
    duty = resolution_max - duty;
  }

  ESP_RETURN_ON_ERROR(ledc_set_duty(kLedcMode, kLedcChannel, duty), kTag, "LEDC set duty failed");
  return ledc_update_duty(kLedcMode, kLedcChannel);
}

}  // namespace laser
