#pragma once

#include "shared/machine_config.hpp"

#include "esp_err.h"
#include "esp_netif.h"

#include <string>

namespace network {

class NetworkService {
 public:
  explicit NetworkService(const shared::MachineConfig &config);

  esp_err_t start();
  esp_err_t connectToSta(const std::string &ssid, const std::string &password, uint32_t apShutdownDelayMs,
                         std::string &assignedIp);
  const std::string &staIp() const;
  bool apActive() const;
  bool staConnected() const;

 private:
  esp_err_t startStaWithFallback();
  esp_err_t startSoftAp();
  bool hasStaCredentials() const;
  esp_err_t ensureStaNetif();
  esp_err_t ensureApNetif();
  esp_err_t startStaOnly();
  esp_err_t configureSta(const std::string &ssid, const std::string &password);
  esp_err_t waitForStaResult(std::string &assignedIp);
  void scheduleApShutdown(uint32_t delayMs);
  void markApInactive();

  shared::MachineConfig config_;
  bool started_;
  esp_netif_t *apNetif_;
  esp_netif_t *staNetif_;
  bool apActive_;
  bool staConnected_;
  std::string staIp_;
};

}  // namespace network
