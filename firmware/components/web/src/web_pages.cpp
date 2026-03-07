#include "web/web_server.hpp"

#include "esp_log.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace web {

namespace {

constexpr const char *kTag = "web";

extern const uint8_t ui_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t ui_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t ui_styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t ui_styles_css_end[] asm("_binary_styles_css_end");
extern const uint8_t ui_api_js_start[] asm("_binary_api_js_start");
extern const uint8_t ui_api_js_end[] asm("_binary_api_js_end");
extern const uint8_t ui_app_js_start[] asm("_binary_app_js_start");
extern const uint8_t ui_app_js_end[] asm("_binary_app_js_end");
extern const uint8_t ui_upload_js_start[] asm("_binary_upload_js_start");
extern const uint8_t ui_upload_js_end[] asm("_binary_upload_js_end");
extern const uint8_t ui_image_rasterize_js_start[] asm("_binary_rasterize_js_start");
extern const uint8_t ui_image_rasterize_js_end[] asm("_binary_rasterize_js_end");
extern const uint8_t ui_image_primitives_js_start[] asm("_binary_primitives_js_start");
extern const uint8_t ui_image_primitives_js_end[] asm("_binary_primitives_js_end");
extern const uint8_t ui_image_stroke_font_js_start[] asm("_binary_stroke_font_js_start");
extern const uint8_t ui_image_stroke_font_js_end[] asm("_binary_stroke_font_js_end");

struct EmbeddedAsset {
  const char *uri;
  const char *contentType;
  const uint8_t *start;
  const uint8_t *end;
};

size_t AssetLength(const uint8_t *start, const uint8_t *end) {
  size_t length = static_cast<size_t>(end - start);
  if (length > 0U && start[length - 1U] == '\0') {
    --length;
  }
  return length;
}

std::string LoadEmbeddedText(const uint8_t *start, const uint8_t *end) {
  return std::string(reinterpret_cast<const char *>(start), AssetLength(start, end));
}

std::string ReplaceAll(std::string value, const char *from, const char *to) {
  size_t position = 0;
  const std::string needle = from;
  const std::string replacement = to;
  while ((position = value.find(needle, position)) != std::string::npos) {
    value.replace(position, needle.size(), replacement);
    position += replacement.size();
  }
  return value;
}

const EmbeddedAsset *FindAsset(const char *uri) {
  const std::string requested = [&]() {
    const char *query = std::strchr(uri, '?');
    if (query == nullptr) {
      return std::string(uri);
    }
    return std::string(uri, static_cast<size_t>(query - uri));
  }();

  static const EmbeddedAsset kAssets[] = {
      {"/assets/styles.css", "text/css; charset=utf-8", ui_styles_css_start, ui_styles_css_end},
      {"/assets/api.js", "application/javascript; charset=utf-8", ui_api_js_start, ui_api_js_end},
      {"/assets/app.js", "application/javascript; charset=utf-8", ui_app_js_start, ui_app_js_end},
      {"/assets/upload.js", "application/javascript; charset=utf-8", ui_upload_js_start, ui_upload_js_end},
      {"/assets/image/rasterize.js", "application/javascript; charset=utf-8", ui_image_rasterize_js_start,
       ui_image_rasterize_js_end},
      {"/assets/image/primitives.js", "application/javascript; charset=utf-8", ui_image_primitives_js_start,
       ui_image_primitives_js_end},
      {"/assets/image/stroke_font.js", "application/javascript; charset=utf-8", ui_image_stroke_font_js_start,
       ui_image_stroke_font_js_end},
  };

  for (const EmbeddedAsset &asset : kAssets) {
    if (requested == asset.uri) {
      return &asset;
    }
  }
  return nullptr;
}

esp_err_t SendTextResponse(httpd_req_t *request, const char *contentType, const char *body, const size_t length) {
  httpd_resp_set_status(request, "200 OK");
  httpd_resp_set_type(request, contentType);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(request, "Pragma", "no-cache");
  httpd_resp_set_hdr(request, "Expires", "0");
  return httpd_resp_send(request, body, static_cast<ssize_t>(length));
}

std::string RenderIndexHtml(const shared::MachineConfig &config) {
  std::string html = LoadEmbeddedText(ui_index_html_start, ui_index_html_end);
  const char *home_label = config.safety.homingEnabled ? "Home" : "Set Zero";
  const char *home_action = config.safety.homingEnabled ? "Home" : "Set Zero";
  html = ReplaceAll(std::move(html), "__HOME_LABEL__", home_label);
  html = ReplaceAll(std::move(html), "__HOME_ACTION__", home_action);
  return html;
}

}  // namespace

esp_err_t WebServer::HandleIndex(httpd_req_t *request) {
  ESP_LOGI(kTag, "GET /");
  WebServer *server = FromRequest(request);
  const std::string html = RenderIndexHtml(server->config_);
  return SendTextResponse(request, "text/html; charset=utf-8", html.c_str(), html.size());
}

esp_err_t WebServer::HandleAsset(httpd_req_t *request) {
  const EmbeddedAsset *asset = FindAsset(request->uri);
  if (asset == nullptr) {
    return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Asset not found");
  }

  return SendTextResponse(request, asset->contentType, reinterpret_cast<const char *>(asset->start),
                          AssetLength(asset->start, asset->end));
}

esp_err_t WebServer::HandleFavicon(httpd_req_t *request) {
  httpd_resp_set_status(request, "204 No Content");
  return httpd_resp_send(request, nullptr, 0);
}

}  // namespace web
