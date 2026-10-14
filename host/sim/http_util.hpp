#pragma once

// Pure HTTP/1.1 and WebSocket (RFC 6455) helpers for orca-sim's tiny server. No sockets here,
// so everything is unit-testable.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace orca::sim {

struct HttpRequest {
  std::string method;
  std::string target;  // path + optional ?query
  std::string path;
  std::string query;
  std::map<std::string, std::string> headers;  // lower-cased names
  std::string body;
};

enum class ParseStatus { Complete, NeedMore, Bad };

struct ParseResult {
  ParseStatus status = ParseStatus::NeedMore;
  HttpRequest request;
  std::size_t consumed = 0;  // bytes of the buffer used by this request
};

// Parses one request from the start of `buf`. Bodies need Content-Length (no chunked).
ParseResult parse_http_request(const std::string& buf, std::size_t max_body = 64 * 1024);

std::string http_response(int status, const std::string& content_type, const std::string& body,
                          const std::map<std::string, std::string>& extra_headers = {});
const char* status_text(int status);
const char* content_type_for(const std::string& path);
std::string url_decode(const std::string& s);

std::array<std::uint8_t, 20> sha1(const std::string& data);
std::string base64(const std::uint8_t* data, std::size_t len);
// Sec-WebSocket-Accept value for a client's Sec-WebSocket-Key.
std::string ws_accept_key(const std::string& client_key);

enum class WsOpcode : std::uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

// Server-to-client frame (unmasked, FIN set).
std::string ws_encode(WsOpcode op, const std::string& payload);

struct WsFrame {
  WsOpcode opcode;
  bool fin;
  std::string payload;  // unmasked
  std::size_t consumed;
};

// Decodes one frame from the start of `buf`; nullopt if more bytes are needed.
// Sets `bad` for protocol errors (e.g. oversized frames).
std::optional<WsFrame> ws_decode(const std::string& buf, bool& bad, std::size_t max_payload = 64 * 1024);

}  // namespace orca::sim
