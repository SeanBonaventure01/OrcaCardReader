#include <algorithm>
#include <mutex>
#include <set>
#include <string>

#include <unistd.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "orca/net/net.hpp"

// Phone UI, gzipped by web/scripts/postbuild.mjs and embedded by CMake.
extern const char index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const char index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const char app_js_gz_start[] asm("_binary_app_js_gz_start");
extern const char app_js_gz_end[] asm("_binary_app_js_gz_end");
extern const char index_css_gz_start[] asm("_binary_index_css_gz_start");
extern const char index_css_gz_end[] asm("_binary_index_css_gz_end");

namespace orca::net {

namespace {

constexpr const char* TAG = "http";
constexpr std::size_t kMaxBody = 8 * 1024;

httpd_handle_t g_server = nullptr;
api::Api* g_api = nullptr;
std::function<std::string()> g_hello;
std::mutex g_ws_mutex;  // guards g_clients and serializes frame sends
std::set<int> g_clients;

const char* status_line(int s) {
  switch (s) {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 204: return "204 No Content";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 429: return "429 Too Many Requests";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

std::string header(httpd_req_t* req, const char* name) {
  const std::size_t len = httpd_req_get_hdr_value_len(req, name);
  if (len == 0) return "";
  std::string v(len + 1, '\0');
  httpd_req_get_hdr_value_str(req, name, v.data(), v.size());
  v.resize(len);
  return v;
}

esp_err_t api_handler(httpd_req_t* req) {
  if (req->content_len > kMaxBody) {
    httpd_resp_set_status(req, "413 Payload Too Large");
    return httpd_resp_sendstr(req, "{\"error\":\"body too large\"}");
  }
  api::ApiRequest areq;
  areq.method = http_method_str(static_cast<http_method>(req->method));
  const std::string uri = req->uri;
  const auto q = uri.find('?');
  areq.path = uri.substr(0, q);
  if (q != std::string::npos) areq.query = uri.substr(q + 1);
  areq.body.resize(req->content_len);
  std::size_t got = 0;
  while (got < areq.body.size()) {
    const int r = httpd_req_recv(req, areq.body.data() + got, areq.body.size() - got);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
      return ESP_FAIL;
    }
    got += static_cast<std::size_t>(r);
  }
  const api::ApiResponse res = g_api->handle(areq);
  httpd_resp_set_status(req, status_line(res.status));
  httpd_resp_set_type(req, res.content_type.c_str());
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, res.body.data(), res.body.size());
}

esp_err_t send_gz(httpd_req_t* req, const char* type, const char* start, const char* end) {
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, start, end - start);
}

esp_err_t static_handler(httpd_req_t* req) {
  const std::string uri = req->uri;
  const std::string path = uri.substr(0, uri.find('?'));
  if (path == "/" || path == "/index.html") return send_gz(req, "text/html; charset=utf-8", index_html_gz_start, index_html_gz_end);
  if (path == "/app.js") return send_gz(req, "text/javascript", app_js_gz_start, app_js_gz_end);
  if (path == "/index.css") return send_gz(req, "text/css", index_css_gz_start, index_css_gz_end);
  // Anything else (captive-portal probes like /hotspot-detect.html) goes to the UI.
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  return httpd_resp_send(req, nullptr, 0);
}

esp_err_t send_text(int fd, const std::string& text) {
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<std::uint8_t*>(const_cast<char*>(text.data()));
  frame.len = text.size();
  frame.final = true;
  return httpd_ws_send_frame_async(g_server, fd, &frame);
}

esp_err_t ws_handler(httpd_req_t* req) {
  const int fd = httpd_req_to_sockfd(req);
  if (req->method == HTTP_GET) {  // handshake done
    const std::string hello = g_hello ? g_hello() : "";
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    g_clients.insert(fd);
    if (!hello.empty()) send_text(fd, hello);
    ESP_LOGI(TAG, "websocket client %d connected (%u total)", fd, static_cast<unsigned>(g_clients.size()));
    return ESP_OK;
  }
  // Clients only send pings/closes; read and discard.
  httpd_ws_frame_t frame = {};
  if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) return ESP_FAIL;
  if (frame.len > 0 && frame.len < 1024) {
    std::string buf(frame.len, '\0');
    frame.payload = reinterpret_cast<std::uint8_t*>(buf.data());
    httpd_ws_recv_frame(req, &frame, frame.len);
  }
  return ESP_OK;
}

void on_close(httpd_handle_t, int fd) {
  {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    g_clients.erase(fd);
  }
  close(fd);
}

}  // namespace

esp_err_t start_http(api::Api& api, std::function<std::string()> hello) {
  g_api = &api;
  g_hello = std::move(hello);
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 12;
  cfg.max_open_sockets = 12;  // needs CONFIG_LWIP_MAX_SOCKETS >= 15
  cfg.lru_purge_enable = true;
  cfg.stack_size = 12 * 1024;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.close_fn = on_close;
  cfg.core_id = 0;
  ESP_RETURN_ON_ERROR(httpd_start(&g_server, &cfg), TAG, "httpd_start");

  const httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = nullptr,
                          .is_websocket = true, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  httpd_register_uri_handler(g_server, &ws);
  for (httpd_method_t m : {HTTP_GET, HTTP_POST, HTTP_PATCH, HTTP_DELETE, HTTP_PUT}) {
    const httpd_uri_t h = {.uri = "/api/*", .method = m, .handler = api_handler, .user_ctx = nullptr,
                           .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
    httpd_register_uri_handler(g_server, &h);
  }
  const httpd_uri_t files = {.uri = "/*", .method = HTTP_GET, .handler = static_handler, .user_ctx = nullptr,
                             .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = nullptr};
  httpd_register_uri_handler(g_server, &files);
  ESP_LOGI(TAG, "HTTP server on port %d", cfg.server_port);
  return ESP_OK;
}

void broadcast(const std::string& text) {
  if (!g_server) return;
  std::lock_guard<std::mutex> lock(g_ws_mutex);
  for (auto it = g_clients.begin(); it != g_clients.end();) {
    if (httpd_ws_get_fd_info(g_server, *it) != HTTPD_WS_CLIENT_WEBSOCKET || send_text(*it, text) != ESP_OK) {
      it = g_clients.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace orca::net
