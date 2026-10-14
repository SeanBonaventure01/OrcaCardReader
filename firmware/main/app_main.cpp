// Phase 0 bring-up: cycle full-screen test colors and a channel-order pattern so the display
// configuration can be verified (by eye or by the webcam). Replaced by the real app in Phase 3.

#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orca/board/board.h"

namespace {

constexpr const char* TAG = "bringup";
constexpr int kPixels = BOARD_LCD_H_RES * BOARD_LCD_V_RES;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct TestColor {
  const char* name;
  uint16_t value;
};

constexpr TestColor kColors[] = {
    {"red", rgb565(255, 0, 0)},     {"green", rgb565(0, 255, 0)}, {"blue", rgb565(0, 0, 255)},
    {"white", rgb565(255, 255, 255)}, {"black", rgb565(0, 0, 0)},
};

void fill(uint16_t* fb, uint16_t color) {
  for (int i = 0; i < kPixels; ++i) fb[i] = color;
}

// Three vertical bands, red | green | blue from left to right.
void bands(uint16_t* fb) {
  constexpr uint16_t band[] = {rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255)};
  for (int y = 0; y < BOARD_LCD_V_RES; ++y) {
    for (int x = 0; x < BOARD_LCD_H_RES; ++x) {
      fb[y * BOARD_LCD_H_RES + x] = band[x * 3 / BOARD_LCD_H_RES];
    }
  }
}

}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "PSRAM free: %u KB, internal free: %u KB",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

  esp_lcd_panel_handle_t panel = nullptr;
  ESP_ERROR_CHECK(board_display_init(&panel));
  void* fbs[2] = {};
  ESP_ERROR_CHECK(board_display_framebuffers(panel, &fbs[0], &fbs[1]));
  ESP_ERROR_CHECK(board_backlight_set(60));
  ESP_LOGI(TAG, "READY");

  int next_fb = 0;
  auto show = [&](const char* name, auto draw) {
    auto* fb = static_cast<uint16_t*>(fbs[next_fb]);
    draw(fb);
    // Drawing a whole framebuffer by its own pointer makes the driver flip to it.
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, fb));
    next_fb ^= 1;
    ESP_LOGI(TAG, "SHOW %s", name);
    vTaskDelay(pdMS_TO_TICKS(3000));
  };

  while (true) {
    for (const auto& c : kColors) {
      show(c.name, [&](uint16_t* fb) { fill(fb, c.value); });
    }
    show("bands_rgb", bands);
  }
}
