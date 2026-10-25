#include "orca/audio/audio.hpp"

#include <array>
#include <atomic>
#include <map>
#include <optional>
#include <vector>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orca/board/board.h"
#include "orca/device/mailbox.hpp"
#include "orca/fx/audio.hpp"
#include "orca/fx/voice.hpp"

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

// Voice lines (profanity mode), built into the firmware by tools/assets/make_voice.py.
extern const std::uint8_t voice_profanity_start[] asm("_binary_voice_profanity_bin_start");
extern const std::uint8_t voice_profanity_end[] asm("_binary_voice_profanity_bin_end");

constexpr std::size_t kVoiceDelay = kRate / 4;  // let the bong ring for 250 ms first

// The lines of one kind, picked in shuffled order without repeats.
struct LineSet {
  std::vector<const fx::VoiceLine*> lines;
  fx::ShuffleBag bag{0, 1};
  const fx::VoiceLine* next() { return lines.empty() ? nullptr : lines[bag.next()]; }
};

std::optional<fx::VoicePack> g_pack;
// [voice][kind]: each voice has its own shuffled order per kind, and each tap picks a voice.
std::vector<std::array<LineSet, 2>> g_lines;
std::uint32_t g_voice_rng = 1;
std::atomic<bool> g_voice_on{false};

void load_voice_pack() {
  g_pack = fx::parse_voice_pack(voice_profanity_start, voice_profanity_end - voice_profanity_start);
  if (!g_pack || g_pack->voices.empty()) {
    ESP_LOGE(TAG, "profanity voice pack is invalid; voice lines disabled");
    g_pack.reset();
    return;
  }
  g_lines.resize(g_pack->voices.size());
  for (const auto& l : g_pack->lines) g_lines[l.voice][static_cast<int>(l.kind)].lines.push_back(&l);
  g_voice_rng = static_cast<std::uint32_t>(esp_timer_get_time()) | 1;
  for (std::size_t v = 0; v < g_lines.size(); ++v) {
    for (auto& set : g_lines[v]) set.bag = fx::ShuffleBag(set.lines.size(), g_voice_rng * (2 * v + 3));
    ESP_LOGI(TAG, "voice %s: %u accept + %u reject lines", g_pack->voices[v].c_str(),
             static_cast<unsigned>(g_lines[v][0].lines.size()), static_cast<unsigned>(g_lines[v][1].lines.size()));
  }
}

// A line of `kind` in a randomly chosen voice that has one.
const fx::VoiceLine* pick_line(fx::LineKind kind) {
  g_voice_rng ^= g_voice_rng << 13;  // xorshift32
  g_voice_rng ^= g_voice_rng >> 17;
  g_voice_rng ^= g_voice_rng << 5;
  const std::size_t n = g_lines.size();
  for (std::size_t k = 0; k < n; ++k) {
    LineSet& set = g_lines[(g_voice_rng + k) % n][static_cast<int>(kind)];
    if (const fx::VoiceLine* line = set.next()) return line;
  }
  return nullptr;
}

// One sound being played: `delay` samples of silence, then `pcm`.
struct Stream {
  const std::vector<std::int16_t>* pcm = nullptr;
  std::size_t pos = 0;
  std::size_t delay = 0;
  bool active() const { return pcm && pos < pcm->size(); }
  // Writes the next `n` samples (silence once done) into `out`.
  void fill(std::int16_t* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      if (delay) {
        --delay;
        out[i] = 0;
      } else {
        out[i] = active() ? (*pcm)[pos++] : 0;
      }
    }
  }
};

// Two streams mixed together: the tap sound, and the voice line that rides on its tail. A new
// tap sound cuts the old one off (taps can come quickly); a new voice line replaces the old.
void audio_task(void*) {
  std::map<std::string, std::vector<std::int16_t>> cache;  // synthesized beeps
  // Synthesize the tap sounds up front so the first tap isn't delayed.
  const std::int64_t t0 = esp_timer_get_time();
  for (const char* id : {"accept", "reject"}) cache.emplace(id, fx::synth_beep(id, kRate));
  ESP_LOGI(TAG, "tap sounds ready in %lld ms", (esp_timer_get_time() - t0) / 1000);
  load_voice_pack();

  std::vector<std::int16_t> a(kChunk), b(kChunk), out(kChunk);
  std::vector<std::int16_t> voice_pcm;
  Stream beep, voice;
  while (true) {
    const bool busy = beep.active() || voice.active();
    if (auto next = g_mailbox->take(busy ? 0 : portMAX_DELAY)) {
      const std::string& id = next->id;
      auto it = cache.find(id);
      if (it == cache.end()) it = cache.emplace(id, fx::synth_beep(id, kRate)).first;
      if (it->second.empty()) ESP_LOGW(TAG, "no sound for '%s'", id.c_str());
      beep = Stream{&it->second, 0, 0};
      ESP_LOGI(TAG, "SOUND %s", id.c_str());

      const bool accept = id == "accept" || id == "bonus", reject = id == "reject";
      if (g_voice_on && g_pack && (accept || reject)) {
        if (const fx::VoiceLine* line = pick_line(accept ? fx::LineKind::Accept : fx::LineKind::Reject)) {
          voice_pcm = fx::resample_linear(fx::adpcm_decode(line->data, line->bytes, line->samples),
                                          g_pack->sample_rate, kRate);
          voice = Stream{&voice_pcm, 0, kVoiceDelay};
          ESP_LOGI(TAG, "VOICE %s: %s", g_pack->voices[line->voice].c_str(), line->text.c_str());
        }
      }
    }
    if (!beep.active() && !voice.active()) continue;
    if (!g_tx) {  // no speaker: just finish the sounds
      beep = voice = Stream{};
      continue;
    }
    beep.fill(a.data(), kChunk);
    voice.fill(b.data(), kChunk);
    fx::mix(a.data(), kChunk, g_volume.load(), b.data(), kChunk, g_volume.load(), out.data(), kChunk);
    std::size_t written = 0;
    i2s_channel_write(g_tx, out.data(), kChunk * sizeof(std::int16_t), &written, pdMS_TO_TICKS(200));
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

void set_voice(const std::string& pack) {
  g_voice_on = pack == "profanity";
  ESP_LOGI(TAG, "voice lines: %s", pack.c_str());
}

void set_volume(int percent) { g_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent; }

}  // namespace orca::audio
