#include "shared/types.hpp"

namespace shared {

const char *ToString(const MachineState state) {
  switch (state) {
    case MachineState::Boot:
      return "boot";
    case MachineState::Idle:
      return "idle";
    case MachineState::Homing:
      return "homing";
    case MachineState::Framing:
      return "framing";
    case MachineState::Running:
      return "running";
    case MachineState::Paused:
      return "paused";
    case MachineState::Alarm:
      return "alarm";
    default:
      return "unknown";
  }
}

const char *ToString(const MotionOperation operation) {
  switch (operation) {
    case MotionOperation::None:
      return "none";
    case MotionOperation::Home:
      return "home";
    case MotionOperation::Frame:
      return "frame";
    case MotionOperation::Jog:
      return "jog";
    case MotionOperation::GoToZero:
      return "go_to_zero";
    case MotionOperation::MoveTo:
      return "move_to";
    default:
      return "unknown";
  }
}

}  // namespace shared
