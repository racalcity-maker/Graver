#include "web/web_server.hpp"

#include "esp_check.h"
#include "esp_log.h"

namespace web {

namespace {

constexpr const char *kTag = "web";
WebServer *gServerInstance = nullptr;

}  // namespace

WebServer::WebServer(control::ControlService &control, jobs::JobManager &jobs, storage::StorageService &storage,
                     network::NetworkService &network, shared::MachineConfig &config)
    : control_(control),
      jobs_(jobs),
      storage_(storage),
      network_(network),
      config_(config),
      server_(nullptr) {}

esp_err_t WebServer::start() {
  if (server_ != nullptr) {
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 24;
  config.stack_size = 10240;
  config.keep_alive_enable = false;

  ESP_RETURN_ON_ERROR(httpd_start(&server_, &config), kTag, "Failed to start HTTP server");
  gServerInstance = this;
  ESP_RETURN_ON_ERROR(registerRoutes(), kTag, "Failed to register routes");

  ESP_LOGI(kTag, "Web server started");
  return ESP_OK;
}

esp_err_t WebServer::stop() {
  if (server_ == nullptr) {
    return ESP_OK;
  }

  const esp_err_t err = httpd_stop(server_);
  server_ = nullptr;
  if (gServerInstance == this) {
    gServerInstance = nullptr;
  }
  return err;
}

esp_err_t WebServer::registerRoutes() {
  httpd_uri_t index_route{};
  index_route.uri = "/";
  index_route.method = HTTP_GET;
  index_route.handler = &WebServer::HandleIndex;
  index_route.user_ctx = this;

  httpd_uri_t favicon_route{};
  favicon_route.uri = "/favicon.ico";
  favicon_route.method = HTTP_GET;
  favicon_route.handler = &WebServer::HandleFavicon;
  favicon_route.user_ctx = this;

  httpd_uri_t asset_route{};
  asset_route.uri = "/assets/*";
  asset_route.method = HTTP_GET;
  asset_route.handler = &WebServer::HandleAsset;
  asset_route.user_ctx = this;

  httpd_uri_t status_route{};
  status_route.uri = "/api/status";
  status_route.method = HTTP_GET;
  status_route.handler = &WebServer::HandleStatus;
  status_route.user_ctx = this;

  httpd_uri_t home_route{};
  home_route.uri = "/api/control/home";
  home_route.method = HTTP_POST;
  home_route.handler = &WebServer::HandleHome;
  home_route.user_ctx = this;

  httpd_uri_t go_to_zero_route{};
  go_to_zero_route.uri = "/api/control/go-to-zero";
  go_to_zero_route.method = HTTP_POST;
  go_to_zero_route.handler = &WebServer::HandleGoToZero;
  go_to_zero_route.user_ctx = this;

  httpd_uri_t move_to_route{};
  move_to_route.uri = "/api/control/move-to";
  move_to_route.method = HTTP_POST;
  move_to_route.handler = &WebServer::HandleMoveTo;
  move_to_route.user_ctx = this;

  httpd_uri_t hold_motors_route{};
  hold_motors_route.uri = "/api/control/motors/hold";
  hold_motors_route.method = HTTP_POST;
  hold_motors_route.handler = &WebServer::HandleHoldMotors;
  hold_motors_route.user_ctx = this;

  httpd_uri_t release_motors_route{};
  release_motors_route.uri = "/api/control/motors/release";
  release_motors_route.method = HTTP_POST;
  release_motors_route.handler = &WebServer::HandleReleaseMotors;
  release_motors_route.user_ctx = this;

  httpd_uri_t machine_settings_route{};
  machine_settings_route.uri = "/api/settings/machine";
  machine_settings_route.method = HTTP_GET;
  machine_settings_route.handler = &WebServer::HandleMachineSettings;
  machine_settings_route.user_ctx = this;

  httpd_uri_t machine_settings_update_route{};
  machine_settings_update_route.uri = "/api/settings/machine";
  machine_settings_update_route.method = HTTP_POST;
  machine_settings_update_route.handler = &WebServer::HandleUpdateMachineSettings;
  machine_settings_update_route.user_ctx = this;

  httpd_uri_t connect_wifi_route{};
  connect_wifi_route.uri = "/api/network/connect";
  connect_wifi_route.method = HTTP_POST;
  connect_wifi_route.handler = &WebServer::HandleConnectWifi;
  connect_wifi_route.user_ctx = this;

  httpd_uri_t jobs_route{};
  jobs_route.uri = "/api/jobs";
  jobs_route.method = HTTP_GET;
  jobs_route.handler = &WebServer::HandleListJobs;
  jobs_route.user_ctx = this;

  httpd_uri_t jobs_manifest_route{};
  jobs_manifest_route.uri = "/api/jobs/manifest";
  jobs_manifest_route.method = HTTP_POST;
  jobs_manifest_route.handler = &WebServer::HandleUploadJobManifest;
  jobs_manifest_route.user_ctx = this;

  httpd_uri_t jobs_raster_route{};
  jobs_raster_route.uri = "/api/jobs/raster";
  jobs_raster_route.method = HTTP_POST;
  jobs_raster_route.handler = &WebServer::HandleUploadJobRaster;
  jobs_raster_route.user_ctx = this;

  httpd_uri_t jobs_run_route{};
  jobs_run_route.uri = "/api/control/jobs/run";
  jobs_run_route.method = HTTP_POST;
  jobs_run_route.handler = &WebServer::HandleRunJob;
  jobs_run_route.user_ctx = this;

  httpd_uri_t jobs_pause_route{};
  jobs_pause_route.uri = "/api/control/jobs/pause";
  jobs_pause_route.method = HTTP_POST;
  jobs_pause_route.handler = &WebServer::HandlePauseJob;
  jobs_pause_route.user_ctx = this;

  httpd_uri_t jobs_resume_route{};
  jobs_resume_route.uri = "/api/control/jobs/resume";
  jobs_resume_route.method = HTTP_POST;
  jobs_resume_route.handler = &WebServer::HandleResumeJob;
  jobs_resume_route.user_ctx = this;

  httpd_uri_t jobs_abort_route{};
  jobs_abort_route.uri = "/api/control/jobs/abort";
  jobs_abort_route.method = HTTP_POST;
  jobs_abort_route.handler = &WebServer::HandleAbortJob;
  jobs_abort_route.user_ctx = this;

  httpd_uri_t diag_jog_route{};
  diag_jog_route.uri = "/api/diag/jog";
  diag_jog_route.method = HTTP_POST;
  diag_jog_route.handler = &WebServer::HandleDiagJog;
  diag_jog_route.user_ctx = this;

  httpd_uri_t diag_jog_button_route{};
  diag_jog_button_route.uri = "/api/diag/jog-button";
  diag_jog_button_route.method = HTTP_GET;
  diag_jog_button_route.handler = &WebServer::HandleDiagJogButton;
  diag_jog_button_route.user_ctx = this;

  httpd_uri_t diag_laser_route{};
  diag_laser_route.uri = "/api/diag/laser-pulse";
  diag_laser_route.method = HTTP_POST;
  diag_laser_route.handler = &WebServer::HandleDiagLaserPulse;
  diag_laser_route.user_ctx = this;

  httpd_uri_t frame_route{};
  frame_route.uri = "/api/control/frame";
  frame_route.method = HTTP_POST;
  frame_route.handler = &WebServer::HandleFrame;
  frame_route.user_ctx = this;

  httpd_uri_t stop_route{};
  stop_route.uri = "/api/control/stop";
  stop_route.method = HTTP_POST;
  stop_route.handler = &WebServer::HandleStop;
  stop_route.user_ctx = this;

  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &index_route), kTag, "Index route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &asset_route), kTag, "Asset route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &favicon_route), kTag, "Favicon route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &status_route), kTag, "Status route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &home_route), kTag, "Home route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &go_to_zero_route), kTag, "Go-to-zero route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &move_to_route), kTag, "Move-to route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &hold_motors_route), kTag, "Hold motors route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &release_motors_route), kTag,
                      "Release motors route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &machine_settings_route), kTag,
                      "Machine settings route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &machine_settings_update_route), kTag,
                      "Machine settings update route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &connect_wifi_route), kTag,
                      "Connect Wi-Fi route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &jobs_route), kTag, "Jobs route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &jobs_manifest_route), kTag, "Jobs manifest route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &jobs_raster_route), kTag, "Jobs raster route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &jobs_run_route), kTag, "Jobs run route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &jobs_pause_route), kTag, "Jobs pause route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &jobs_resume_route), kTag, "Jobs resume route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &jobs_abort_route), kTag, "Jobs abort route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &diag_jog_route), kTag, "Diag jog route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &diag_jog_button_route), kTag,
                      "Diag jog button route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &diag_laser_route), kTag, "Diag laser route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &frame_route), kTag, "Frame route failed");
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server_, &stop_route), kTag, "Stop route failed");
  return ESP_OK;
}

WebServer *WebServer::FromRequest(httpd_req_t *request) {
  (void)request;
  return gServerInstance;
}

}  // namespace web
