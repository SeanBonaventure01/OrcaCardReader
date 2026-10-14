#include "orca/leds/leds.hpp"

#include <atomic>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "orca/device/mailbox.hpp"
#include "orca/fx/leds.hpp"

namespace orca::leds {

namespace {

constexpr const char* TAG = "leds";
constexpr int kFrameMs = 20;  // 50 fps

device::Mailbox<hal::LedCmd>* g_mailbox = nullptr;
led_strip_handle_t g_strip = nullptr;
std::atomic<int> g_count{0};
std::atomic<int> g_brightness{25};
std::atomic<int> g_max_ma{450};
std::function<void(std::uint8_t, std::uint8_t, std::uint8_t)> g_preview;

void led_task(void*) {
  fx::LedAnimator anim;
  anim.set_base("idle_breathe", 0);
  fx::Frame frame;
  while (true) {
    const std::int64_t now = esp_timer_get_time() / 1000;
    while (auto cmd = g_mailbox->take(0)) anim.play(cmd->effect, cmd->duration_ms, now);

    const int count = g_count.load();
    frame.assign(count > 0 ? count : 1, fx::Rgb{});
    anim.render_at(now, frame);
    fx::apply_brightness(frame, g_brightness.load());
    fx::limit_current(frame, g_max_ma.load());

    if (g_preview) {
      // The preview shows the effect at full brightness so it's visible on screen.
      fx::Frame preview(1);
      anim.render_at(now, preview);
      g_preview(preview[0].r, preview[0].g, preview[0].b);
    }
    if (g_strip && count > 0) {
      for (int i = 0; i < count; ++i) led_strip_set_pixel(g_strip, i, frame[i].r, frame[i].g, frame[i].b);
      led_strip_refresh(g_strip);
    }
    vTaskDelay(pdMS_TO_TICKS(anim.animating(now) ? kFrameMs : 100));
  }
}

}  // namespace

esp_err_t start(int gpio, const config::Settings& s, std::function<void(std::uint8_t, std::uint8_t, std::uint8_t)> preview) {
  g_preview = std::move(preview);
  apply_settings(s);
  led_strip_config_t strip_cfg = {};
  strip_cfg.strip_gpio_num = gpio;
  strip_cfg.max_leds = 300;
  strip_cfg.led_model = LED_MODEL_WS2812;
  strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  led_strip_rmt_config_t rmt_cfg = {};
  rmt_cfg.resolution_hz = 10 * 1000 * 1000;
  rmt_cfg.flags.with_dma = false;
  if (esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &g_strip); err != ESP_OK) {
    ESP_LOGW(TAG, "LED strip unavailable (%s); preview only", esp_err_to_name(err));
    g_strip = nullptr;
  }
  g_mailbox = new device::Mailbox<hal::LedCmd>(8);
  xTaskCreatePinnedToCore(led_task, "leds", 4096, nullptr, 3, nullptr, 0);
  ESP_LOGI(TAG, "LEDs on GPIO %d, %d pixels", gpio, g_count.load());
  return ESP_OK;
}

void play(const hal::LedCmd& cmd) {
  if (g_mailbox) g_mailbox->post(cmd);
}

void apply_settings(const config::Settings& s) {
  g_count = s.led_count;
  g_brightness = s.led_brightness;
  g_max_ma = s.led_max_ma;
}

}  // namespace orca::leds
