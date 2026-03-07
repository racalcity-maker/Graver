#pragma once

#include "jobs/job_types.hpp"
#include "storage/storage_service.hpp"

#include "esp_err.h"

#include <string>
#include <vector>

namespace jobs {

class JobManager {
 public:
  explicit JobManager(storage::StorageService &storage);

  esp_err_t start();
  esp_err_t saveManifest(const std::string &jobId, const std::string &manifestJson);
  esp_err_t loadJob(const std::string &jobId, LoadedJob &job) const;
  esp_err_t loadRasterJob(const std::string &jobId, RasterJob &job) const;
  bool hasJob(const std::string &jobId) const;
  std::vector<std::string> listJobs() const;
  storage::StorageService &storage() const;

 private:
  storage::StorageService &storage_;
};

}  // namespace jobs
