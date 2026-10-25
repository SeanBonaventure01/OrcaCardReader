#include "sim_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include "http_util.hpp"

namespace orca::sim {

namespace {

const std::map<std::string, std::string> kCors = {
    {"Access-Control-Allow-Origin", "*"},
    {"Access-Control-Allow-Headers", "Content-Type"},
    {"Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS"},
};

}  // namespace

SimServer::~SimServer() { stop(); }

bool SimServer::listen(int port) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    std::perror("socket");
    return false;
  }
  int yes = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || ::listen(listen_fd_, 16) < 0) {
    std::perror("bind/listen");
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  running_ = true;
  return true;
}

void SimServer::stop() {
  running_ = false;
  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  std::lock_guard<std::mutex> lock(clients_mutex_);
  for (auto& c : clients_) {
    c->alive = false;
    ::shutdown(c->fd, SHUT_RDWR);
  }
}

void SimServer::run() {
  while (running_) {
    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) continue;
#ifdef SO_NOSIGPIPE
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
    std::thread([this, fd] { serve_connection(fd); }).detach();
  }
}

bool SimServer::send_all(int fd, const std::string& data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) return false;
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

void SimServer::serve_connection(int fd) {
  std::string buf;
  char chunk[4096];
  ParseResult parsed;
  while (true) {
    parsed = parse_http_request(buf);
    if (parsed.status != ParseStatus::NeedMore) break;
    const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
    if (n <= 0) {
      ::close(fd);
      return;
    }
    buf.append(chunk, static_cast<std::size_t>(n));
  }
  if (parsed.status == ParseStatus::Bad) {
    send_all(fd, http_response(400, "text/plain", "bad request"));
    ::close(fd);
    return;
  }
  const HttpRequest& req = parsed.request;

  if (req.path == "/ws") {
    auto key = req.headers.find("sec-websocket-key");
    if (key == req.headers.end()) {
      send_all(fd, http_response(400, "text/plain", "websocket key required"));
      ::close(fd);
      return;
    }
    serve_websocket(fd, key->second);
    return;
  }

  std::string response;
  if (req.method == "OPTIONS") {
    response = http_response(204, "", "", kCors);
  } else if (req.path.rfind("/api/", 0) == 0) {
    api::ApiRequest areq{req.method, req.path, req.query, req.body};
    const api::ApiResponse ares = world_.api.handle(areq);
    response = http_response(ares.status, ares.content_type, ares.body, kCors);
  } else {
    response = serve_static(req.path);
  }
  send_all(fd, response);
  ::close(fd);
}

std::string SimServer::serve_static(const std::string& path) {
  if (web_dir_.empty())
    return http_response(404, "text/plain", "orca-sim: no web UI directory (use --web web/dist)");
  if (path.find("..") != std::string::npos) return http_response(400, "text/plain", "bad path");
  auto read = [](const std::string& file) -> std::optional<std::string> {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  };
  const std::string rel = path == "/" ? "/index.html" : path;
  if (auto body = read(web_dir_ + rel)) return http_response(200, content_type_for(rel), *body);
  // Single-page app: unknown paths without an extension get index.html.
  if (rel.find('.', rel.rfind('/')) == std::string::npos) {
    if (auto body = read(web_dir_ + "/index.html")) return http_response(200, content_type_for("/index.html"), *body);
  }
  return http_response(404, "text/plain", "not found");
}

void SimServer::serve_websocket(int fd, const std::string& key) {
  const std::string handshake =
      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
      ws_accept_key(key) + "\r\n\r\n";
  if (!send_all(fd, handshake)) {
    ::close(fd);
    return;
  }
  auto client = std::make_shared<WsClient>();
  client->fd = fd;
  {
    std::lock_guard<std::mutex> w(client->write_mutex);
    send_all(fd, ws_encode(WsOpcode::Text, world_.api.hello().dump()));
  }
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.push_back(client);
  }

  std::string buf;
  char chunk[4096];
  while (client->alive) {
    const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
    if (n <= 0) break;
    buf.append(chunk, static_cast<std::size_t>(n));
    bool bad = false;
    while (auto frame = ws_decode(buf, bad)) {
      buf.erase(0, frame->consumed);
      if (frame->opcode == WsOpcode::Close) {
        client->alive = false;
        break;
      }
      if (frame->opcode == WsOpcode::Ping) {
        std::lock_guard<std::mutex> w(client->write_mutex);
        send_all(fd, ws_encode(WsOpcode::Pong, frame->payload));
      }
      // Clients don't send commands over the socket; REST is used for that.
    }
    if (bad) break;
  }
  client->alive = false;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
  }
  ::close(fd);
}

void SimServer::broadcast(const json& msg) {
  const std::string frame = ws_encode(WsOpcode::Text, msg.dump());
  std::vector<std::shared_ptr<WsClient>> targets;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    targets = clients_;
  }
  for (auto& c : targets) {
    if (!c->alive) continue;
    std::lock_guard<std::mutex> w(c->write_mutex);
    if (!send_all(c->fd, frame)) {
      c->alive = false;
      ::shutdown(c->fd, SHUT_RDWR);
    }
  }
}

std::size_t SimServer::client_count() {
  std::lock_guard<std::mutex> lock(clients_mutex_);
  return clients_.size();
}

}  // namespace orca::sim
