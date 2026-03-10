#include "grbl/grbl_service.hpp"

#include "esp_log.h"

#include "freertos/task.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>

namespace grbl {

namespace {

constexpr const char *kTag = "grbl";
constexpr uint32_t kDefaultTaskStack = 4096;
constexpr uint32_t kDefaultRxLineMax = 256;
constexpr float kMmPerInch = 25.4f;
constexpr TickType_t kIdleDelay = pdMS_TO_TICKS(10);

#if !defined(CONFIG_LASERGRAVER_GRBL_ENABLED)
#define CONFIG_LASERGRAVER_GRBL_ENABLED 0
#endif

#if !defined(CONFIG_LASERGRAVER_GRBL_TASK_STACK)
#define CONFIG_LASERGRAVER_GRBL_TASK_STACK 0
#endif

#if !defined(CONFIG_LASERGRAVER_GRBL_RX_LINE_MAX)
#define CONFIG_LASERGRAVER_GRBL_RX_LINE_MAX 0
#endif

float ConvertToMm(const float value, const bool inches) {
  return inches ? value * kMmPerInch : value;
}

}  // namespace

GrblService::GrblService(const shared::MachineConfig &config, control::ControlService &control)
    : config_(config),
      control_(control),
      task_(nullptr),
      started_(false),
      absoluteMode_(true),
      unitsInch_(false),
      feedRateMmMin_(600.0f),
      spindleEnabled_(false),
      spindlePower_(0U),
      warnedLaserDryRun_(false) {}

esp_err_t GrblService::start() {
  if (!CONFIG_LASERGRAVER_GRBL_ENABLED) {
    ESP_LOGI(kTag, "GRBL compatibility disabled in menuconfig");
    return ESP_OK;
  }
  if (started_) {
    return ESP_OK;
  }

  const uint32_t stack_size =
      CONFIG_LASERGRAVER_GRBL_TASK_STACK > 0 ? CONFIG_LASERGRAVER_GRBL_TASK_STACK : kDefaultTaskStack;
  const BaseType_t created = xTaskCreate(&GrblService::TaskEntry, "grbl_task", stack_size, this, 5, &task_);
  if (created != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  started_ = true;
  ESP_LOGI(kTag, "GRBL compatibility service started");
  return ESP_OK;
}

void GrblService::TaskEntry(void *ctx) {
  static_cast<GrblService *>(ctx)->taskLoop();
}

void GrblService::taskLoop() {
  sendBanner();

  std::string line;
  const size_t max_line = CONFIG_LASERGRAVER_GRBL_RX_LINE_MAX > 0
                              ? static_cast<size_t>(CONFIG_LASERGRAVER_GRBL_RX_LINE_MAX)
                              : static_cast<size_t>(kDefaultRxLineMax);
  line.reserve(max_line);

  for (;;) {
    const int ch = fgetc(stdin);
    if (ch == EOF) {
      vTaskDelay(kIdleDelay);
      continue;
    }

    const char c = static_cast<char>(ch);
    if (c == '?' || c == '!' || c == '~' || c == 0x18) {
      handleRealtimeByte(c);
      continue;
    }

    if (c == '\r' || c == '\n') {
      if (!line.empty()) {
        processLine(line);
        line.clear();
      }
      continue;
    }

    if (line.size() < max_line) {
      line.push_back(c);
    } else {
      line.clear();
      sendError(11);  // line overflow
    }
  }
}

void GrblService::handleRealtimeByte(const char c) {
  switch (c) {
    case '?':
      sendStatusReport();
      break;
    case '!':
      (void)control_.pauseJob();
      break;
    case '~':
      (void)control_.resumeJob();
      break;
    case 0x18:
      (void)control_.stop();
      (void)control_.clearAlarm();
      absoluteMode_ = true;
      unitsInch_ = false;
      spindleEnabled_ = false;
      spindlePower_ = 0U;
      warnedLaserDryRun_ = false;
      sendBanner();
      break;
    default:
      break;
  }
}

void GrblService::processLine(const std::string &line) {
  std::string processed;
  if (!preprocessLine(line, processed)) {
    sendError(1);
    return;
  }
  if (processed.empty()) {
    sendOk();
    return;
  }

  const esp_err_t err = executeGcode(processed);
  if (err == ESP_OK) {
    sendOk();
  } else {
    sendError(mapEspError(err));
  }
}

esp_err_t GrblService::executeGcode(const std::string &line) {
  if (!line.empty() && line[0] == '$') {
    if (line == "$$") {
      sendSettings();
      return ESP_OK;
    }
    if (line == "$I") {
      sendBuildInfo();
      return ESP_OK;
    }
    if (line == "$G") {
      sendParserState();
      return ESP_OK;
    }
    if (line == "$X") {
      return control_.clearAlarm();
    }
    if (line == "$H") {
      return control_.home();
    }
    return ESP_ERR_NOT_SUPPORTED;
  }

  Words words{};
  if (!parseWords(line, words)) {
    return ESP_ERR_INVALID_ARG;
  }

  if (words.hasF) {
    const float next_feed_mm_min = ConvertToMm(words.f, unitsInch_);
    if (next_feed_mm_min <= 0.0f) {
      return ESP_ERR_INVALID_ARG;
    }
    feedRateMmMin_ = next_feed_mm_min;
  }

  if (words.hasS) {
    const float clamped = std::clamp(words.s, 0.0f, static_cast<float>(config_.laser.pwmMax));
    spindlePower_ = static_cast<uint32_t>(std::lround(clamped));
  }

  if (words.hasM) {
    switch (words.mCode) {
      case 3:
      case 4:
        spindleEnabled_ = true;
        break;
      case 5:
        spindleEnabled_ = false;
        spindlePower_ = 0U;
        warnedLaserDryRun_ = false;
        break;
      default:
        return ESP_ERR_NOT_SUPPORTED;
    }
  }

  if (!words.hasG) {
    return ESP_OK;
  }

  switch (words.gCode) {
    case 0:
    case 1: {
      const shared::MachineStatus status = control_.status();
      float target_x = status.position.x;
      float target_y = status.position.y;

      if (words.hasX) {
        const float x_mm = ConvertToMm(words.x, unitsInch_);
        target_x = absoluteMode_ ? x_mm : (target_x + x_mm);
      }
      if (words.hasY) {
        const float y_mm = ConvertToMm(words.y, unitsInch_);
        target_y = absoluteMode_ ? y_mm : (target_y + y_mm);
      }
      if (!words.hasX && !words.hasY) {
        return ESP_OK;
      }

      float feed_mm_min = std::min(config_.xAxis.maxRateMmMin, config_.yAxis.maxRateMmMin);
      if (words.gCode == 1) {
        feed_mm_min = feedRateMmMin_;
      }
      if (feed_mm_min <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
      }

      if (words.gCode == 1 && spindleEnabled_ && spindlePower_ > 0U && !warnedLaserDryRun_) {
        sendLine("[MSG:laser sync for live G1 is not implemented yet; motion runs in dry mode]");
        warnedLaserDryRun_ = true;
      }
      return control_.moveTo(target_x, target_y, feed_mm_min);
    }
    case 4: {
      const float dwell_sec = words.hasP ? words.p : 0.0f;
      if (dwell_sec < 0.0f) {
        return ESP_ERR_INVALID_ARG;
      }
      const TickType_t ticks = pdMS_TO_TICKS(static_cast<uint32_t>(dwell_sec * 1000.0f));
      vTaskDelay(ticks);
      return ESP_OK;
    }
    case 20:
      unitsInch_ = true;
      return ESP_OK;
    case 21:
      unitsInch_ = false;
      return ESP_OK;
    case 90:
      absoluteMode_ = true;
      return ESP_OK;
    case 91:
      absoluteMode_ = false;
      return ESP_OK;
    case 92: {
      const float x = words.hasX ? words.x : 0.0f;
      const float y = words.hasY ? words.y : 0.0f;
      if (std::fabs(x) > 0.0001f || std::fabs(y) > 0.0001f) {
        return ESP_ERR_NOT_SUPPORTED;
      }
      return control_.setZero();
    }
    default:
      return ESP_ERR_NOT_SUPPORTED;
  }
}

bool GrblService::parseWords(const std::string &line, Words &words) const {
  words = {};

  size_t index = 0;
  while (index < line.size()) {
    const char letter = line[index];
    if (!std::isalpha(static_cast<unsigned char>(letter))) {
      return false;
    }
    ++index;
    const char *start = line.c_str() + index;
    char *end = nullptr;
    errno = 0;
    const float value = std::strtof(start, &end);
    if (start == end || errno == ERANGE) {
      return false;
    }
    index += static_cast<size_t>(end - start);

    switch (letter) {
      case 'G':
        words.hasG = true;
        words.gCode = static_cast<int>(std::lround(value));
        break;
      case 'M':
        words.hasM = true;
        words.mCode = static_cast<int>(std::lround(value));
        break;
      case 'X':
        words.hasX = true;
        words.x = value;
        break;
      case 'Y':
        words.hasY = true;
        words.y = value;
        break;
      case 'F':
        words.hasF = true;
        words.f = value;
        break;
      case 'S':
        words.hasS = true;
        words.s = value;
        break;
      case 'P':
        words.hasP = true;
        words.p = value;
        break;
      case 'N':
        break;  // line number, ignore
      default:
        return false;
    }
  }

  return true;
}

bool GrblService::preprocessLine(const std::string &raw, std::string &processed) const {
  processed.clear();
  processed.reserve(raw.size());

  bool in_parenthesis_comment = false;
  for (const char rc : raw) {
    if (rc == '%') {
      continue;
    }
    if (rc == '*') {
      break;
    }
    if (rc == ';') {
      break;
    }
    if (rc == '(') {
      in_parenthesis_comment = true;
      continue;
    }
    if (rc == ')') {
      in_parenthesis_comment = false;
      continue;
    }
    if (in_parenthesis_comment) {
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(rc))) {
      continue;
    }
    processed.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(rc))));
  }

  return !in_parenthesis_comment;
}

void GrblService::sendBanner() {
  sendLine("Grbl 1.1f ['$' for help]");
}

void GrblService::sendOk() {
  sendLine("ok");
}

void GrblService::sendError(const int code) {
  char buffer[32] = {0};
  std::snprintf(buffer, sizeof(buffer), "error:%d", code);
  sendLine(buffer);
}

void GrblService::sendLine(const std::string &line) {
  std::printf("%s\r\n", line.c_str());
  std::fflush(stdout);
}

void GrblService::sendStatusReport() {
  const shared::MachineStatus status = control_.status();
  const std::string state = mapState(status);
  const uint32_t feed = static_cast<uint32_t>(std::lround(feedRateMmMin_));
  const uint32_t spindle = spindleEnabled_ ? spindlePower_ : 0U;
  std::string pins;
  if (status.estopActive) {
    pins.push_back('E');
  }
  if (status.lidOpen) {
    pins.push_back('D');
  }
  if (status.limitActive) {
    pins.push_back('L');
  }

  char buffer[192] = {0};
  if (!pins.empty()) {
    std::snprintf(buffer, sizeof(buffer),
                  "<%s|MPos:%.3f,%.3f,0.000|WPos:%.3f,%.3f,0.000|FS:%" PRIu32 ",%" PRIu32 "|Pn:%s>",
                  state.c_str(), static_cast<double>(status.machinePosition.x),
                  static_cast<double>(status.machinePosition.y),
                  static_cast<double>(status.position.x), static_cast<double>(status.position.y), feed, spindle,
                  pins.c_str());
  } else {
    std::snprintf(buffer, sizeof(buffer),
                  "<%s|MPos:%.3f,%.3f,0.000|WPos:%.3f,%.3f,0.000|FS:%" PRIu32 ",%" PRIu32 ">",
                  state.c_str(), static_cast<double>(status.machinePosition.x),
                  static_cast<double>(status.machinePosition.y),
                  static_cast<double>(status.position.x), static_cast<double>(status.position.y), feed, spindle);
  }
  sendLine(buffer);
}

void GrblService::sendSettings() {
  char line[96] = {0};
  std::snprintf(line, sizeof(line), "$30=%" PRIu32, config_.laser.pwmMax);
  sendLine(line);
  std::snprintf(line, sizeof(line), "$31=%" PRIu32, config_.laser.pwmMin);
  sendLine(line);
  std::snprintf(line, sizeof(line), "$110=%.3f", static_cast<double>(config_.xAxis.maxRateMmMin));
  sendLine(line);
  std::snprintf(line, sizeof(line), "$111=%.3f", static_cast<double>(config_.yAxis.maxRateMmMin));
  sendLine(line);
  std::snprintf(line, sizeof(line), "$130=%.3f", static_cast<double>(config_.xAxis.travelMm));
  sendLine(line);
  std::snprintf(line, sizeof(line), "$131=%.3f", static_cast<double>(config_.yAxis.travelMm));
  sendLine(line);
}

void GrblService::sendBuildInfo() {
  sendLine("[VER:LaserGraverESP32,GRBL-Compat-0.1]");
  sendLine("[OPT:V,128,256]");
}

void GrblService::sendParserState() {
  char buffer[192] = {0};
  std::snprintf(buffer, sizeof(buffer), "[GC:G%d G%s G%s M%s F%.3f S%" PRIu32 "]",
                0,
                unitsInch_ ? "20" : "21",
                absoluteMode_ ? "90" : "91",
                spindleEnabled_ ? "3" : "5",
                static_cast<double>(feedRateMmMin_),
                spindleEnabled_ ? spindlePower_ : 0U);
  sendLine(buffer);
}

std::string GrblService::mapState(const shared::MachineStatus &status) const {
  switch (status.state) {
    case shared::MachineState::Running:
      return "Run";
    case shared::MachineState::Paused:
      return "Hold";
    case shared::MachineState::Homing:
      return "Home";
    case shared::MachineState::Alarm:
    case shared::MachineState::EstopLatched:
      return "Alarm";
    case shared::MachineState::Framing:
      return "Run";
    case shared::MachineState::Boot:
    case shared::MachineState::Idle:
    default:
      return "Idle";
  }
}

int GrblService::mapEspError(const esp_err_t err) const {
  switch (err) {
    case ESP_ERR_INVALID_STATE:
      return 22;
    case ESP_ERR_NOT_SUPPORTED:
      return 20;
    case ESP_ERR_TIMEOUT:
      return 24;
    case ESP_ERR_INVALID_ARG:
      return 2;
    default:
      return 1;
  }
}

}  // namespace grbl
