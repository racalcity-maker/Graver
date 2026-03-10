#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include <string>

struct cJSON;

namespace web {

void ApplyCorsHeaders(httpd_req_t *request);
esp_err_t SendSimpleResponse(httpd_req_t *request, int status_code, const char *body);
esp_err_t SendErrNameResponse(httpd_req_t *request, int status_code, const char *error_code, esp_err_t err);
esp_err_t SendJsonResponse(httpd_req_t *request, cJSON *root);
esp_err_t ReceiveRequestBody(httpd_req_t *request, std::string &body, size_t max_bytes);
esp_err_t ReceiveBinaryBody(httpd_req_t *request, std::string &body, size_t max_bytes);
std::string GetHeaderValue(httpd_req_t *request, const char *header_name);
std::string GetQueryValue(httpd_req_t *request, const char *key);

}  // namespace web
