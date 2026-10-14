#pragma once

// orca-sim's HTTP + WebSocket server: /api/* -> Api::handle, /ws -> push messages,
// everything else -> static files from the built phone UI.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "world.hpp"

namespace orca::sim {

class SimServer {
 public:
  SimServer(World& world, std::string web_dir) : world_(world), web_dir_(std::move(web_dir)) {}
  ~SimServer();

  // Binds 0.0.0.0:port. Returns false (with a message on stderr) on failure.
  bool listen(int port);
  // Accept loop; returns when stop() is called.
  void run();
  void stop();

  // Sends a JSON message to every WebSocket client.
  void broadcast(const json& msg);
  std::size_t client_count();

 private:
  struct WsClient {
    int fd;
    std::mutex write_mutex;
    std::atomic<bool> alive{true};
  };

  void serve_connection(int fd);
  void serve_websocket(int fd, const std::string& key);
  std::string serve_static(const std::string& path);
  static bool send_all(int fd, const std::string& data);

  World& world_;
  std::string web_dir_;
  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::mutex clients_mutex_;
  std::vector<std::shared_ptr<WsClient>> clients_;
};

}  // namespace orca::sim
