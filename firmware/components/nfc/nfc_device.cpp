#include "orca/nfc_device/nfc_device.hpp"

#include <atomic>
#include <cstdio>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orca/nfc/card_payload.hpp"
#include "orca/nfc/pn532.hpp"
#include "orca/nfc/tap_debouncer.hpp"

namespace orca::nfc_device {

namespace {

constexpr const char* TAG = "nfc";
constexpr std::uint16_t kPn532Addr = 0x24;
constexpr std::uint32_t kBusHz = 100000;  // the PN532 stretches the clock; keep the bus slow
constexpr std::uint32_t kPollMs = 50;
constexpr int kFailuresBeforeReinit = 3;

std::int64_t now_ms() { return esp_timer_get_time() / 1000; }

// PN532 I2C framing (UM0701-02 §6.2.4): every read transaction starts with a status byte
// (bit 0 = ready) followed by the frame from its beginning. So after wait_ready() we fetch
// the whole frame in one transaction and serve the driver's reads from that buffer.
class I2cTransport : public nfc::Transport {
 public:
  explicit I2cTransport(i2c_master_dev_handle_t dev) : dev_(dev) {}

  bool write(const nfc::Bytes& data) override {
    buf_.clear();
    return i2c_master_transmit(dev_, data.data(), data.size(), 50) == ESP_OK;
  }

  bool wait_ready(std::uint32_t timeout_ms) override {
    buf_.clear();
    const std::int64_t start = esp_timer_get_time();
    const std::int64_t deadline = start + static_cast<std::int64_t>(timeout_ms) * 1000;
    while (true) {
      std::uint8_t status = 0;
      if (i2c_master_receive(dev_, &status, 1, 20) == ESP_OK && (status & 0x01)) return true;
      const std::int64_t now = esp_timer_get_time();
      if (now >= deadline) return false;
      // ACKs arrive within ~1 ms: spin briefly before falling back to 10 ms ticks.
      if (now - start < 5000) esp_rom_delay_us(500);
      else vTaskDelay(1);
    }
  }

  nfc::Bytes read(std::size_t len) override {
    if (buf_.empty()) {
      // Largest frame we expect: NTAG READ response (26 bytes), 10-byte UID target (25).
      std::uint8_t raw[1 + kFrameMax];
      if (i2c_master_receive(dev_, raw, sizeof raw, 50) != ESP_OK || !(raw[0] & 0x01)) return {};
      buf_.assign(raw + 1, raw + sizeof raw);
      pos_ = 0;
    }
    if (pos_ + len > buf_.size()) return {};
    nfc::Bytes out(buf_.begin() + static_cast<long>(pos_), buf_.begin() + static_cast<long>(pos_ + len));
    pos_ += len;
    return out;
  }

 private:
  static constexpr std::size_t kFrameMax = 40;
  i2c_master_dev_handle_t dev_;
  nfc::Bytes buf_;
  std::size_t pos_ = 0;
};

i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_dev = nullptr;
CardHandler g_handler;
std::atomic<bool> g_reader_ok{false};

std::string to_hex(const nfc::Bytes& bytes) {
  std::string s;
  char b[3];
  for (auto v : bytes) {
    std::snprintf(b, sizeof b, "%02X", v);
    s += b;
  }
  return s;
}

// NTAG21x / Ultralight answer with SAK 0x00; MIFARE Classic, DESFire etc. don't, and can't be
// read or written with plain NTAG page commands.
bool is_ntag(const nfc::PassiveTarget& t) { return t.sel_res == 0x00; }

// The player number written on the card ("orca-card-game-07" as an NDEF Text record).
std::optional<int> read_card_player(nfc::Pn532& pn, const nfc::PassiveTarget& t) {
  if (!is_ntag(t)) return std::nullopt;
  auto mem = pn.ntag_read(nfc::kUserMemoryFirstPage);  // 16 bytes = 4 pages
  if (!mem) return std::nullopt;
  const std::size_t needed = nfc::ndef_bytes_needed(*mem);
  while (mem->size() < needed) {
    auto more = pn.ntag_read(static_cast<std::uint8_t>(nfc::kUserMemoryFirstPage + mem->size() / 4));
    if (!more) return std::nullopt;
    mem->insert(mem->end(), more->begin(), more->end());
  }
  return nfc::decode_card(*mem);
}

WriteResult write_card_player(nfc::Pn532& pn, const nfc::PassiveTarget& t, int number) {
  if (!is_ntag(t) || number < 1 || number > 9999) return WriteResult::Unsupported;
  const auto bytes = nfc::encode_card(number);
  for (std::size_t page = 0; page * 4 < bytes.size(); ++page) {
    const nfc::Bytes four(bytes.begin() + static_cast<long>(page * 4), bytes.begin() + static_cast<long>(page * 4 + 4));
    if (!pn.ntag_write(static_cast<std::uint8_t>(nfc::kUserMemoryFirstPage + page), four)) return WriteResult::Failed;
  }
  // Only trust the write once the card reads it back.
  return read_card_player(pn, t) == number ? WriteResult::Ok : WriteResult::Failed;
}

// Wiring diagnostic for when the reader doesn't answer: lists everything on the bus (PN532
// 0x24, GT911 touch 0x5D/0x14, PCA9557 0x18). Empty addresses make the driver log timeouts,
// so its logging is muted meanwhile.
void log_bus_scan() {
  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  std::string found;
  for (std::uint16_t addr = 0x08; addr < 0x78; ++addr) {
    if (i2c_master_probe(g_bus, addr, 20) == ESP_OK) {
      char b[6];
      std::snprintf(b, sizeof b, " 0x%02X", addr);
      found += b;
    }
  }
  esp_log_level_set("i2c.master", ESP_LOG_WARN);
  ESP_LOGW(TAG, "I2C scan:%s (PN532 should be 0x24: check its DIP switches are set to I2C and power-cycle)",
           found.empty() ? " nothing" : found.c_str());
}

bool init_reader(nfc::Pn532& pn) {
  // The PN532 may need a transaction or two to wake up after power-on.
  std::optional<nfc::FirmwareVersion> fw;
  for (int i = 0; i < 5 && !fw; ++i) {
    fw = pn.firmware_version();
    if (!fw) vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (!fw) {
    ESP_LOGW(TAG, "no PN532 at 0x%02X (%s)", kPn532Addr, nfc::to_string(pn.last_error()));
    static bool scanned = false;
    if (!scanned) log_bus_scan();
    scanned = true;
    return false;
  }
  if (!pn.sam_configure() || !pn.set_passive_retries(1)) {
    ESP_LOGW(TAG, "PN532 configuration failed (%s)", nfc::to_string(pn.last_error()));
    return false;
  }
  ESP_LOGI(TAG, "READER PN5%02X firmware %d.%d", fw->ic, fw->ver, fw->rev);
  return true;
}

void nfc_task(void*) {
  I2cTransport transport(g_dev);
  nfc::Pn532 pn(transport);
  nfc::TapDebouncer debouncer;
  int failures = 0;
  while (true) {
    if (!g_reader_ok) {
      g_reader_ok = init_reader(pn);
      if (!g_reader_ok) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
      failures = 0;
    }
    const std::int64_t t0 = now_ms();
    auto card = pn.poll_card(100);
    if (card) {
      failures = 0;
      const std::string uid = to_hex(card->uid);
      if (debouncer.card_present(uid, t0)) {
        const auto on_card = read_card_player(pn, *card);
        ESP_LOGI(TAG, "CARD %s sens_res=%04X sel_res=%02X card=#%d (read %lld ms)", uid.c_str(), card->sens_res,
                 card->sel_res, on_card.value_or(0), now_ms() - t0);
        if (auto number = g_handler.seen(uid, on_card)) {
          const WriteResult r = write_card_player(pn, *card, *number);
          ESP_LOGI(TAG, "PROGRAM %s #%d -> %s (%lld ms)", uid.c_str(), *number,
                   r == WriteResult::Ok ? "ok" : r == WriteResult::Failed ? "failed" : "unsupported", now_ms() - t0);
          g_handler.written(uid, *number, r);
        }
      }
    } else if (pn.last_error() == nfc::Pn532Error::None) {
      failures = 0;
      debouncer.field_empty(t0);
    } else if (++failures >= kFailuresBeforeReinit) {
      ESP_LOGE(TAG, "reader not responding (%s); reinitialising", nfc::to_string(pn.last_error()));
      g_reader_ok = false;
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

}  // namespace

esp_err_t start(int sda_pin, int scl_pin, CardHandler handler) {
  g_handler = std::move(handler);
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = -1;  // any free port
  bus_cfg.sda_io_num = static_cast<gpio_num_t>(sda_pin);
  bus_cfg.scl_io_num = static_cast<gpio_num_t>(scl_pin);
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &g_bus), TAG, "i2c bus");

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = kPn532Addr;
  dev_cfg.scl_speed_hz = kBusHz;
  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &dev_cfg, &g_dev), TAG, "i2c device");

  xTaskCreatePinnedToCore(nfc_task, "nfc", 4096, nullptr, 6, nullptr, 0);
  return ESP_OK;
}

bool reader_ok() { return g_reader_ok; }

}  // namespace orca::nfc_device
