#include "app/application.hpp"

#include "esp_err.h"
#include "esp_log.h"

extern "C" void app_main(void) {
  static const char *kTag = "main";

  static app::Application application;
  const esp_err_t err = application.start();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "Application start failed: %s", esp_err_to_name(err));
  }
}
