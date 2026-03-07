#include "web/web_server.hpp"

#include "shared/types.hpp"
#include "web_internal.hpp"

#include "cJSON.h"
#include "esp_log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace web {

namespace {

constexpr const char *kTag = "web";
constexpr size_t kMaxJsonBodyBytes = 256 * 1024;
constexpr size_t kMaxRasterBodyBytes = 4 * 1024 * 1024;
constexpr uint32_t kApShutdownDelayMs = 15000;

}  // namespace

esp_err_t SendSimpleResponse(httpd_req_t *request, const int status_code, const char *body) {
  switch (status_code) {
    case 202:
      httpd_resp_set_status(request, "202 Accepted");
      break;
    case 400:
      httpd_resp_set_status(request, "400 Bad Request");
      break;
    case 404:
      httpd_resp_set_status(request, "404 Not Found");
      break;
    case 409:
      httpd_resp_set_status(request, "409 Conflict");
      break;
    case 413:
      httpd_resp_set_status(request, "413 Payload Too Large");
      break;
    default:
      httpd_resp_set_status(request, "200 OK");
      break;
  }

  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, body);
}

esp_err_t SendErrNameResponse(httpd_req_t *request, const int status_code, const char *error_code,
                              const esp_err_t err) {
  char body[160];
  std::snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\",\"errName\":\"%s\"}", error_code,
                esp_err_to_name(err));
  return SendSimpleResponse(request, status_code, body);
}

esp_err_t SendJsonResponse(httpd_req_t *request, cJSON *root) {
  char *body = cJSON_PrintUnformatted(root);
  if (body == nullptr) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  httpd_resp_set_status(request, "200 OK");
  httpd_resp_set_type(request, "application/json");
  const esp_err_t err = httpd_resp_sendstr(request, body);
  cJSON_free(body);
  cJSON_Delete(root);
  return err;
}

cJSON *BuildMachineConfigJson(const shared::MachineConfig &config) {
  cJSON *root = cJSON_CreateObject();
  cJSON *pins = cJSON_AddObjectToObject(root, "pins");
  cJSON *motion = cJSON_AddObjectToObject(root, "motion");
  cJSON *x_axis = cJSON_AddObjectToObject(root, "xAxis");
  cJSON *y_axis = cJSON_AddObjectToObject(root, "yAxis");
  cJSON *laser = cJSON_AddObjectToObject(root, "laser");
  cJSON *safety = cJSON_AddObjectToObject(root, "safety");
  cJSON *network = cJSON_AddObjectToObject(root, "network");

  if (root == nullptr || pins == nullptr || motion == nullptr || x_axis == nullptr || y_axis == nullptr ||
      laser == nullptr || safety == nullptr || network == nullptr) {
    if (root != nullptr) {
      cJSON_Delete(root);
    }
    return nullptr;
  }

  cJSON_AddNumberToObject(pins, "xStep", config.pins.xStep);
  cJSON_AddNumberToObject(pins, "xDir", config.pins.xDir);
  cJSON_AddNumberToObject(pins, "xSecondaryStep", config.pins.xSecondaryStep);
  cJSON_AddNumberToObject(pins, "xSecondaryDir", config.pins.xSecondaryDir);
  cJSON_AddNumberToObject(pins, "yStep", config.pins.yStep);
  cJSON_AddNumberToObject(pins, "yDir", config.pins.yDir);
  cJSON_AddNumberToObject(pins, "ySecondaryStep", config.pins.ySecondaryStep);
  cJSON_AddNumberToObject(pins, "ySecondaryDir", config.pins.ySecondaryDir);
  cJSON_AddNumberToObject(pins, "motorsEnable", config.pins.motorsEnable);
  cJSON_AddNumberToObject(pins, "laserPwm", config.pins.laserPwm);
  cJSON_AddNumberToObject(pins, "laserGate", config.pins.laserGate);
  cJSON_AddNumberToObject(pins, "xLimit", config.pins.xLimit);
  cJSON_AddNumberToObject(pins, "yLimit", config.pins.yLimit);
  cJSON_AddNumberToObject(pins, "estop", config.pins.estop);
  cJSON_AddNumberToObject(pins, "lidInterlock", config.pins.lidInterlock);

  cJSON_AddNumberToObject(motion, "stepPulseUs", config.motion.stepPulseUs);
  cJSON_AddNumberToObject(motion, "dirSetupUs", config.motion.dirSetupUs);
  cJSON_AddNumberToObject(motion, "dirHoldUs", config.motion.dirHoldUs);
  cJSON_AddBoolToObject(motion, "invertEnable", config.motion.invertEnable);

  cJSON_AddNumberToObject(x_axis, "stepsPerMm", config.xAxis.stepsPerMm);
  cJSON_AddNumberToObject(x_axis, "maxRateMmMin", config.xAxis.maxRateMmMin);
  cJSON_AddNumberToObject(x_axis, "accelerationMmS2", config.xAxis.accelerationMmS2);
  cJSON_AddBoolToObject(x_axis, "invertDirPrimary", config.xAxis.invertDirPrimary);
  cJSON_AddBoolToObject(x_axis, "invertDirSecondary", config.xAxis.invertDirSecondary);
  cJSON_AddBoolToObject(x_axis, "dualMotor", config.xAxis.dualMotor);
  cJSON_AddBoolToObject(x_axis, "secondaryUsesSeparateDriver", config.xAxis.secondaryUsesSeparateDriver);
  cJSON_AddNumberToObject(x_axis, "travelMm", config.xAxis.travelMm);

  cJSON_AddNumberToObject(y_axis, "stepsPerMm", config.yAxis.stepsPerMm);
  cJSON_AddNumberToObject(y_axis, "maxRateMmMin", config.yAxis.maxRateMmMin);
  cJSON_AddNumberToObject(y_axis, "accelerationMmS2", config.yAxis.accelerationMmS2);
  cJSON_AddBoolToObject(y_axis, "invertDirPrimary", config.yAxis.invertDirPrimary);
  cJSON_AddBoolToObject(y_axis, "invertDirSecondary", config.yAxis.invertDirSecondary);
  cJSON_AddBoolToObject(y_axis, "dualMotor", config.yAxis.dualMotor);
  cJSON_AddBoolToObject(y_axis, "secondaryUsesSeparateDriver", config.yAxis.secondaryUsesSeparateDriver);
  cJSON_AddNumberToObject(y_axis, "travelMm", config.yAxis.travelMm);

  cJSON_AddNumberToObject(laser, "pwmFreqHz", config.laser.pwmFreqHz);
  cJSON_AddNumberToObject(laser, "pwmResolutionBits", config.laser.pwmResolutionBits);
  cJSON_AddNumberToObject(laser, "pwmMin", config.laser.pwmMin);
  cJSON_AddNumberToObject(laser, "pwmMax", config.laser.pwmMax);
  cJSON_AddBoolToObject(laser, "activeHigh", config.laser.activeHigh);
  cJSON_AddBoolToObject(laser, "gateActiveHigh", config.laser.gateActiveHigh);

  cJSON_AddBoolToObject(safety, "homingEnabled", config.safety.homingEnabled);
  cJSON_AddBoolToObject(safety, "requireHomingBeforeRun", config.safety.requireHomingBeforeRun);
  cJSON_AddBoolToObject(safety, "stopLaserOnPause", config.safety.stopLaserOnPause);
  cJSON_AddBoolToObject(safety, "stopLaserOnAlarm", config.safety.stopLaserOnAlarm);
  cJSON_AddBoolToObject(safety, "limitInputsPullup", config.safety.limitInputsPullup);
  cJSON_AddBoolToObject(safety, "estopPullup", config.safety.estopPullup);
  cJSON_AddBoolToObject(safety, "lidInterlockPullup", config.safety.lidInterlockPullup);
  cJSON_AddBoolToObject(safety, "xLimitActiveLow", config.safety.xLimitActiveLow);
  cJSON_AddBoolToObject(safety, "yLimitActiveLow", config.safety.yLimitActiveLow);
  cJSON_AddBoolToObject(safety, "estopActiveLow", config.safety.estopActiveLow);
  cJSON_AddBoolToObject(safety, "lidInterlockActiveLow", config.safety.lidInterlockActiveLow);

  cJSON_AddStringToObject(network, "hostname", config.network.hostname.c_str());
  cJSON_AddStringToObject(network, "apSsid", config.network.apSsid.c_str());
  cJSON_AddStringToObject(network, "apPassword", config.network.apPassword.c_str());
  cJSON_AddBoolToObject(network, "staEnabled", config.network.staEnabled);
  cJSON_AddStringToObject(network, "staSsid", config.network.staSsid.c_str());
  cJSON_AddStringToObject(network, "staPassword", config.network.staPassword.c_str());
  cJSON_AddNumberToObject(network, "apChannel", config.network.apChannel);
  cJSON_AddNumberToObject(network, "apMaxConnections", config.network.apMaxConnections);

  return root;
}

esp_err_t ReceiveRequestBody(httpd_req_t *request, std::string &body, const size_t max_bytes) {
  if (request->content_len > static_cast<int>(max_bytes)) {
    return ESP_ERR_NO_MEM;
  }

  body.resize(static_cast<size_t>(request->content_len));
  int remaining = request->content_len;
  size_t offset = 0;
  while (remaining > 0) {
    const int received = httpd_req_recv(request, body.data() + offset, remaining);
    if (received <= 0) {
      return ESP_FAIL;
    }

    remaining -= received;
    offset += static_cast<size_t>(received);
  }

  return ESP_OK;
}

std::string GetHeaderValue(httpd_req_t *request, const char *header_name) {
  const size_t length = httpd_req_get_hdr_value_len(request, header_name);
  if (length == 0) {
    return {};
  }

  std::string value(length + 1, '\0');
  if (httpd_req_get_hdr_value_str(request, header_name, value.data(), value.size()) != ESP_OK) {
    return {};
  }

  value.resize(length);
  return value;
}

std::string GetQueryValue(httpd_req_t *request, const char *key) {
  const size_t length = httpd_req_get_url_query_len(request);
  if (length == 0) {
    return {};
  }

  std::string query(length + 1, '\0');
  if (httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK) {
    return {};
  }

  char value[64] = {};
  if (httpd_query_key_value(query.c_str(), key, value, sizeof(value)) != ESP_OK) {
    return {};
  }

  return value;
}

esp_err_t WebServer::HandleStatus(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  const shared::MachineStatus status = server->control_.status();
  const char *network_mode = server->network_.staConnected() ? "sta" : (server->network_.apActive() ? "ap" : "idle");
  const std::string sta_ip = server->network_.staIp();

  char body[512];
  std::snprintf(
      body, sizeof(body),
      "{\"state\":\"%s\",\"homed\":%s,\"laserArmed\":%s,\"motorsHeld\":%s,\"motionBusy\":%s,\"motionOp\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"activeJobId\":\"%s\",\"message\":\"%s\",\"jobRowsDone\":%u,\"jobRowsTotal\":%u,\"jobProgressPercent\":%u,\"networkMode\":\"%s\",\"staConnected\":%s,\"staIp\":\"%s\",\"apActive\":%s}",
      shared::ToString(status.state), status.homed ? "true" : "false", status.laserArmed ? "true" : "false",
      status.motorsHeld ? "true" : "false", status.motionBusy ? "true" : "false",
      shared::ToString(status.motionOperation),
      static_cast<double>(status.position.x), static_cast<double>(status.position.y), status.activeJobId.c_str(),
      status.message.c_str(), static_cast<unsigned>(status.jobRowsDone), static_cast<unsigned>(status.jobRowsTotal),
      static_cast<unsigned>(status.jobProgressPercent), network_mode,
      server->network_.staConnected() ? "true" : "false", sta_ip.c_str(), server->network_.apActive() ? "true" : "false");

  return SendSimpleResponse(request, 200, body);
}

esp_err_t WebServer::HandleMachineSettings(httpd_req_t *request) {
  const WebServer *server = FromRequest(request);
  cJSON *root = BuildMachineConfigJson(server->config_);
  if (root == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  return SendJsonResponse(request, root);
}

esp_err_t WebServer::HandleUpdateMachineSettings(httpd_req_t *request) {
  WebServer *server = FromRequest(request);

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, kMaxJsonBodyBytes);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  shared::MachineConfig updated_config = server->config_;
  const esp_err_t save_err = server->storage_.saveMachineConfig(body, updated_config);
  if (save_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-config\"}");
  }

  server->config_ = updated_config;
  return SendSimpleResponse(request, 202, "{\"ok\":true,\"restartRequired\":true}");
}

esp_err_t WebServer::HandleConnectWifi(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/network/connect");

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, kMaxJsonBodyBytes);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-json\"}");
  }

  const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
  const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
  if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-fields\"}");
  }

  shared::MachineConfig updated_config = server->config_;
  updated_config.network.staEnabled = true;
  updated_config.network.staSsid = ssid->valuestring;
  updated_config.network.staPassword = password->valuestring;

  cJSON *config_json_root = BuildMachineConfigJson(updated_config);
  if (config_json_root == nullptr) {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"config-encode-failed\"}");
  }

  char *config_json = cJSON_PrintUnformatted(config_json_root);
  cJSON_Delete(config_json_root);
  if (config_json == nullptr) {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"config-encode-failed\"}");
  }

  shared::MachineConfig saved_config = server->config_;
  const esp_err_t save_err = server->storage_.saveMachineConfig(config_json, saved_config);
  if (save_err != ESP_OK) {
    cJSON_free(config_json);
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"wifi-save-failed\"}");
  }

  server->config_ = saved_config;
  cJSON_free(config_json);

  std::string assigned_ip;
  const esp_err_t connect_err =
      server->network_.connectToSta(ssid->valuestring, password->valuestring, kApShutdownDelayMs, assigned_ip);
  if (connect_err != ESP_OK) {
    cJSON_Delete(root);
    return SendErrNameResponse(request, 409, "wifi-connect-failed", connect_err);
  }

  cJSON_Delete(root);
  char response[256];
  std::snprintf(response, sizeof(response),
                "{\"ok\":true,\"mode\":\"sta\",\"ip\":\"%s\",\"apShutdownDelayMs\":%u}",
                assigned_ip.c_str(), static_cast<unsigned>(kApShutdownDelayMs));
  return SendSimpleResponse(request, 200, response);
}

esp_err_t WebServer::HandleListJobs(httpd_req_t *request) {
  const WebServer *server = FromRequest(request);
  const std::vector<std::string> jobs = server->jobs_.listJobs();

  cJSON *root = cJSON_CreateObject();
  cJSON *items = cJSON_AddArrayToObject(root, "items");
  if (root == nullptr || items == nullptr) {
    if (root != nullptr) {
      cJSON_Delete(root);
    }
    return ESP_ERR_NO_MEM;
  }

  for (const std::string &job_id : jobs) {
    cJSON_AddItemToArray(items, cJSON_CreateString(job_id.c_str()));
  }

  return SendJsonResponse(request, root);
}

esp_err_t WebServer::HandleUploadJobManifest(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/jobs/manifest");

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, kMaxJsonBodyBytes);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-json\"}");
  }

  const cJSON *job_id = cJSON_GetObjectItemCaseSensitive(root, "jobId");
  const cJSON *manifest = cJSON_GetObjectItemCaseSensitive(root, "manifest");
  if (!cJSON_IsString(job_id) || !cJSON_IsObject(manifest)) {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-fields\"}");
  }

  const cJSON *job_type = cJSON_GetObjectItemCaseSensitive(manifest, "jobType");
  const bool is_raster_job = cJSON_IsString(job_type) && std::strcmp(job_type->valuestring, "raster") == 0;
  const char *job_type_value = cJSON_IsString(job_type) ? job_type->valuestring : "unknown";

  const std::string job_id_value = job_id->valuestring;
  ESP_LOGI(kTag, "Upload manifest: jobId=%s type=%s size=%u", job_id_value.c_str(), job_type_value,
           static_cast<unsigned>(body.size()));
  char *manifest_json = cJSON_PrintUnformatted(manifest);
  cJSON_Delete(root);
  if (manifest_json == nullptr) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"manifest-encode-failed\"}");
  }

  const esp_err_t save_err = server->jobs_.saveManifest(job_id_value, manifest_json);
  cJSON_free(manifest_json);
  if (save_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"manifest-save-failed\"}");
  }

  if (!is_raster_job) {
    const esp_err_t cleanup_err = server->storage_.deleteJobRaster(job_id_value);
    if (cleanup_err != ESP_OK) {
      return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"job-cleanup-failed\"}");
    }
  }

  return SendSimpleResponse(request, 202, "{\"ok\":true}");
}

esp_err_t WebServer::HandleUploadJobRaster(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/jobs/raster");
  if (request->content_len > static_cast<int>(kMaxRasterBodyBytes)) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }

  const std::string job_id = GetHeaderValue(request, "X-Job-Id");
  if (job_id.empty()) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-job-id\"}");
  }

  const std::string chunk_offset_raw = GetHeaderValue(request, "X-Chunk-Offset");
  const std::string chunk_total_raw = GetHeaderValue(request, "X-Chunk-Total");
  if (chunk_offset_raw.empty() || chunk_total_raw.empty()) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-chunk-headers\"}");
  }

  const size_t chunk_offset = static_cast<size_t>(std::strtoull(chunk_offset_raw.c_str(), nullptr, 10));
  const size_t chunk_total = static_cast<size_t>(std::strtoull(chunk_total_raw.c_str(), nullptr, 10));
  ESP_LOGI(kTag, "Upload raster chunk: jobId=%s offset=%u chunk=%u total=%u", job_id.c_str(),
           static_cast<unsigned>(chunk_offset), static_cast<unsigned>(request->content_len),
           static_cast<unsigned>(chunk_total));

  if (chunk_offset == 0) {
    const esp_err_t truncate_err = server->storage_.truncateJobRaster(job_id);
    if (truncate_err != ESP_OK) {
      return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"raster-save-failed\"}");
    }
  }

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, 64 * 1024);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  const esp_err_t append_err = server->storage_.appendJobRaster(job_id, body.data(), body.size());
  if (append_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"raster-save-failed\"}");
  }

  return SendSimpleResponse(request, 202, "{\"ok\":true}");
}

esp_err_t WebServer::HandleDiagJog(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/diag/jog");

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, kMaxJsonBodyBytes);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-json\"}");
  }

  const cJSON *axis = cJSON_GetObjectItemCaseSensitive(root, "axis");
  const cJSON *distance = cJSON_GetObjectItemCaseSensitive(root, "distanceMm");
  const cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feedMmMin");
  if (!cJSON_IsString(axis) || !cJSON_IsNumber(distance) || !cJSON_IsNumber(feed)) {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-fields\"}");
  }

  motion::AxisId axis_id;
  if (strcmp(axis->valuestring, "x") == 0 || strcmp(axis->valuestring, "X") == 0) {
    axis_id = motion::AxisId::X;
  } else if (strcmp(axis->valuestring, "y") == 0 || strcmp(axis->valuestring, "Y") == 0) {
    axis_id = motion::AxisId::Y;
  } else {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-axis\"}");
  }

  const esp_err_t jog_err =
      server->control_.jog(axis_id, static_cast<float>(distance->valuedouble), static_cast<float>(feed->valuedouble));
  cJSON_Delete(root);
  if (jog_err != ESP_OK) {
    return SendErrNameResponse(request, 409, "jog-failed", jog_err);
  }

  return SendSimpleResponse(request, 202, "{\"ok\":true}");
}

esp_err_t WebServer::HandleDiagJogButton(httpd_req_t *request) {
  WebServer *server = FromRequest(request);

  const std::string cmd = GetQueryValue(request, "cmd");
  const std::string distance_raw = GetQueryValue(request, "distanceMm");
  const std::string feed_raw = GetQueryValue(request, "feedMmMin");
  ESP_LOGI(kTag, "GET /api/diag/jog-button cmd=%s distance=%s feed=%s", cmd.c_str(), distance_raw.c_str(),
           feed_raw.c_str());
  if (cmd.empty() || distance_raw.empty() || feed_raw.empty()) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-query\"}");
  }

  motion::AxisId axis_id;
  float direction = 0.0f;
  if (cmd == "x+" || cmd == "x%2B" || cmd == "x ") {
    axis_id = motion::AxisId::X;
    direction = 1.0f;
  } else if (cmd == "x-") {
    axis_id = motion::AxisId::X;
    direction = -1.0f;
  } else if (cmd == "y+" || cmd == "y%2B" || cmd == "y ") {
    axis_id = motion::AxisId::Y;
    direction = 1.0f;
  } else if (cmd == "y-") {
    axis_id = motion::AxisId::Y;
    direction = -1.0f;
  } else {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-cmd\"}");
  }

  const float distance_mm = std::strtof(distance_raw.c_str(), nullptr) * direction;
  const float feed_mm_min = std::strtof(feed_raw.c_str(), nullptr);
  const esp_err_t jog_err = server->control_.jog(axis_id, distance_mm, feed_mm_min);
  if (jog_err != ESP_OK) {
    return SendErrNameResponse(request, 409, "jog-failed", jog_err);
  }

  return SendSimpleResponse(request, 200, "{\"ok\":true}");
}

esp_err_t WebServer::HandleDiagLaserPulse(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/diag/laser-pulse");

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, kMaxJsonBodyBytes);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-json\"}");
  }

  const cJSON *power = cJSON_GetObjectItemCaseSensitive(root, "power");
  const cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(root, "durationMs");
  if (!cJSON_IsNumber(power) || !cJSON_IsNumber(duration_ms)) {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-fields\"}");
  }

  const esp_err_t pulse_err =
      server->control_.laserPulse(static_cast<uint8_t>(power->valueint), static_cast<uint32_t>(duration_ms->valueint));
  cJSON_Delete(root);
  if (pulse_err != ESP_OK) {
    return SendErrNameResponse(request, 409, "laser-test-failed", pulse_err);
  }

  return SendSimpleResponse(request, 202, "{\"ok\":true}");
}

esp_err_t WebServer::HandleHome(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/home");
  const esp_err_t err = server->control_.home();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "home-failed", err);
}

esp_err_t WebServer::HandleGoToZero(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/go-to-zero");
  const esp_err_t err = server->control_.goToZero(1200.0f);
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "go-to-zero-failed", err);
}

esp_err_t WebServer::HandleMoveTo(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/move-to");

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, kMaxJsonBodyBytes);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-json\"}");
  }

  const cJSON *x = cJSON_GetObjectItemCaseSensitive(root, "xMm");
  const cJSON *y = cJSON_GetObjectItemCaseSensitive(root, "yMm");
  const cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feedMmMin");
  if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) || !cJSON_IsNumber(feed)) {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-fields\"}");
  }

  const esp_err_t err = server->control_.moveTo(static_cast<float>(x->valuedouble), static_cast<float>(y->valuedouble),
                                                static_cast<float>(feed->valuedouble));
  cJSON_Delete(root);
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "move-to-failed", err);
}

esp_err_t WebServer::HandleRunJob(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/jobs/run");

  std::string body;
  const esp_err_t recv_err = ReceiveRequestBody(request, body, kMaxJsonBodyBytes);
  if (recv_err == ESP_ERR_NO_MEM) {
    return SendSimpleResponse(request, 413, "{\"ok\":false,\"error\":\"payload-too-large\"}");
  }
  if (recv_err != ESP_OK) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-body\"}");
  }

  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) {
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"invalid-json\"}");
  }

  const cJSON *job_id = cJSON_GetObjectItemCaseSensitive(root, "jobId");
  if (!cJSON_IsString(job_id) || job_id->valuestring == nullptr || job_id->valuestring[0] == '\0') {
    cJSON_Delete(root);
    return SendSimpleResponse(request, 400, "{\"ok\":false,\"error\":\"missing-fields\"}");
  }

  const esp_err_t err = server->control_.runJob(job_id->valuestring);
  cJSON_Delete(root);
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "run-job-failed", err);
}

esp_err_t WebServer::HandlePauseJob(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/jobs/pause");
  const esp_err_t err = server->control_.pauseJob();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "pause-job-failed", err);
}

esp_err_t WebServer::HandleResumeJob(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/jobs/resume");
  const esp_err_t err = server->control_.resumeJob();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "resume-job-failed", err);
}

esp_err_t WebServer::HandleAbortJob(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/jobs/abort");
  const esp_err_t err = server->control_.abortJob();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "abort-job-failed", err);
}

esp_err_t WebServer::HandleFrame(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/frame");
  const esp_err_t err = server->control_.frame();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "frame-failed", err);
}

esp_err_t WebServer::HandleHoldMotors(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/motors/hold");
  const esp_err_t err = server->control_.holdMotors();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "hold-motors-failed", err);
}

esp_err_t WebServer::HandleReleaseMotors(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/motors/release");
  const esp_err_t err = server->control_.releaseMotors();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "release-motors-failed", err);
}

esp_err_t WebServer::HandleStop(httpd_req_t *request) {
  WebServer *server = FromRequest(request);
  ESP_LOGI(kTag, "POST /api/control/stop");
  const esp_err_t err = server->control_.stop();
  return err == ESP_OK ? SendSimpleResponse(request, 202, "{\"ok\":true}")
                       : SendErrNameResponse(request, 409, "stop-failed", err);
}

}  // namespace web
