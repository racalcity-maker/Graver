#pragma once

#include <string>

namespace shared {

enum class MachineState {
  Boot,
  Idle,
  Homing,
  Framing,
  Running,
  Paused,
  Alarm,
  EstopLatched,
};

enum class MotionOperation {
  None,
  Home,
  Frame,
  Jog,
  GoToZero,
  MoveTo,
};

struct PositionMm {
  float x;
  float y;
};

struct MachineStatus {
  MachineState state;
  bool homed;
  bool estopActive;
  bool lidOpen;
  bool limitActive;
  bool laserArmed;
  bool motorsHeld;
  bool motionBusy;
  MotionOperation motionOperation;
  PositionMm position;
  PositionMm machinePosition;
  PositionMm workOffset;
  std::string activeJobId;
  std::string message;
  uint32_t jobRowsDone;
  uint32_t jobRowsTotal;
  uint8_t jobProgressPercent;
};

const char *ToString(MachineState state);
const char *ToString(MotionOperation operation);

}  // namespace shared
