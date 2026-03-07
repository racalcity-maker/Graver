#pragma once

#include "control/control_service.hpp"
#include "jobs/job_manager.hpp"
#include "network/network_service.hpp"
#include "shared/machine_config.hpp"
#include "storage/storage_service.hpp"

#include "esp_err.h"
#include "esp_http_server.h"

namespace web {

class WebServer {
 public:
 WebServer(control::ControlService &control, jobs::JobManager &jobs, storage::StorageService &storage,
            network::NetworkService &network, shared::MachineConfig &config);

  esp_err_t start();
  esp_err_t stop();

 private:
  static esp_err_t HandleIndex(httpd_req_t *request);
  static esp_err_t HandleAsset(httpd_req_t *request);
  static esp_err_t HandleFavicon(httpd_req_t *request);
  static esp_err_t HandleStatus(httpd_req_t *request);
  static esp_err_t HandleMachineSettings(httpd_req_t *request);
  static esp_err_t HandleUpdateMachineSettings(httpd_req_t *request);
  static esp_err_t HandleConnectWifi(httpd_req_t *request);
  static esp_err_t HandleListJobs(httpd_req_t *request);
  static esp_err_t HandleUploadJobManifest(httpd_req_t *request);
  static esp_err_t HandleUploadJobRaster(httpd_req_t *request);
  static esp_err_t HandleRunJob(httpd_req_t *request);
  static esp_err_t HandlePauseJob(httpd_req_t *request);
  static esp_err_t HandleResumeJob(httpd_req_t *request);
  static esp_err_t HandleAbortJob(httpd_req_t *request);
  static esp_err_t HandleDiagJog(httpd_req_t *request);
  static esp_err_t HandleDiagJogButton(httpd_req_t *request);
  static esp_err_t HandleDiagLaserPulse(httpd_req_t *request);
  static esp_err_t HandleHome(httpd_req_t *request);
  static esp_err_t HandleGoToZero(httpd_req_t *request);
  static esp_err_t HandleMoveTo(httpd_req_t *request);
  static esp_err_t HandleHoldMotors(httpd_req_t *request);
  static esp_err_t HandleReleaseMotors(httpd_req_t *request);
  static esp_err_t HandleFrame(httpd_req_t *request);
  static esp_err_t HandleStop(httpd_req_t *request);

  esp_err_t registerRoutes();
  static WebServer *FromRequest(httpd_req_t *request);

  control::ControlService &control_;
  jobs::JobManager &jobs_;
  storage::StorageService &storage_;
  network::NetworkService &network_;
  shared::MachineConfig &config_;
  httpd_handle_t server_;
};

}  // namespace web
