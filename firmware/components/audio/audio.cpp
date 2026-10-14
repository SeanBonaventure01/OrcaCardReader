#include "orca/audio/audio.hpp"

#include <atomic>
#include <map>
#include <vector>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orca/board/board.h"
#include "orca/device/mailbox.hpp"
#include "orca/fx/audio.hpp"

namespace orca::audio {

namespace {

constexpr const char* TAG = "audio";
constexpr std::uint32_t kRate = 22050;
constexpr std::size_t kChunk = 512;  // samples per write (~23 ms), so new sounds cut in quickly

device::Mailbox<hal::SoundCmd>* g_mailbox = nullptr;
i2s_chan_handle_t g_tx = nullptr;
std::atomic<int> g_volume{70};

esp_err_t init_i2s() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;  // silence when we stop feeding it
  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &g_tx, nullptr), TAG, "new channel");
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = static_cast<gpio_num_t>(BOARD_PIN_I2S_BCLK),
              .ws = static_cast<gpio_num_t>(BOARD_PIN_I2S_LRCLK),
              .dout = static_cast<gpio_num_t>(BOARD_PIN_I2S_DOUT),
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {},
          },
  };
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_tx, &std_cfg), TAG, "std mode");
  return i2s_channel_enable(g_tx);
}

void audio_task(void*) {
  std::map<std::string, std::vector<std::int16_t>> cache;  // synthesized beeps
  std::vector<std::int16_t> out(kChunk);
  std::unique_ptr<hal::SoundCmd> current;
  std::size_t pos = 0;
  while (true) {
    if (auto next = g_mailbox->take(current ? 0 : portMAX_DELAY)) {
      current = std::move(next);
      pos = 0;
      ESP_LOGI(TAG, "SOUND %s", current->id.c_str());
    }
    if (!current) continue;
    auto it = cache.find(current->id);
    if (it == cache.end()) it = cache.emplace(current->id, fx::synth_beep(current->id, kRate)).first;
    const auto& pcm = it->second;
    if (!g_tx || pos >= pcm.size()) {
      if (pcm.empty()) ESP_LOGW(TAG, "no sound for '%s' (SD sounds not wired up yet)", current->id.c_str());
      current.reset();
      continue;
    }
    const std::size_t n = std::min(kChunk, pcm.size() - pos);
    fx::mix(pcm.data() + pos, n, g_volume.load(), nullptr, 0, 0, out.data(), n);
    std::size_t written = 0;
    i2s_channel_write(g_tx, out.data(), n * sizeof(std::int16_t), &written, pdMS_TO_TICKS(200));
    pos += n;
  }
}

}  // namespace

esp_err_t start(int volume_percent) {
  set_volume(volume_percent);
  if (esp_err_t err = init_i2s(); err != ESP_OK) {
    ESP_LOGW(TAG, "I2S unavailable (%s); sounds will only be logged", esp_err_to_name(err));
    g_tx = nullptr;
  }
  g_mailbox = new device::Mailbox<hal::SoundCmd>(8);
  xTaskCreatePinnedToCore(audio_task, "audio", 6144, nullptr, 7, nullptr, 1);
  return ESP_OK;
}

void play(const hal::SoundCmd& cmd) {
  if (g_mailbox) g_mailbox->post(cmd);
}

void set_volume(int percent) { g_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent; }

}  // namespace orca::audio
