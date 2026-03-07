#pragma once

#include "shared/machine_config.hpp"

#include "esp_err.h"

#include <string>
#include <vector>

namespace storage {

class StorageService {
 public:
  StorageService();

  esp_err_t start();
  esp_err_t loadMachineConfig(shared::MachineConfig &config, bool &loadedFromStorage) const;
  esp_err_t saveMachineConfig(const std::string &json, shared::MachineConfig &config) const;
  esp_err_t saveJobManifest(const std::string &jobId, const std::string &json) const;
  esp_err_t saveJobRaster(const std::string &jobId, const std::string &data) const;
  esp_err_t truncateJobRaster(const std::string &jobId) const;
  esp_err_t deleteJobRaster(const std::string &jobId) const;
  esp_err_t appendJobRaster(const std::string &jobId, const char *data, size_t size) const;
  esp_err_t loadJobRasterRow(const std::string &jobId, uint32_t rowIndex, uint32_t rowWidthBytes, std::string &data) const;
  esp_err_t loadJobManifest(const std::string &jobId, std::string &json) const;
  esp_err_t loadJobRaster(const std::string &jobId, std::string &data) const;
  std::vector<std::string> listJobIds() const;
  bool jobExists(const std::string &jobId) const;
  std::string machineConfigPath() const;
  std::string jobManifestPath(const std::string &jobId) const;
  std::string jobRasterPath(const std::string &jobId) const;

 private:
  bool started_;
};

}  // namespace storage
