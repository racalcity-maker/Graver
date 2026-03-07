#include "jobs/job_manager.hpp"

#include "cJSON.h"
#include "esp_log.h"

#include <utility>

namespace jobs {

namespace {

constexpr const char *kTag = "jobs";

std::string DetectJobType(const std::string &manifestJson) {
  cJSON *root = cJSON_ParseWithLength(manifestJson.c_str(), manifestJson.size());
  if (root == nullptr) {
    return {};
  }

  const cJSON *job_type = cJSON_GetObjectItemCaseSensitive(root, "jobType");
  const std::string type = cJSON_IsString(job_type) ? job_type->valuestring : "";
  cJSON_Delete(root);
  return type;
}

}  // namespace

JobManager::JobManager(storage::StorageService &storage) : storage_(storage) {}

esp_err_t JobManager::start() {
  (void)storage_;
  ESP_LOGI(kTag, "Job manager started");
  return ESP_OK;
}

bool JobManager::hasJob(const std::string &jobId) const {
  return storage_.jobExists(jobId);
}

esp_err_t JobManager::saveManifest(const std::string &jobId, const std::string &manifestJson) {
  return storage_.saveJobManifest(jobId, manifestJson);
}

esp_err_t JobManager::loadJob(const std::string &jobId, LoadedJob &job) const {
  std::string manifest_json;
  esp_err_t err = storage_.loadJobManifest(jobId, manifest_json);
  if (err != ESP_OK) {
    return err;
  }

  const std::string job_type = DetectJobType(manifest_json);
  if (job_type.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(kTag, "Load job id=%s type=%s", jobId.c_str(), job_type.c_str());

  if (job_type == "raster") {
    job.type = JobType::Raster;
    return loadRasterJob(jobId, job.rasterJob);
  }

  if (job_type == "vector-primitive") {
    VectorPrimitiveManifest manifest{};
    err = ParseVectorPrimitiveJobManifest(jobId, manifest_json, manifest);
    if (err != ESP_OK) {
      return err;
    }
    job.type = JobType::VectorPrimitive;
    job.vectorPrimitiveJob.manifest = std::move(manifest);
    return ESP_OK;
  }

  if (job_type == "vector-text") {
    VectorTextManifest manifest{};
    err = ParseVectorTextJobManifest(jobId, manifest_json, manifest);
    if (err != ESP_OK) {
      return err;
    }
    job.type = JobType::VectorText;
    job.vectorTextJob.manifest = std::move(manifest);
    return ESP_OK;
  }

  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t JobManager::loadRasterJob(const std::string &jobId, RasterJob &job) const {
  std::string manifest_json;
  esp_err_t err = storage_.loadJobManifest(jobId, manifest_json);
  if (err != ESP_OK) {
    return err;
  }

  RasterJobManifest manifest{};
  err = ParseRasterJobManifest(jobId, manifest_json, manifest);
  if (err != ESP_OK) {
    return err;
  }

  job.jobId = jobId;
  job.manifest = manifest;
  job.rasterData.clear();
  return ESP_OK;
}

std::vector<std::string> JobManager::listJobs() const {
  return storage_.listJobIds();
}

storage::StorageService &JobManager::storage() const {
  return storage_;
}

}  // namespace jobs
