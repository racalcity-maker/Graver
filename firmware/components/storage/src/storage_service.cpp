#include "storage/storage_service.hpp"

#include "cJSON.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace storage {

namespace {

constexpr const char *kTag = "storage";
constexpr const char *kBasePath = "/spiffs";
constexpr const char *kPartitionLabel = "storage";
constexpr const char *kMachineConfigRelativePath = "/config/machine.json";
constexpr const char *kConfigDirPath = "/spiffs/config";
constexpr const char *kJobsDirPath = "/spiffs/jobs";
constexpr const char *kRootDirPath = "/spiffs";

bool IsSafeJobId(const std::string &jobId) {
  if (jobId.empty()) {
    return false;
  }

  for (const char ch : jobId) {
    const bool is_alpha_num = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    const bool is_safe_punct = ch == '-' || ch == '_';
    if (!is_alpha_num && !is_safe_punct) {
      return false;
    }
  }

  return true;
}

esp_err_t LoadFile(const std::string &path, std::string &content) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
  }

  std::fseek(file, 0, SEEK_END);
  const long file_size = std::ftell(file);
  std::rewind(file);

  if (file_size < 0) {
    std::fclose(file);
    return ESP_FAIL;
  }

  content.resize(static_cast<size_t>(file_size));
  if (file_size > 0) {
    const size_t bytes_read = std::fread(content.data(), 1, content.size(), file);
    if (bytes_read != content.size()) {
      std::fclose(file);
      return ESP_FAIL;
    }
  }

  std::fclose(file);
  return ESP_OK;
}

esp_err_t LoadFileSlice(const std::string &path, const size_t offset, const size_t length, std::string &content) {
  FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
  }

  if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
    std::fclose(file);
    return ESP_FAIL;
  }

  content.resize(length);
  if (length > 0) {
    const size_t bytes_read = std::fread(content.data(), 1, length, file);
    if (bytes_read != length) {
      std::fclose(file);
      return ESP_FAIL;
    }
  }

  std::fclose(file);
  return ESP_OK;
}

esp_err_t SaveFile(const std::string &path, const std::string &content) {
  FILE *file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return ESP_FAIL;
  }

  if (!content.empty()) {
    const size_t written = std::fwrite(content.data(), 1, content.size(), file);
    if (written != content.size()) {
      std::fclose(file);
      return ESP_FAIL;
    }
  }

  std::fclose(file);
  return ESP_OK;
}

esp_err_t SaveFileChunk(const std::string &path, const char *data, const size_t size, const bool append) {
  const int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  const int fd = ::open(path.c_str(), flags, 0666);
  if (fd < 0) {
    return ESP_FAIL;
  }

  if (size > 0 && data != nullptr) {
    const ssize_t written = ::write(fd, data, size);
    if (written < 0 || static_cast<size_t>(written) != size) {
      ::close(fd);
      return ESP_FAIL;
    }
  }

  ::close(fd);
  return ESP_OK;
}

esp_err_t DeleteFile(const std::string &path) {
  if (::unlink(path.c_str()) == 0) {
    return ESP_OK;
  }
  return errno == ENOENT ? ESP_OK : ESP_FAIL;
}

const cJSON *GetRequiredObject(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsObject(item) ? item : nullptr;
}

const cJSON *GetRequiredNumber(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsNumber(item) ? item : nullptr;
}

const cJSON *GetRequiredBool(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsBool(item) ? item : nullptr;
}

const cJSON *GetRequiredString(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsString(item) ? item : nullptr;
}

const cJSON *GetOptionalString(const cJSON *parent, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  return cJSON_IsString(item) ? item : nullptr;
}

esp_err_t ParsePins(const cJSON *root, shared::MachinePins &pins) {
  const cJSON *section = GetRequiredObject(root, "pins");
  if (section == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *x_step = GetRequiredNumber(section, "xStep");
  const cJSON *x_dir = GetRequiredNumber(section, "xDir");
  const cJSON *x_secondary_step = GetRequiredNumber(section, "xSecondaryStep");
  const cJSON *x_secondary_dir = GetRequiredNumber(section, "xSecondaryDir");
  const cJSON *y_step = GetRequiredNumber(section, "yStep");
  const cJSON *y_dir = GetRequiredNumber(section, "yDir");
  const cJSON *y_secondary_step = GetRequiredNumber(section, "ySecondaryStep");
  const cJSON *y_secondary_dir = GetRequiredNumber(section, "ySecondaryDir");
  const cJSON *motors_enable = GetRequiredNumber(section, "motorsEnable");
  const cJSON *laser_pwm = GetRequiredNumber(section, "laserPwm");
  const cJSON *laser_gate = GetRequiredNumber(section, "laserGate");
  const cJSON *x_limit = GetRequiredNumber(section, "xLimit");
  const cJSON *y_limit = GetRequiredNumber(section, "yLimit");
  const cJSON *estop = GetRequiredNumber(section, "estop");
  const cJSON *lid_interlock = GetRequiredNumber(section, "lidInterlock");

  if (x_step == nullptr || x_dir == nullptr || x_secondary_step == nullptr || x_secondary_dir == nullptr ||
      y_step == nullptr || y_dir == nullptr || y_secondary_step == nullptr || y_secondary_dir == nullptr ||
      motors_enable == nullptr || laser_pwm == nullptr ||
      laser_gate == nullptr || x_limit == nullptr || y_limit == nullptr || estop == nullptr ||
      lid_interlock == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  pins.xStep = x_step->valueint;
  pins.xDir = x_dir->valueint;
  pins.xSecondaryStep = x_secondary_step->valueint;
  pins.xSecondaryDir = x_secondary_dir->valueint;
  pins.yStep = y_step->valueint;
  pins.yDir = y_dir->valueint;
  pins.ySecondaryStep = y_secondary_step->valueint;
  pins.ySecondaryDir = y_secondary_dir->valueint;
  pins.motorsEnable = motors_enable->valueint;
  pins.laserPwm = laser_pwm->valueint;
  pins.laserGate = laser_gate->valueint;
  pins.xLimit = x_limit->valueint;
  pins.yLimit = y_limit->valueint;
  pins.estop = estop->valueint;
  pins.lidInterlock = lid_interlock->valueint;
  return ESP_OK;
}

esp_err_t ParseMotion(const cJSON *root, shared::MotionTimingConfig &motion) {
  const cJSON *section = GetRequiredObject(root, "motion");
  if (section == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *step_pulse_us = GetRequiredNumber(section, "stepPulseUs");
  const cJSON *dir_setup_us = GetRequiredNumber(section, "dirSetupUs");
  const cJSON *dir_hold_us = GetRequiredNumber(section, "dirHoldUs");
  const cJSON *invert_enable = GetRequiredBool(section, "invertEnable");

  if (step_pulse_us == nullptr || dir_setup_us == nullptr || dir_hold_us == nullptr || invert_enable == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  motion.stepPulseUs = static_cast<uint32_t>(step_pulse_us->valuedouble);
  motion.dirSetupUs = static_cast<uint32_t>(dir_setup_us->valuedouble);
  motion.dirHoldUs = static_cast<uint32_t>(dir_hold_us->valuedouble);
  motion.invertEnable = cJSON_IsTrue(invert_enable);
  return ESP_OK;
}

esp_err_t ParseAxis(const cJSON *root, const char *sectionName, shared::AxisConfig &axis) {
  const cJSON *section = GetRequiredObject(root, sectionName);
  if (section == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *steps_per_mm = GetRequiredNumber(section, "stepsPerMm");
  const cJSON *max_rate = GetRequiredNumber(section, "maxRateMmMin");
  const cJSON *acceleration = GetRequiredNumber(section, "accelerationMmS2");
  const cJSON *invert_primary = GetRequiredBool(section, "invertDirPrimary");
  const cJSON *invert_secondary = GetRequiredBool(section, "invertDirSecondary");
  const cJSON *dual_motor = GetRequiredBool(section, "dualMotor");
  const cJSON *secondary_separate_driver = GetRequiredBool(section, "secondaryUsesSeparateDriver");
  const cJSON *travel_mm = GetRequiredNumber(section, "travelMm");

  if (steps_per_mm == nullptr || max_rate == nullptr || acceleration == nullptr || invert_primary == nullptr ||
      invert_secondary == nullptr || dual_motor == nullptr || secondary_separate_driver == nullptr ||
      travel_mm == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  axis.stepsPerMm = static_cast<float>(steps_per_mm->valuedouble);
  axis.maxRateMmMin = static_cast<float>(max_rate->valuedouble);
  axis.accelerationMmS2 = static_cast<float>(acceleration->valuedouble);
  axis.invertDirPrimary = cJSON_IsTrue(invert_primary);
  axis.invertDirSecondary = cJSON_IsTrue(invert_secondary);
  axis.dualMotor = cJSON_IsTrue(dual_motor);
  axis.secondaryUsesSeparateDriver = cJSON_IsTrue(secondary_separate_driver);
  axis.travelMm = static_cast<float>(travel_mm->valuedouble);
  return ESP_OK;
}

esp_err_t ParseLaser(const cJSON *root, shared::LaserConfig &laser) {
  const cJSON *section = GetRequiredObject(root, "laser");
  if (section == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *pwm_freq_hz = GetRequiredNumber(section, "pwmFreqHz");
  const cJSON *pwm_resolution_bits = GetRequiredNumber(section, "pwmResolutionBits");
  const cJSON *pwm_min = GetRequiredNumber(section, "pwmMin");
  const cJSON *pwm_max = GetRequiredNumber(section, "pwmMax");
  const cJSON *active_high = GetRequiredBool(section, "activeHigh");
  const cJSON *gate_active_high = GetRequiredBool(section, "gateActiveHigh");

  if (pwm_freq_hz == nullptr || pwm_resolution_bits == nullptr || pwm_min == nullptr || pwm_max == nullptr ||
      active_high == nullptr || gate_active_high == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  laser.pwmFreqHz = static_cast<uint32_t>(pwm_freq_hz->valuedouble);
  laser.pwmResolutionBits = static_cast<uint32_t>(pwm_resolution_bits->valuedouble);
  laser.pwmMin = static_cast<uint32_t>(pwm_min->valuedouble);
  laser.pwmMax = static_cast<uint32_t>(pwm_max->valuedouble);
  laser.activeHigh = cJSON_IsTrue(active_high);
  laser.gateActiveHigh = cJSON_IsTrue(gate_active_high);
  return ESP_OK;
}

esp_err_t ParseSafety(const cJSON *root, shared::SafetyConfig &safety) {
  const cJSON *section = GetRequiredObject(root, "safety");
  if (section == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *homing_enabled = cJSON_GetObjectItemCaseSensitive(section, "homingEnabled");
  const cJSON *home_x_enabled = cJSON_GetObjectItemCaseSensitive(section, "homeXEnabled");
  const cJSON *home_y_enabled = cJSON_GetObjectItemCaseSensitive(section, "homeYEnabled");
  const cJSON *home_x_to_min = cJSON_GetObjectItemCaseSensitive(section, "homeXToMin");
  const cJSON *home_y_to_min = cJSON_GetObjectItemCaseSensitive(section, "homeYToMin");
  const cJSON *homing_seek_feed = cJSON_GetObjectItemCaseSensitive(section, "homingSeekFeedMmMin");
  const cJSON *homing_latch_feed = cJSON_GetObjectItemCaseSensitive(section, "homingLatchFeedMmMin");
  const cJSON *homing_pull_off = cJSON_GetObjectItemCaseSensitive(section, "homingPullOffMm");
  const cJSON *homing_timeout_ms = cJSON_GetObjectItemCaseSensitive(section, "homingTimeoutMs");
  const cJSON *require_homing = GetRequiredBool(section, "requireHomingBeforeRun");
  const cJSON *stop_laser_on_pause = GetRequiredBool(section, "stopLaserOnPause");
  const cJSON *stop_laser_on_alarm = GetRequiredBool(section, "stopLaserOnAlarm");
  const cJSON *limit_inputs_pullup = GetRequiredBool(section, "limitInputsPullup");
  const cJSON *estop_pullup = GetRequiredBool(section, "estopPullup");
  const cJSON *lid_interlock_pullup = GetRequiredBool(section, "lidInterlockPullup");
  const cJSON *x_limit_active_low = GetRequiredBool(section, "xLimitActiveLow");
  const cJSON *y_limit_active_low = GetRequiredBool(section, "yLimitActiveLow");
  const cJSON *estop_active_low = GetRequiredBool(section, "estopActiveLow");
  const cJSON *lid_interlock_active_low = GetRequiredBool(section, "lidInterlockActiveLow");

  if (require_homing == nullptr || stop_laser_on_pause == nullptr || stop_laser_on_alarm == nullptr ||
      limit_inputs_pullup == nullptr || estop_pullup == nullptr || lid_interlock_pullup == nullptr ||
      x_limit_active_low == nullptr || y_limit_active_low == nullptr || estop_active_low == nullptr ||
      lid_interlock_active_low == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  safety.homingEnabled = cJSON_IsBool(homing_enabled) ? cJSON_IsTrue(homing_enabled) : false;
  if (cJSON_IsBool(home_x_enabled)) {
    safety.homeXEnabled = cJSON_IsTrue(home_x_enabled);
  }
  if (cJSON_IsBool(home_y_enabled)) {
    safety.homeYEnabled = cJSON_IsTrue(home_y_enabled);
  }
  if (cJSON_IsBool(home_x_to_min)) {
    safety.homeXToMin = cJSON_IsTrue(home_x_to_min);
  }
  if (cJSON_IsBool(home_y_to_min)) {
    safety.homeYToMin = cJSON_IsTrue(home_y_to_min);
  }
  if (cJSON_IsNumber(homing_seek_feed) && homing_seek_feed->valuedouble > 0.0) {
    safety.homingSeekFeedMmMin = static_cast<float>(homing_seek_feed->valuedouble);
  }
  if (cJSON_IsNumber(homing_latch_feed) && homing_latch_feed->valuedouble > 0.0) {
    safety.homingLatchFeedMmMin = static_cast<float>(homing_latch_feed->valuedouble);
  }
  if (cJSON_IsNumber(homing_pull_off) && homing_pull_off->valuedouble > 0.0) {
    safety.homingPullOffMm = static_cast<float>(homing_pull_off->valuedouble);
  }
  if (cJSON_IsNumber(homing_timeout_ms) && homing_timeout_ms->valuedouble > 0.0) {
    safety.homingTimeoutMs = static_cast<uint32_t>(homing_timeout_ms->valuedouble);
  }
  safety.requireHomingBeforeRun = cJSON_IsTrue(require_homing);
  safety.stopLaserOnPause = cJSON_IsTrue(stop_laser_on_pause);
  safety.stopLaserOnAlarm = cJSON_IsTrue(stop_laser_on_alarm);
  safety.limitInputsPullup = cJSON_IsTrue(limit_inputs_pullup);
  safety.estopPullup = cJSON_IsTrue(estop_pullup);
  safety.lidInterlockPullup = cJSON_IsTrue(lid_interlock_pullup);
  safety.xLimitActiveLow = cJSON_IsTrue(x_limit_active_low);
  safety.yLimitActiveLow = cJSON_IsTrue(y_limit_active_low);
  safety.estopActiveLow = cJSON_IsTrue(estop_active_low);
  safety.lidInterlockActiveLow = cJSON_IsTrue(lid_interlock_active_low);
  return ESP_OK;
}

esp_err_t ParseNetwork(const cJSON *root, shared::NetworkConfig &network) {
  const cJSON *section = GetRequiredObject(root, "network");
  if (section == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const cJSON *hostname = GetRequiredString(section, "hostname");
  const cJSON *ap_ssid = GetRequiredString(section, "apSsid");
  const cJSON *ap_password = GetRequiredString(section, "apPassword");
  const cJSON *sta_enabled = GetRequiredBool(section, "staEnabled");
  const cJSON *sta_ssid = GetOptionalString(section, "staSsid");
  const cJSON *sta_password = GetOptionalString(section, "staPassword");
  const cJSON *ap_channel = GetRequiredNumber(section, "apChannel");
  const cJSON *ap_max_connections = GetRequiredNumber(section, "apMaxConnections");

  if (hostname == nullptr || ap_ssid == nullptr || ap_password == nullptr || sta_enabled == nullptr ||
      ap_channel == nullptr || ap_max_connections == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  network.hostname = hostname->valuestring;
  network.apSsid = ap_ssid->valuestring;
  network.apPassword = ap_password->valuestring;
  network.staEnabled = cJSON_IsTrue(sta_enabled);
  network.staSsid = sta_ssid != nullptr ? sta_ssid->valuestring : "";
  network.staPassword = sta_password != nullptr ? sta_password->valuestring : "";
  network.apChannel = static_cast<uint8_t>(ap_channel->valueint);
  network.apMaxConnections = static_cast<uint8_t>(ap_max_connections->valueint);
  return ESP_OK;
}

esp_err_t ParseMachineConfig(const std::string &json, shared::MachineConfig &config) {
  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (root == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = ParsePins(root, config.pins);
  if (err == ESP_OK) {
    err = ParseMotion(root, config.motion);
  }
  if (err == ESP_OK) {
    err = ParseAxis(root, "xAxis", config.xAxis);
  }
  if (err == ESP_OK) {
    err = ParseAxis(root, "yAxis", config.yAxis);
  }
  if (err == ESP_OK) {
    err = ParseLaser(root, config.laser);
  }
  if (err == ESP_OK) {
    err = ParseSafety(root, config.safety);
  }
  if (err == ESP_OK) {
    err = ParseNetwork(root, config.network);
  }

  cJSON_Delete(root);
  return err;
}

}  // namespace

StorageService::StorageService() : started_(false) {}

esp_err_t StorageService::start() {
  esp_vfs_spiffs_conf_t config{};
  config.base_path = kBasePath;
  config.partition_label = kPartitionLabel;
  config.max_files = 8;
  config.format_if_mount_failed = true;

  const esp_err_t err = esp_vfs_spiffs_register(&config);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  started_ = true;
  ESP_LOGI(kTag, "Storage service started at %s (SPIFFS flat namespace)", kBasePath);
  return ESP_OK;
}

esp_err_t StorageService::loadMachineConfig(shared::MachineConfig &config, bool &loadedFromStorage) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  std::string content;
  const std::string path = machineConfigPath();
  const esp_err_t read_err = LoadFile(path, content);
  if (read_err == ESP_ERR_NOT_FOUND) {
    loadedFromStorage = false;
    ESP_LOGW(kTag, "Machine config not found at %s, using defaults", path.c_str());
    return ESP_OK;
  }
  if (read_err != ESP_OK) {
    return read_err;
  }

  ESP_RETURN_ON_ERROR(ParseMachineConfig(content, config), kTag, "Failed to parse machine config");
  loadedFromStorage = true;
  ESP_LOGI(kTag, "Loaded machine config from %s", path.c_str());
  return ESP_OK;
}

esp_err_t StorageService::saveMachineConfig(const std::string &json, shared::MachineConfig &config) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }

  shared::MachineConfig parsed = config;
  ESP_RETURN_ON_ERROR(ParseMachineConfig(json, parsed), kTag, "Machine config validation failed");
  ESP_RETURN_ON_ERROR(SaveFile(machineConfigPath(), json), kTag, "Failed to save machine config");
  config = parsed;
  return ESP_OK;
}

esp_err_t StorageService::saveJobManifest(const std::string &jobId, const std::string &json) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (root == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  cJSON_Delete(root);

  return SaveFile(jobManifestPath(jobId), json);
}

esp_err_t StorageService::saveJobRaster(const std::string &jobId, const std::string &data) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  return SaveFile(jobRasterPath(jobId), data);
}

esp_err_t StorageService::truncateJobRaster(const std::string &jobId) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  return SaveFileChunk(jobRasterPath(jobId), nullptr, 0, false);
}

esp_err_t StorageService::deleteJobRaster(const std::string &jobId) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  return DeleteFile(jobRasterPath(jobId));
}

esp_err_t StorageService::appendJobRaster(const std::string &jobId, const char *data, const size_t size) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  return SaveFileChunk(jobRasterPath(jobId), data, size, true);
}

esp_err_t StorageService::loadJobManifest(const std::string &jobId, std::string &json) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  return LoadFile(jobManifestPath(jobId), json);
}

esp_err_t StorageService::loadJobRaster(const std::string &jobId, std::string &data) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  return LoadFile(jobRasterPath(jobId), data);
}

esp_err_t StorageService::loadJobRasterRow(const std::string &jobId, const uint32_t rowIndex, const uint32_t rowWidthBytes,
                                           std::string &data) const {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!IsSafeJobId(jobId)) {
    return ESP_ERR_INVALID_ARG;
  }

  const size_t offset = static_cast<size_t>(rowIndex) * static_cast<size_t>(rowWidthBytes);
  return LoadFileSlice(jobRasterPath(jobId), offset, rowWidthBytes, data);
}

std::vector<std::string> StorageService::listJobIds() const {
  std::vector<std::string> job_ids;
  if (!started_) {
    return job_ids;
  }

  DIR *dir = opendir(kRootDirPath);
  if (dir == nullptr) {
    return job_ids;
  }

  while (dirent *entry = readdir(dir)) {
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    const std::string name = entry->d_name;
    constexpr const char *kManifestSuffix = "/manifest.json";
    const std::string prefix = "jobs/";
    if (name.rfind(prefix, 0) != 0) {
      continue;
    }
    if (name.size() <= prefix.size() + std::strlen(kManifestSuffix)) {
      continue;
    }
    if (name.compare(name.size() - std::strlen(kManifestSuffix), std::strlen(kManifestSuffix), kManifestSuffix) != 0) {
      continue;
    }

    const std::string job_id =
        name.substr(prefix.size(), name.size() - prefix.size() - std::strlen(kManifestSuffix));
    if (IsSafeJobId(job_id)) {
      job_ids.push_back(job_id);
    }
  }

  closedir(dir);
  return job_ids;
}

bool StorageService::jobExists(const std::string &jobId) const {
  if (!started_ || !IsSafeJobId(jobId)) {
    return false;
  }

  struct stat info {};
  return stat(jobManifestPath(jobId).c_str(), &info) == 0;
}

std::string StorageService::machineConfigPath() const {
  return std::string(kBasePath) + kMachineConfigRelativePath;
}

std::string StorageService::jobManifestPath(const std::string &jobId) const {
  return std::string(kJobsDirPath) + "/" + jobId + "/manifest.json";
}

std::string StorageService::jobRasterPath(const std::string &jobId) const {
  return std::string(kJobsDirPath) + "/" + jobId + "/raster.bin";
}

}  // namespace storage
