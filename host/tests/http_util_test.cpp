#include <gtest/gtest.h>

#include "http_util.hpp"

using namespace orca::sim;

namespace {
std::string hex(const std::array<std::uint8_t, 20>& d) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  for (auto b : d) {
    s.push_back(digits[b >> 4]);
    s.push_back(digits[b & 15]);
  }
  return s;
}
}  // namespace

TEST(Sha1, KnownVectors) {
  EXPECT_EQ(hex(sha1("")), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  EXPECT_EQ(hex(sha1("abc")), "a9993e364706816aba3e25717850c26c9cd0d89d");
  EXPECT_EQ(hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
            "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  EXPECT_EQ(hex(sha1(std::string(1000, 'a'))), "291e9a6c66994949b57ba5e650361e98fc36b1ba");
}

TEST(Base64, Padding) {
  auto b = [](const std::string& s) { return base64(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); };
  EXPECT_EQ(b(""), "");
  EXPECT_EQ(b("f"), "Zg==");
  EXPECT_EQ(b("fo"), "Zm8=");
  EXPECT_EQ(b("foo"), "Zm9v");
  EXPECT_EQ(b("foobar"), "Zm9vYmFy");
}

TEST(WebSocket, AcceptKeyFromRfc6455) {
  EXPECT_EQ(ws_accept_key("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocket, EncodeLengthForms) {
  EXPECT_EQ(ws_encode(WsOpcode::Text, "hi"), std::string("\x81\x02hi", 4));
  const std::string mid = ws_encode(WsOpcode::Text, std::string(300, 'x'));
  EXPECT_EQ(static_cast<unsigned char>(mid[1]), 126);
  EXPECT_EQ(mid.size(), 4u + 300u);
  const std::string big = ws_encode(WsOpcode::Binary, std::string(70000, 'x'));
  EXPECT_EQ(static_cast<unsigned char>(big[1]), 127);
  EXPECT_EQ(big.size(), 10u + 70000u);
}

TEST(WebSocket, DecodeMaskedClientFrame) {
  // RFC 6455 §5.7: masked "Hello"
  const std::string frame("\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58", 11);
  bool bad = false;
  auto f = ws_decode(frame + "extra", bad);
  ASSERT_TRUE(f);
  EXPECT_FALSE(bad);
  EXPECT_EQ(f->opcode, WsOpcode::Text);
  EXPECT_TRUE(f->fin);
  EXPECT_EQ(f->payload, "Hello");
  EXPECT_EQ(f->consumed, 11u);
}

TEST(WebSocket, DecodeNeedsMoreAndRejectsHuge) {
  bool bad = false;
  EXPECT_FALSE(ws_decode(std::string("\x81", 1), bad));
  EXPECT_FALSE(ws_decode(std::string("\x81\x85\x37\xfa", 4), bad));
  EXPECT_FALSE(bad);
  EXPECT_FALSE(ws_decode(std::string("\x81\x7f\x00\x00\x00\x00\x01\x00\x00\x00", 10), bad));
  EXPECT_TRUE(bad);
}

TEST(WebSocket, RoundTripUnmasked) {
  bool bad = false;
  auto f = ws_decode(ws_encode(WsOpcode::Text, std::string(500, 'q')), bad);
  ASSERT_TRUE(f);
  EXPECT_EQ(f->payload, std::string(500, 'q'));
}

TEST(HttpParse, CompleteRequestWithBody) {
  const std::string raw =
      "POST /api/sim/tap?x=1 HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
      "Authorization: Bearer abc\r\nContent-Length: 13\r\n\r\n{\"uid\":\"04\"}!NEXT";
  auto r = parse_http_request(raw);
  ASSERT_EQ(r.status, ParseStatus::Complete);
  EXPECT_EQ(r.request.method, "POST");
  EXPECT_EQ(r.request.path, "/api/sim/tap");
  EXPECT_EQ(r.request.query, "x=1");
  EXPECT_EQ(r.request.headers["authorization"], "Bearer abc");
  EXPECT_EQ(r.request.body, "{\"uid\":\"04\"}!");
  EXPECT_EQ(raw.substr(r.consumed), "NEXT");
}

TEST(HttpParse, NeedsMoreUntilHeadersAndBodyArrive) {
  EXPECT_EQ(parse_http_request("GET / HTTP/1.1\r\nHost: x\r\n").status, ParseStatus::NeedMore);
  EXPECT_EQ(parse_http_request("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nab").status, ParseStatus::NeedMore);
}

TEST(HttpParse, RejectsMalformed) {
  EXPECT_EQ(parse_http_request("NONSENSE\r\n\r\n").status, ParseStatus::Bad);
  EXPECT_EQ(parse_http_request("GET / HTTP/1.1\r\nbadheader\r\n\r\n").status, ParseStatus::Bad);
  EXPECT_EQ(parse_http_request("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n").status, ParseStatus::Bad);
  EXPECT_EQ(parse_http_request("POST / HTTP/1.1\r\nContent-Length: 999999\r\n\r\n").status, ParseStatus::Bad);
  EXPECT_EQ(parse_http_request(std::string(20000, 'a')).status, ParseStatus::Bad);
}

TEST(HttpParse, UrlDecodePath) {
  auto r = parse_http_request("GET /a%20b/%zz HTTP/1.1\r\n\r\n");
  EXPECT_EQ(r.request.path, "/a b/%zz");
}

TEST(HttpResponse, Format) {
  EXPECT_EQ(http_response(404, "text/plain", "nope"),
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 4\r\nConnection: close\r\n\r\nnope");
  EXPECT_STREQ(content_type_for("/assets/index-abc.js"), "text/javascript");
  EXPECT_STREQ(content_type_for("/x.unknown"), "application/octet-stream");
}
