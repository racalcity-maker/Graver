#include "app/application.hpp"

#include "shared/default_machine_config.hpp"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

namespace app {

namespace {

constexpr const char *kTag = "app";

esp_err_t InitializeNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }

  return err;
}

}  // namespace

Application::Application()
    : config_(shared::BuildDefaultMachineConfig()),
      boardHal_(),
      storage_(),
      network_(nullptr),
      jobs_(nullptr),
      motion_(nullptr),
      laser_(nullptr),
      control_(nullptr),
      web_(nullptr) {}

esp_err_t Application::start() {
  ESP_RETURN_ON_ERROR(initializeSystemServices(), kTag, "Failed to initialize core services");
  ESP_RETURN_ON_ERROR(storage_.start(), kTag, "Failed to start storage");
  ESP_RETURN_ON_ERROR(loadMachineConfig(), kTag, "Failed to load machine config");
  ESP_RETURN_ON_ERROR(boardHal_.initialize(config_), kTag, "Failed to initialize board HAL");

  network_ = std::make_unique<network::NetworkService>(config_);
  jobs_ = std::make_unique<jobs::JobManager>(storage_);
  motion_ = std::make_unique<motion::MotionService>(config_, boardHal_);
  laser_ = std::make_unique<laser::LaserService>(config_, boardHal_);
  control_ = std::make_unique<control::ControlService>(config_, *motion_, *laser_, *jobs_);
  web_ = std::make_unique<web::WebServer>(*control_, *jobs_, storage_, *network_, config_);

  ESP_RETURN_ON_ERROR(network_->start(), kTag, "Failed to start network");
  ESP_RETURN_ON_ERROR(jobs_->start(), kTag, "Failed to start job manager");
  ESP_RETURN_ON_ERROR(motion_->start(), kTag, "Failed to start motion");
  ESP_RETURN_ON_ERROR(laser_->start(), kTag, "Failed to start laser");
  ESP_RETURN_ON_ERROR(control_->start(), kTag, "Failed to start control service");
  ESP_RETURN_ON_ERROR(web_->start(), kTag, "Failed to start web server");

  ESP_LOGI(kTag, "Application started");
  return ESP_OK;
}

esp_err_t Application::initializeSystemServices() {
  ESP_RETURN_ON_ERROR(InitializeNvs(), kTag, "Failed to initialize NVS");
  ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "Failed to initialize esp_netif");

  const esp_err_t event_loop_err = esp_event_loop_create_default();
  if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_INVALID_STATE) {
    return event_loop_err;
  }

  return ESP_OK;
}

esp_err_t Application::loadMachineConfig() {
  bool loaded_from_storage = false;
  const esp_err_t err = storage_.loadMachineConfig(config_, loaded_from_storage);
  if (err != ESP_OK) {
    config_ = shared::BuildDefaultMachineConfig();
    ESP_LOGW(kTag, "Machine config load failed (%s), using compiled defaults", esp_err_to_name(err));
    return ESP_OK;
  }

  if (loaded_from_storage) {
    ESP_LOGI(kTag, "Machine config source: storage");
  } else {
    ESP_LOGI(kTag, "Machine config source: compiled defaults");
  }

  return ESP_OK;
}

}  // namespace app
