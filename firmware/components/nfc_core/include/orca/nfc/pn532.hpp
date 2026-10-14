#pragma once

// PN532 host protocol (NXP UM0701-02 §6.2): frame building/parsing and a small command
// driver over an abstract byte transport. Hardware transports (I2C) live in the `pn532`
// ESP-IDF component; tests use a scripted fake.

#include <cstdint>
#include <optional>
#include <vector>

namespace orca::nfc {

using Bytes = std::vector<std::uint8_t>;

namespace cmd {
inline constexpr std::uint8_t kGetFirmwareVersion = 0x02;
inline constexpr std::uint8_t kSamConfiguration = 0x14;
inline constexpr std::uint8_t kInDataExchange = 0x40;
inline constexpr std::uint8_t kInListPassiveTarget = 0x4A;
inline constexpr std::uint8_t kInRelease = 0x52;
}  // namespace cmd

inline constexpr std::uint8_t kHostToPn532 = 0xD4;
inline constexpr std::uint8_t kPn532ToHost = 0xD5;

// Normal information frame from host to PN532 for `command` + params.
Bytes build_frame(std::uint8_t command, const Bytes& params = {});
extern const Bytes kAckFrame;   // 00 00 FF 00 FF 00
extern const Bytes kNackFrame;  // 00 00 FF FF 00 00

enum class FrameKind { Incomplete, Invalid, Ack, Nack, Error, Data };

struct ParsedFrame {
  FrameKind kind = FrameKind::Incomplete;
  std::uint8_t tfi = 0;
  Bytes data;  // bytes after TFI and before DCS: response code + payload
  std::size_t consumed = 0;
};

// Parses the first frame in `buf`, skipping any leading bytes before 00 FF.
ParsedFrame parse_frame(const Bytes& buf);

struct FirmwareVersion {
  std::uint8_t ic, ver, rev, support;
};
std::optional<FirmwareVersion> parse_firmware_version(const Bytes& response);

struct PassiveTarget {
  std::uint8_t target_number;
  std::uint16_t sens_res;
  std::uint8_t sel_res;
  Bytes uid;
};
// Parses an InListPassiveTarget (106 kbps type A) response. nullopt if no target was found.
std::optional<PassiveTarget> parse_passive_target(const Bytes& response);

// Byte stream to the PN532. On I2C, `read` returns frame bytes after the ready status byte.
class Transport {
 public:
  virtual ~Transport() = default;
  virtual bool write(const Bytes& data) = 0;
  // Waits up to timeout_ms for the PN532 to signal ready.
  virtual bool wait_ready(std::uint32_t timeout_ms) = 0;
  // Reads exactly `len` bytes; empty on failure.
  virtual Bytes read(std::size_t len) = 0;
};

enum class Pn532Error { None, WriteFailed, NoAck, Timeout, BadFrame, ErrorFrame, WrongResponse, CardError };
const char* to_string(Pn532Error e);

class Pn532 {
 public:
  explicit Pn532(Transport& t) : t_(t) {}

  // Sends a command and returns the response payload (after the response code).
  std::optional<Bytes> command(std::uint8_t code, const Bytes& params, std::uint32_t timeout_ms = 100);

  std::optional<FirmwareVersion> firmware_version();
  bool sam_configure();  // normal mode, no timeout, IRQ on
  // Polls once for an ISO14443A card. nullopt if none in the field or on error (see last_error()).
  std::optional<PassiveTarget> poll_card(std::uint32_t timeout_ms = 60);
  // NTAG READ: 16 bytes starting at `page`.
  std::optional<Bytes> ntag_read(std::uint8_t page);
  // NTAG WRITE: 4 bytes at `page`.
  bool ntag_write(std::uint8_t page, const Bytes& four_bytes);
  bool release();

  Pn532Error last_error() const { return error_; }

 private:
  Transport& t_;
  Pn532Error error_ = Pn532Error::None;
};

}  // namespace orca::nfc
