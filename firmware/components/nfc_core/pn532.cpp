#include "orca/nfc/pn532.hpp"

namespace orca::nfc {

const Bytes kAckFrame = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
const Bytes kNackFrame = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};

Bytes build_frame(std::uint8_t command, const Bytes& params) {
  const std::size_t len = 2 + params.size();  // TFI + command + params
  Bytes f = {0x00, 0x00, 0xFF, static_cast<std::uint8_t>(len), static_cast<std::uint8_t>(0x100 - len), kHostToPn532,
             command};
  std::uint8_t sum = kHostToPn532 + command;
  for (auto b : params) {
    f.push_back(b);
    sum = static_cast<std::uint8_t>(sum + b);
  }
  f.push_back(static_cast<std::uint8_t>(0x100 - sum));
  f.push_back(0x00);
  return f;
}

ParsedFrame parse_frame(const Bytes& buf) {
  ParsedFrame p;
  std::size_t i = 0;
  while (i + 1 < buf.size() && !(buf[i] == 0x00 && buf[i + 1] == 0xFF)) ++i;
  if (i + 1 >= buf.size()) return p;  // no start code yet
  const std::size_t start = i + 2;
  if (buf.size() < start + 2) return p;
  const std::uint8_t len = buf[start];
  const std::uint8_t lcs = buf[start + 1];
  if (len == 0x00 && lcs == 0xFF) {
    p.kind = FrameKind::Ack;
    p.consumed = start + 2 + (buf.size() > start + 2 ? 1 : 0);
    return p;
  }
  if (len == 0xFF && lcs == 0x00) {
    p.kind = FrameKind::Nack;
    p.consumed = start + 2 + (buf.size() > start + 2 ? 1 : 0);
    return p;
  }
  if (static_cast<std::uint8_t>(len + lcs) != 0 || len == 0) {
    p.kind = FrameKind::Invalid;
    p.consumed = start + 2;
    return p;
  }
  if (buf.size() < start + 2 + len + 1) return p;  // need TFI..PD + DCS
  std::uint8_t sum = 0;
  for (std::size_t k = 0; k <= len; ++k) sum = static_cast<std::uint8_t>(sum + buf[start + 2 + k]);
  p.consumed = start + 2 + len + 1 + (buf.size() > start + 3 + len ? 1 : 0);
  if (sum != 0) {
    p.kind = FrameKind::Invalid;
    return p;
  }
  p.tfi = buf[start + 2];
  p.data.assign(buf.begin() + static_cast<long>(start + 3), buf.begin() + static_cast<long>(start + 2 + len));
  // Application-level error frame: TFI 0x7F, one byte of error code.
  p.kind = (p.tfi == 0x7F) ? FrameKind::Error : FrameKind::Data;
  return p;
}

std::optional<FirmwareVersion> parse_firmware_version(const Bytes& r) {
  if (r.size() != 4) return std::nullopt;
  return FirmwareVersion{r[0], r[1], r[2], r[3]};
}

std::optional<PassiveTarget> parse_passive_target(const Bytes& r) {
  // NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID1...
  if (r.empty() || r[0] == 0) return std::nullopt;
  if (r.size() < 6) return std::nullopt;
  const std::size_t uid_len = r[5];
  if (uid_len < 4 || uid_len > 10 || r.size() < 6 + uid_len) return std::nullopt;
  PassiveTarget t;
  t.target_number = r[1];
  t.sens_res = static_cast<std::uint16_t>(r[2] << 8 | r[3]);
  t.sel_res = r[4];
  t.uid.assign(r.begin() + 6, r.begin() + 6 + static_cast<long>(uid_len));
  return t;
}

const char* to_string(Pn532Error e) {
  switch (e) {
    case Pn532Error::None: return "none";
    case Pn532Error::WriteFailed: return "write failed";
    case Pn532Error::NoAck: return "no ack";
    case Pn532Error::Timeout: return "timeout";
    case Pn532Error::BadFrame: return "bad frame";
    case Pn532Error::ErrorFrame: return "error frame";
    case Pn532Error::WrongResponse: return "wrong response";
    case Pn532Error::CardError: return "card error";
  }
  return "?";
}

std::optional<Bytes> Pn532::command(std::uint8_t code, const Bytes& params, std::uint32_t timeout_ms) {
  error_ = Pn532Error::None;
  if (!t_.write(build_frame(code, params))) {
    error_ = Pn532Error::WriteFailed;
    return std::nullopt;
  }
  if (!t_.wait_ready(20)) {
    error_ = Pn532Error::NoAck;
    return std::nullopt;
  }
  const Bytes ack = t_.read(kAckFrame.size());
  if (ack != kAckFrame) {
    error_ = Pn532Error::NoAck;
    return std::nullopt;
  }
  if (!t_.wait_ready(timeout_ms)) {
    error_ = Pn532Error::Timeout;
    return std::nullopt;
  }
  // Read the header first to learn the length, then the rest.
  Bytes frame = t_.read(5);
  if (frame.size() != 5) {
    error_ = Pn532Error::BadFrame;
    return std::nullopt;
  }
  const std::size_t rest = static_cast<std::size_t>(frame[3]) + 2;  // TFI..PD + DCS + postamble
  const Bytes tail = t_.read(rest);
  frame.insert(frame.end(), tail.begin(), tail.end());
  const ParsedFrame p = parse_frame(frame);
  if (p.kind == FrameKind::Error) {
    error_ = Pn532Error::ErrorFrame;
    return std::nullopt;
  }
  if (p.kind != FrameKind::Data || p.tfi != kPn532ToHost) {
    error_ = Pn532Error::BadFrame;
    return std::nullopt;
  }
  if (p.data.empty() || p.data[0] != code + 1) {
    error_ = Pn532Error::WrongResponse;
    return std::nullopt;
  }
  return Bytes(p.data.begin() + 1, p.data.end());
}

std::optional<FirmwareVersion> Pn532::firmware_version() {
  auto r = command(cmd::kGetFirmwareVersion, {});
  return r ? parse_firmware_version(*r) : std::nullopt;
}

bool Pn532::sam_configure() { return command(cmd::kSamConfiguration, {0x01, 0x00, 0x01}).has_value(); }

std::optional<PassiveTarget> Pn532::poll_card(std::uint32_t timeout_ms) {
  auto r = command(cmd::kInListPassiveTarget, {0x01, 0x00}, timeout_ms);
  return r ? parse_passive_target(*r) : std::nullopt;
}

std::optional<Bytes> Pn532::ntag_read(std::uint8_t page) {
  auto r = command(cmd::kInDataExchange, {0x01, 0x30, page});
  if (!r) return std::nullopt;
  if (r->size() != 17 || (*r)[0] != 0x00) {
    error_ = Pn532Error::CardError;
    return std::nullopt;
  }
  return Bytes(r->begin() + 1, r->end());
}

bool Pn532::ntag_write(std::uint8_t page, const Bytes& four) {
  if (four.size() != 4) return false;
  Bytes params = {0x01, 0xA2, page};
  params.insert(params.end(), four.begin(), four.end());
  auto r = command(cmd::kInDataExchange, params);
  if (!r) return false;
  if (r->empty() || (*r)[0] != 0x00) {
    error_ = Pn532Error::CardError;
    return false;
  }
  return true;
}

bool Pn532::release() { return command(cmd::kInRelease, {0x00}).has_value(); }

}  // namespace orca::nfc
