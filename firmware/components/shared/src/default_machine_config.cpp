#include "shared/default_machine_config.hpp"

#include "sdkconfig.h"

namespace shared {

namespace {

#ifndef CONFIG_LASERGRAVER_STA_SSID
#define CONFIG_LASERGRAVER_STA_SSID ""
#endif

#ifndef CONFIG_LASERGRAVER_STA_PASSWORD
#define CONFIG_LASERGRAVER_STA_PASSWORD ""
#endif

#ifdef CONFIG_LASERGRAVER_INVERT_MOTOR_ENABLE
constexpr bool kInvertMotorEnable = true;
#else
constexpr bool kInvertMotorEnable = false;
#endif

#ifdef CONFIG_LASERGRAVER_X_INVERT_DIR_PRIMARY
constexpr bool kInvertXPrimaryDir = true;
#else
constexpr bool kInvertXPrimaryDir = false;
#endif

#ifdef CONFIG_LASERGRAVER_X_INVERT_DIR_SECONDARY
constexpr bool kInvertXSecondaryDir = true;
#else
constexpr bool kInvertXSecondaryDir = false;
#endif

#ifdef CONFIG_LASERGRAVER_X_DUAL_MOTOR
constexpr bool kXDualMotor = true;
#else
constexpr bool kXDualMotor = false;
#endif

#ifdef CONFIG_LASERGRAVER_X_SECONDARY_SEPARATE_DRIVER
constexpr bool kXSecondarySeparateDriver = true;
#else
constexpr bool kXSecondarySeparateDriver = false;
#endif

#ifdef CONFIG_LASERGRAVER_Y_INVERT_DIR_PRIMARY
constexpr bool kInvertYPrimaryDir = true;
#else
constexpr bool kInvertYPrimaryDir = false;
#endif

#ifdef CONFIG_LASERGRAVER_Y_INVERT_DIR_SECONDARY
constexpr bool kInvertYSecondaryDir = true;
#else
constexpr bool kInvertYSecondaryDir = false;
#endif

#ifdef CONFIG_LASERGRAVER_Y_DUAL_MOTOR
constexpr bool kYDualMotor = true;
#else
constexpr bool kYDualMotor = false;
#endif

#ifdef CONFIG_LASERGRAVER_Y_SECONDARY_SEPARATE_DRIVER
constexpr bool kYSecondarySeparateDriver = true;
#else
constexpr bool kYSecondarySeparateDriver = false;
#endif

#ifdef CONFIG_LASERGRAVER_LASER_ACTIVE_HIGH
constexpr bool kLaserActiveHigh = true;
#else
constexpr bool kLaserActiveHigh = false;
#endif

#ifdef CONFIG_LASERGRAVER_LASER_GATE_ACTIVE_HIGH
constexpr bool kLaserGateActiveHigh = true;
#else
constexpr bool kLaserGateActiveHigh = false;
#endif

#ifdef CONFIG_LASERGRAVER_REQUIRE_HOMING_BEFORE_RUN
constexpr bool kRequireHomingBeforeRun = true;
#else
constexpr bool kRequireHomingBeforeRun = false;
#endif

#ifdef CONFIG_LASERGRAVER_HOMING_ENABLED
constexpr bool kHomingEnabled = true;
#else
constexpr bool kHomingEnabled = false;
#endif

constexpr bool kHomeXEnabled = true;
constexpr bool kHomeYEnabled = true;
constexpr bool kHomeXToMin = true;
constexpr bool kHomeYToMin = true;
constexpr float kHomingSeekFeedMmMin = 900.0f;
constexpr float kHomingLatchFeedMmMin = 240.0f;
constexpr float kHomingPullOffMm = 2.0f;
constexpr uint32_t kHomingTimeoutMs = 30000;

#ifdef CONFIG_LASERGRAVER_STOP_LASER_ON_PAUSE
constexpr bool kStopLaserOnPause = true;
#else
constexpr bool kStopLaserOnPause = false;
#endif

#ifdef CONFIG_LASERGRAVER_STOP_LASER_ON_ALARM
constexpr bool kStopLaserOnAlarm = true;
#else
constexpr bool kStopLaserOnAlarm = false;
#endif

#ifdef CONFIG_LASERGRAVER_LIMIT_INPUTS_PULLUP
constexpr bool kLimitInputsPullup = true;
#else
constexpr bool kLimitInputsPullup = false;
#endif

#ifdef CONFIG_LASERGRAVER_ESTOP_INPUT_PULLUP
constexpr bool kEstopPullup = true;
#else
constexpr bool kEstopPullup = false;
#endif

#ifdef CONFIG_LASERGRAVER_LID_INTERLOCK_INPUT_PULLUP
constexpr bool kLidInterlockPullup = true;
#else
constexpr bool kLidInterlockPullup = false;
#endif

#ifdef CONFIG_LASERGRAVER_X_LIMIT_ACTIVE_LOW
constexpr bool kXLimitActiveLow = true;
#else
constexpr bool kXLimitActiveLow = false;
#endif

#ifdef CONFIG_LASERGRAVER_Y_LIMIT_ACTIVE_LOW
constexpr bool kYLimitActiveLow = true;
#else
constexpr bool kYLimitActiveLow = false;
#endif

#ifdef CONFIG_LASERGRAVER_ESTOP_ACTIVE_LOW
constexpr bool kEstopActiveLow = true;
#else
constexpr bool kEstopActiveLow = false;
#endif

#ifdef CONFIG_LASERGRAVER_LID_INTERLOCK_ACTIVE_LOW
constexpr bool kLidInterlockActiveLow = true;
#else
constexpr bool kLidInterlockActiveLow = false;
#endif

#ifdef CONFIG_LASERGRAVER_STA_ENABLED
constexpr bool kStaEnabled = true;
#else
constexpr bool kStaEnabled = false;
#endif

}  // namespace

MachineConfig BuildDefaultMachineConfig() {
  MachineConfig config{};

  config.pins.xStep = CONFIG_LASERGRAVER_PIN_X_STEP;
  config.pins.xDir = CONFIG_LASERGRAVER_PIN_X_DIR;
  config.pins.xSecondaryStep = CONFIG_LASERGRAVER_PIN_X_SECONDARY_STEP;
  config.pins.xSecondaryDir = CONFIG_LASERGRAVER_PIN_X_SECONDARY_DIR;
  config.pins.yStep = CONFIG_LASERGRAVER_PIN_Y_STEP;
  config.pins.yDir = CONFIG_LASERGRAVER_PIN_Y_DIR;
  config.pins.ySecondaryStep = CONFIG_LASERGRAVER_PIN_Y_SECONDARY_STEP;
  config.pins.ySecondaryDir = CONFIG_LASERGRAVER_PIN_Y_SECONDARY_DIR;
  config.pins.motorsEnable = CONFIG_LASERGRAVER_PIN_MOTORS_ENABLE;
  config.pins.laserPwm = CONFIG_LASERGRAVER_PIN_LASER_PWM;
  config.pins.laserGate = CONFIG_LASERGRAVER_PIN_LASER_GATE;
  config.pins.xLimit = CONFIG_LASERGRAVER_PIN_X_LIMIT;
  config.pins.yLimit = CONFIG_LASERGRAVER_PIN_Y_LIMIT;
  config.pins.estop = CONFIG_LASERGRAVER_PIN_ESTOP;
  config.pins.lidInterlock = CONFIG_LASERGRAVER_PIN_LID_INTERLOCK;

  config.motion.stepPulseUs = CONFIG_LASERGRAVER_STEP_PULSE_US;
  config.motion.dirSetupUs = CONFIG_LASERGRAVER_DIR_SETUP_US;
  config.motion.dirHoldUs = CONFIG_LASERGRAVER_DIR_HOLD_US;
  config.motion.invertEnable = kInvertMotorEnable;

  config.xAxis.stepsPerMm = static_cast<float>(CONFIG_LASERGRAVER_X_STEPS_PER_MM);
  config.xAxis.maxRateMmMin = static_cast<float>(CONFIG_LASERGRAVER_X_MAX_RATE_MM_MIN);
  config.xAxis.accelerationMmS2 = static_cast<float>(CONFIG_LASERGRAVER_X_ACCEL_MM_S2);
  config.xAxis.invertDirPrimary = kInvertXPrimaryDir;
  config.xAxis.invertDirSecondary = kInvertXSecondaryDir;
  config.xAxis.dualMotor = kXDualMotor;
  config.xAxis.secondaryUsesSeparateDriver = kXSecondarySeparateDriver;
  config.xAxis.travelMm = static_cast<float>(CONFIG_LASERGRAVER_X_TRAVEL_MM);

  config.yAxis.stepsPerMm = static_cast<float>(CONFIG_LASERGRAVER_Y_STEPS_PER_MM);
  config.yAxis.maxRateMmMin = static_cast<float>(CONFIG_LASERGRAVER_Y_MAX_RATE_MM_MIN);
  config.yAxis.accelerationMmS2 = static_cast<float>(CONFIG_LASERGRAVER_Y_ACCEL_MM_S2);
  config.yAxis.invertDirPrimary = kInvertYPrimaryDir;
  config.yAxis.invertDirSecondary = kInvertYSecondaryDir;
  config.yAxis.dualMotor = kYDualMotor;
  config.yAxis.secondaryUsesSeparateDriver = kYSecondarySeparateDriver;
  config.yAxis.travelMm = static_cast<float>(CONFIG_LASERGRAVER_Y_TRAVEL_MM);

  config.laser.pwmFreqHz = CONFIG_LASERGRAVER_LASER_PWM_FREQ_HZ;
  config.laser.pwmResolutionBits = CONFIG_LASERGRAVER_LASER_PWM_RESOLUTION_BITS;
  config.laser.pwmMin = CONFIG_LASERGRAVER_LASER_PWM_MIN;
  config.laser.pwmMax = CONFIG_LASERGRAVER_LASER_PWM_MAX;
  config.laser.activeHigh = kLaserActiveHigh;
  config.laser.gateActiveHigh = kLaserGateActiveHigh;

  config.safety.homingEnabled = kHomingEnabled;
  config.safety.homeXEnabled = kHomeXEnabled;
  config.safety.homeYEnabled = kHomeYEnabled;
  config.safety.homeXToMin = kHomeXToMin;
  config.safety.homeYToMin = kHomeYToMin;
  config.safety.homingSeekFeedMmMin = kHomingSeekFeedMmMin;
  config.safety.homingLatchFeedMmMin = kHomingLatchFeedMmMin;
  config.safety.homingPullOffMm = kHomingPullOffMm;
  config.safety.homingTimeoutMs = kHomingTimeoutMs;
  config.safety.requireHomingBeforeRun = kRequireHomingBeforeRun;
  config.safety.stopLaserOnPause = kStopLaserOnPause;
  config.safety.stopLaserOnAlarm = kStopLaserOnAlarm;
  config.safety.limitInputsPullup = kLimitInputsPullup;
  config.safety.estopPullup = kEstopPullup;
  config.safety.lidInterlockPullup = kLidInterlockPullup;
  config.safety.xLimitActiveLow = kXLimitActiveLow;
  config.safety.yLimitActiveLow = kYLimitActiveLow;
  config.safety.estopActiveLow = kEstopActiveLow;
  config.safety.lidInterlockActiveLow = kLidInterlockActiveLow;

  config.network.hostname = CONFIG_LASERGRAVER_HOSTNAME;
  config.network.apSsid = CONFIG_LASERGRAVER_AP_SSID;
  config.network.apPassword = CONFIG_LASERGRAVER_AP_PASSWORD;
  config.network.staEnabled = kStaEnabled;
  config.network.staSsid = CONFIG_LASERGRAVER_STA_SSID;
  config.network.staPassword = CONFIG_LASERGRAVER_STA_PASSWORD;
  config.network.apChannel = static_cast<uint8_t>(CONFIG_LASERGRAVER_AP_CHANNEL);
  config.network.apMaxConnections = static_cast<uint8_t>(CONFIG_LASERGRAVER_AP_MAX_CONNECTIONS);

  return config;
}

}  // namespace shared
