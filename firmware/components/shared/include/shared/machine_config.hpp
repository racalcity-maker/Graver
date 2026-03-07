#pragma once

#include <cstdint>
#include <string>

namespace shared {

struct MachinePins {
  int xStep;
  int xDir;
  int xSecondaryStep;
  int xSecondaryDir;
  int yStep;
  int yDir;
  int ySecondaryStep;
  int ySecondaryDir;
  int motorsEnable;
  int laserPwm;
  int laserGate;
  int xLimit;
  int yLimit;
  int estop;
  int lidInterlock;
};

struct MotionTimingConfig {
  uint32_t stepPulseUs;
  uint32_t dirSetupUs;
  uint32_t dirHoldUs;
  bool invertEnable;
};

struct AxisConfig {
  float stepsPerMm;
  float maxRateMmMin;
  float accelerationMmS2;
  bool invertDirPrimary;
  bool invertDirSecondary;
  bool dualMotor;
  bool secondaryUsesSeparateDriver;
  float travelMm;
};

struct LaserConfig {
  uint32_t pwmFreqHz;
  uint32_t pwmResolutionBits;
  uint32_t pwmMin;
  uint32_t pwmMax;
  bool activeHigh;
  bool gateActiveHigh;
};

struct SafetyConfig {
  bool homingEnabled;
  bool requireHomingBeforeRun;
  bool stopLaserOnPause;
  bool stopLaserOnAlarm;
  bool limitInputsPullup;
  bool estopPullup;
  bool lidInterlockPullup;
  bool xLimitActiveLow;
  bool yLimitActiveLow;
  bool estopActiveLow;
  bool lidInterlockActiveLow;
};

struct NetworkConfig {
  std::string hostname;
  std::string apSsid;
  std::string apPassword;
  bool staEnabled;
  std::string staSsid;
  std::string staPassword;
  uint8_t apChannel;
  uint8_t apMaxConnections;
};

struct MachineConfig {
  MachinePins pins;
  MotionTimingConfig motion;
  AxisConfig xAxis;
  AxisConfig yAxis;
  LaserConfig laser;
  SafetyConfig safety;
  NetworkConfig network;
};

}  // namespace shared
