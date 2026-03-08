#pragma once

#include "control/control_service.hpp"
#include "shared/machine_config.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string>

namespace grbl {

class GrblService {
 public:
  GrblService(const shared::MachineConfig &config, control::ControlService &control);

  esp_err_t start();

 private:
  struct Words {
    bool hasG;
    int gCode;
    bool hasM;
    int mCode;
    bool hasX;
    float x;
    bool hasY;
    float y;
    bool hasF;
    float f;
    bool hasS;
    float s;
    bool hasP;
    float p;
  };

  static void TaskEntry(void *ctx);
  void taskLoop();

  void handleRealtimeByte(char c);
  void processLine(const std::string &line);
  esp_err_t executeGcode(const std::string &line);
  bool parseWords(const std::string &line, Words &words) const;
  bool preprocessLine(const std::string &raw, std::string &processed) const;

  void sendBanner();
  void sendOk();
  void sendError(int code);
  void sendLine(const std::string &line);
  void sendStatusReport();
  void sendSettings();
  void sendBuildInfo();
  void sendParserState();
  std::string mapState(const shared::MachineStatus &status) const;
  int mapEspError(esp_err_t err) const;

  shared::MachineConfig config_;
  control::ControlService &control_;
  TaskHandle_t task_;
  bool started_;
  bool absoluteMode_;
  bool unitsInch_;
  float feedRateMmMin_;
  bool spindleEnabled_;
  uint32_t spindlePower_;
  bool warnedLaserDryRun_;
};

}  // namespace grbl

