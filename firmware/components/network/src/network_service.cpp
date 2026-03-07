#include "network/network_service.hpp"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace network {

namespace {

constexpr const char *kTag = "network";
constexpr int kStaMaxAttempts = 3;
constexpr TickType_t kStaAttemptTimeout = pdMS_TO_TICKS(15000);
constexpr int kConnectedBit = BIT0;
constexpr int kFailedBit = BIT1;

EventGroupHandle_t g_wifiEventGroup = nullptr;
int g_staAttempts = 0;

struct ApShutdownContext {
  NetworkService *service;
  uint32_t delayMs;
};

void CopyStringToBuffer(const std::string &source, uint8_t *destination, const size_t destinationSize) {
  if (destinationSize == 0) {
    return;
  }

  std::memset(destination, 0, destinationSize);
  const size_t source_length = std::min(source.size(), destinationSize - 1);
  std::memcpy(destination, source.data(), source_length);
}

wifi_auth_mode_t ResolveAuthMode(const std::string &password) {
  return password.size() >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
}

void WifiEventHandler(void *, esp_event_base_t event_base, int32_t event_id, void *) {
  if (g_wifiEventGroup == nullptr) {
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    g_staAttempts = 0;
    esp_wifi_connect();
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_staAttempts < kStaMaxAttempts) {
      ++g_staAttempts;
      esp_wifi_connect();
    } else {
      xEventGroupSetBits(g_wifiEventGroup, kFailedBit);
    }
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(g_wifiEventGroup, kConnectedBit);
  }
}

}  // namespace

NetworkService::NetworkService(const shared::MachineConfig &config)
    : config_(config),
      started_(false),
      apNetif_(nullptr),
      staNetif_(nullptr),
      apActive_(false),
      staConnected_(false),
      staIp_() {}

esp_err_t NetworkService::start() {
  if (started_) {
    return ESP_OK;
  }

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_config), kTag, "Failed to initialize Wi-Fi");
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), kTag, "Failed to set Wi-Fi storage");

  esp_err_t err = ESP_OK;
  if (config_.network.staEnabled && hasStaCredentials()) {
    err = startStaWithFallback();
  } else {
    err = startSoftAp();
  }

  if (err == ESP_OK) {
    started_ = true;
  }
  return err;
}

bool NetworkService::hasStaCredentials() const {
  return !config_.network.staSsid.empty();
}

esp_err_t NetworkService::startStaWithFallback() {
  ESP_RETURN_ON_ERROR(ensureStaNetif(), kTag, "Failed to ensure STA netif");
  ESP_RETURN_ON_ERROR(startStaOnly(), kTag, "Failed to start STA");

  std::string assigned_ip;
  const esp_err_t sta_err = waitForStaResult(assigned_ip);
  if (sta_err == ESP_OK) {
    staConnected_ = true;
    staIp_ = assigned_ip;
    ESP_LOGI(kTag, "STA connected: ssid=%s ip=%s", config_.network.staSsid.c_str(), staIp_.c_str());
    return ESP_OK;
  }

  ESP_LOGW(kTag, "STA connection failed or timed out, starting SoftAP fallback");
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_NULL));
  return startSoftAp();
}

esp_err_t NetworkService::startSoftAp() {
  ESP_RETURN_ON_ERROR(ensureApNetif(), kTag, "Failed to ensure AP netif");

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), kTag, "Failed to set Wi-Fi mode");

  wifi_config_t ap_config{};
  CopyStringToBuffer(config_.network.apSsid, ap_config.ap.ssid, sizeof(ap_config.ap.ssid));
  CopyStringToBuffer(config_.network.apPassword, ap_config.ap.password, sizeof(ap_config.ap.password));
  ap_config.ap.ssid_len = static_cast<uint8_t>(std::min(config_.network.apSsid.size(), sizeof(ap_config.ap.ssid) - 1));
  ap_config.ap.channel = config_.network.apChannel;
  ap_config.ap.max_connection = config_.network.apMaxConnections;
  ap_config.ap.authmode = ResolveAuthMode(config_.network.apPassword);
  ap_config.ap.pmf_cfg.required = false;
  ap_config.ap.pmf_cfg.capable = true;

  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), kTag, "Failed to set AP config");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "Failed to start Wi-Fi");

  apActive_ = true;
  ESP_LOGI(kTag, "SoftAP started: ssid=%s hostname=%s channel=%u max_conn=%u", config_.network.apSsid.c_str(),
           config_.network.hostname.c_str(), static_cast<unsigned>(config_.network.apChannel),
           static_cast<unsigned>(config_.network.apMaxConnections));
  return ESP_OK;
}

esp_err_t NetworkService::connectToSta(const std::string &ssid, const std::string &password, const uint32_t apShutdownDelayMs,
                                       std::string &assignedIp) {
  if (!started_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (ssid.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  config_.network.staEnabled = true;
  config_.network.staSsid = ssid;
  config_.network.staPassword = password;

  ESP_RETURN_ON_ERROR(ensureStaNetif(), kTag, "Failed to ensure STA netif");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(apActive_ ? WIFI_MODE_APSTA : WIFI_MODE_STA), kTag, "Failed to set APSTA/STA");
  ESP_RETURN_ON_ERROR(configureSta(ssid, password), kTag, "Failed to configure STA");
  ESP_RETURN_ON_ERROR(esp_wifi_connect(), kTag, "Failed to start STA connect");

  ESP_RETURN_ON_ERROR(waitForStaResult(assignedIp), kTag, "Failed to connect STA");
  staConnected_ = true;
  staIp_ = assignedIp;
  ESP_LOGI(kTag, "STA connected from UI: ssid=%s ip=%s", ssid.c_str(), staIp_.c_str());

  if (apActive_ && apShutdownDelayMs > 0U) {
    scheduleApShutdown(apShutdownDelayMs);
  }

  return ESP_OK;
}

const std::string &NetworkService::staIp() const {
  return staIp_;
}

bool NetworkService::apActive() const {
  return apActive_;
}

bool NetworkService::staConnected() const {
  return staConnected_;
}

esp_err_t NetworkService::ensureStaNetif() {
  if (staNetif_ == nullptr) {
    staNetif_ = esp_netif_create_default_wifi_sta();
    if (staNetif_ == nullptr) {
      return ESP_FAIL;
    }
    if (!config_.network.hostname.empty()) {
      ESP_RETURN_ON_ERROR(esp_netif_set_hostname(staNetif_, config_.network.hostname.c_str()), kTag,
                          "Failed to set STA hostname");
    }
  }

  if (g_wifiEventGroup == nullptr) {
    g_wifiEventGroup = xEventGroupCreate();
    if (g_wifiEventGroup == nullptr) {
      return ESP_ERR_NO_MEM;
    }
  }

  ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, nullptr), kTag,
                      "Failed to register WIFI handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiEventHandler, nullptr), kTag,
                      "Failed to register IP handler");
  return ESP_OK;
}

esp_err_t NetworkService::ensureApNetif() {
  if (apNetif_ == nullptr) {
    apNetif_ = esp_netif_create_default_wifi_ap();
    if (apNetif_ == nullptr) {
      return ESP_FAIL;
    }
    if (!config_.network.hostname.empty()) {
      ESP_RETURN_ON_ERROR(esp_netif_set_hostname(apNetif_, config_.network.hostname.c_str()), kTag,
                          "Failed to set AP hostname");
    }
  }
  return ESP_OK;
}

esp_err_t NetworkService::startStaOnly() {
  staConnected_ = false;
  staIp_.clear();
  xEventGroupClearBits(g_wifiEventGroup, kConnectedBit | kFailedBit);
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "Failed to set STA mode");
  ESP_RETURN_ON_ERROR(configureSta(config_.network.staSsid, config_.network.staPassword), kTag, "Failed to configure STA");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "Failed to start STA");
  ESP_LOGI(kTag, "Connecting to STA ssid=%s with up to %d attempts", config_.network.staSsid.c_str(), kStaMaxAttempts);
  return ESP_OK;
}

esp_err_t NetworkService::configureSta(const std::string &ssid, const std::string &password) {
  xEventGroupClearBits(g_wifiEventGroup, kConnectedBit | kFailedBit);
  g_staAttempts = 0;

  wifi_config_t sta_config{};
  CopyStringToBuffer(ssid, sta_config.sta.ssid, sizeof(sta_config.sta.ssid));
  CopyStringToBuffer(password, sta_config.sta.password, sizeof(sta_config.sta.password));
  sta_config.sta.threshold.authmode = ResolveAuthMode(password);
  sta_config.sta.pmf_cfg.capable = true;
  sta_config.sta.pmf_cfg.required = false;
  return esp_wifi_set_config(WIFI_IF_STA, &sta_config);
}

esp_err_t NetworkService::waitForStaResult(std::string &assignedIp) {
  const EventBits_t bits =
      xEventGroupWaitBits(g_wifiEventGroup, kConnectedBit | kFailedBit, pdTRUE, pdFALSE, kStaAttemptTimeout);

  esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiEventHandler);
  esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler);

  if ((bits & kConnectedBit) == 0) {
    staConnected_ = false;
    staIp_.clear();
    return ESP_FAIL;
  }

  esp_netif_ip_info_t ip_info{};
  if (staNetif_ == nullptr || esp_netif_get_ip_info(staNetif_, &ip_info) != ESP_OK) {
    return ESP_FAIL;
  }

  char ip_buffer[16] = {};
  std::snprintf(ip_buffer, sizeof(ip_buffer), IPSTR, IP2STR(&ip_info.ip));
  assignedIp = ip_buffer;
  return ESP_OK;
}

void NetworkService::scheduleApShutdown(const uint32_t delayMs) {
  struct ShutdownTask {
    static void Run(void *context) {
      std::unique_ptr<ApShutdownContext> ctx(static_cast<ApShutdownContext *>(context));
      vTaskDelay(pdMS_TO_TICKS(ctx->delayMs));
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
      if (ctx->service != nullptr) {
        ctx->service->markApInactive();
      }
      vTaskDelete(nullptr);
    }
  };

  auto *ctx = new ApShutdownContext{this, delayMs};
  if (xTaskCreate(&ShutdownTask::Run, "wifi_ap_shutdown", 3072, ctx, 4, nullptr) != pdPASS) {
    delete ctx;
  }
}

void NetworkService::markApInactive() {
  apActive_ = false;
}

}  // namespace network
