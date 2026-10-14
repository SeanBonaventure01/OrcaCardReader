#include "http_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace orca::sim {

namespace {

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t");
  if (b == std::string::npos) return "";
  return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

}  // namespace

ParseResult parse_http_request(const std::string& buf, std::size_t max_body) {
  ParseResult r;
  const auto header_end = buf.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    r.status = buf.size() > 16 * 1024 ? ParseStatus::Bad : ParseStatus::NeedMore;
    return r;
  }
  HttpRequest& req = r.request;
  std::size_t pos = 0;
  auto next_line = [&]() {
    const auto e = buf.find("\r\n", pos);
    std::string line = buf.substr(pos, e - pos);
    pos = e + 2;
    return line;
  };
  const std::string request_line = next_line();
  const auto sp1 = request_line.find(' ');
  const auto sp2 = request_line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos || request_line.compare(sp2 + 1, 5, "HTTP/") != 0) {
    r.status = ParseStatus::Bad;
    return r;
  }
  req.method = request_line.substr(0, sp1);
  req.target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
  const auto q = req.target.find('?');
  req.path = url_decode(req.target.substr(0, q));
  if (q != std::string::npos) req.query = req.target.substr(q + 1);

  while (pos < header_end) {
    const std::string line = next_line();
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      r.status = ParseStatus::Bad;
      return r;
    }
    req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
  }
  std::size_t body_len = 0;
  if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
    if (it->second.empty() || it->second.size() > 9 ||
        !std::all_of(it->second.begin(), it->second.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
      r.status = ParseStatus::Bad;
      return r;
    }
    body_len = std::stoul(it->second);
    if (body_len > max_body) {
      r.status = ParseStatus::Bad;
      return r;
    }
  }
  const std::size_t body_start = header_end + 4;
  if (buf.size() < body_start + body_len) {
    r.status = ParseStatus::NeedMore;
    return r;
  }
  req.body = buf.substr(body_start, body_len);
  r.consumed = body_start + body_len;
  r.status = ParseStatus::Complete;
  return r;
}

const char* status_text(int status) {
  switch (status) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    default: return "Unknown";
  }
}

std::string http_response(int status, const std::string& content_type, const std::string& body,
                          const std::map<std::string, std::string>& extra_headers) {
  std::string out = "HTTP/1.1 " + std::to_string(status) + " " + status_text(status) + "\r\n";
  if (!content_type.empty()) out += "Content-Type: " + content_type + "\r\n";
  out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  out += "Connection: close\r\n";
  for (const auto& [k, v] : extra_headers) out += k + ": " + v + "\r\n";
  out += "\r\n";
  out += body;
  return out;
}

const char* content_type_for(const std::string& path) {
  auto ends = [&](const char* ext) {
    const std::size_t n = std::strlen(ext);
    return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
  };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".js")) return "text/javascript";
  if (ends(".css")) return "text/css";
  if (ends(".json")) return "application/json";
  if (ends(".svg")) return "image/svg+xml";
  if (ends(".png")) return "image/png";
  if (ends(".ico")) return "image/x-icon";
  if (ends(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

std::string url_decode(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
      i += 2;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

// ---------------------------------------------------------------- SHA-1 (FIPS 180-1)

std::array<std::uint8_t, 20> sha1(const std::string& data) {
  std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string msg = data;
  const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back('\0');
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));

  auto rol = [](std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
  for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + chunk + i * 4);
      w[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      std::uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::array<std::uint8_t, 20> out{};
  for (int i = 0; i < 5; ++i)
    for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<std::uint8_t>(h[i] >> (24 - j * 8));
  return out;
}

std::string base64(const std::uint8_t* data, std::size_t len) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (std::size_t i = 0; i < len; i += 3) {
    const std::uint32_t n = (std::uint32_t(data[i]) << 16) | (i + 1 < len ? std::uint32_t(data[i + 1]) << 8 : 0) |
                            (i + 2 < len ? data[i + 2] : 0);
    out.push_back(t[(n >> 18) & 63]);
    out.push_back(t[(n >> 12) & 63]);
    out.push_back(i + 1 < len ? t[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < len ? t[n & 63] : '=');
  }
  return out;
}

std::string ws_accept_key(const std::string& client_key) {
  const auto digest = sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  return base64(digest.data(), digest.size());
}

// ---------------------------------------------------------------- WebSocket frames

std::string ws_encode(WsOpcode op, const std::string& payload) {
  std::string f;
  f.push_back(static_cast<char>(0x80 | static_cast<std::uint8_t>(op)));
  const std::size_t n = payload.size();
  if (n < 126) {
    f.push_back(static_cast<char>(n));
  } else if (n <= 0xFFFF) {
    f.push_back(126);
    f.push_back(static_cast<char>(n >> 8));
    f.push_back(static_cast<char>(n & 0xFF));
  } else {
    f.push_back(127);
    for (int i = 7; i >= 0; --i) f.push_back(static_cast<char>((static_cast<std::uint64_t>(n) >> (i * 8)) & 0xFF));
  }
  return f + payload;
}

std::optional<WsFrame> ws_decode(const std::string& buf, bool& bad, std::size_t max_payload) {
  bad = false;
  if (buf.size() < 2) return std::nullopt;
  const auto* p = reinterpret_cast<const unsigned char*>(buf.data());
  WsFrame f;
  f.fin = p[0] & 0x80;
  f.opcode = static_cast<WsOpcode>(p[0] & 0x0F);
  const bool masked = p[1] & 0x80;
  std::uint64_t len = p[1] & 0x7F;
  std::size_t pos = 2;
  if (len == 126) {
    if (buf.size() < 4) return std::nullopt;
    len = (std::uint64_t(p[2]) << 8) | p[3];
    pos = 4;
  } else if (len == 127) {
    if (buf.size() < 10) return std::nullopt;
    len = 0;
    for (int i = 0; i < 8; ++i) len = (len << 8) | p[2 + i];
    pos = 10;
  }
  if (len > max_payload) {
    bad = true;
    return std::nullopt;
  }
  std::uint8_t mask[4] = {0, 0, 0, 0};
  if (masked) {
    if (buf.size() < pos + 4) return std::nullopt;
    std::memcpy(mask, p + pos, 4);
    pos += 4;
  }
  if (buf.size() < pos + len) return std::nullopt;
  f.payload.resize(len);
  for (std::size_t i = 0; i < len; ++i) f.payload[i] = static_cast<char>(p[pos + i] ^ mask[i % 4]);
  f.consumed = pos + len;
  return f;
}

}  // namespace orca::sim
