#include "orca/board/board.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"

static const char *TAG = "board";

#define LCD_PCLK_HZ (15 * 1000 * 1000)
#define BOUNCE_BUFFER_LINES 10

#define BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_DUTY_BITS LEDC_TIMER_10_BIT

static bool s_backlight_ready;

static esp_err_t backlight_init(void) {
  const ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = BACKLIGHT_DUTY_BITS,
      .timer_num = BACKLIGHT_LEDC_TIMER,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
  const ledc_channel_config_t channel = {
      .gpio_num = BOARD_PIN_BACKLIGHT,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = BACKLIGHT_LEDC_CHANNEL,
      .timer_sel = BACKLIGHT_LEDC_TIMER,
      .duty = 0,
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel");
  s_backlight_ready = true;
  return ESP_OK;
}

esp_err_t board_backlight_set(int percent) {
  if (!s_backlight_ready) {
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight init");
  }
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const uint32_t max_duty = (1u << BACKLIGHT_DUTY_BITS) - 1;
  ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL, max_duty * percent / 100),
                      TAG, "backlight duty");
  return ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL);
}

esp_err_t board_display_init(esp_lcd_panel_handle_t *out_panel) {
  const esp_lcd_rgb_panel_config_t config = {
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .timings =
          {
              .pclk_hz = LCD_PCLK_HZ,
              .h_res = BOARD_LCD_H_RES,
              .v_res = BOARD_LCD_V_RES,
              .hsync_pulse_width = 4,
              .hsync_back_porch = 43,
              .hsync_front_porch = 8,
              .vsync_pulse_width = 4,
              .vsync_back_porch = 12,
              .vsync_front_porch = 8,
              .flags = {.pclk_active_neg = 1},
          },
      .data_width = 16,
      .bits_per_pixel = 16,
      .num_fbs = 2,
      .bounce_buffer_size_px = BOARD_LCD_H_RES * BOUNCE_BUFFER_LINES,
      .dma_burst_size = 64,
      .hsync_gpio_num = 39,
      .vsync_gpio_num = 41,
      .de_gpio_num = 40,
      .pclk_gpio_num = 0,
      .disp_gpio_num = -1,
      // D0-D4 = B0-B4, D5-D10 = G0-G5, D11-D15 = R0-R4 (RGB565)
      .data_gpio_nums = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14},
      .flags = {.fb_in_psram = 1},
  };

  esp_lcd_panel_handle_t panel = NULL;
  ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&config, &panel), TAG, "new rgb panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
  *out_panel = panel;
  return ESP_OK;
}

esp_err_t board_display_framebuffers(esp_lcd_panel_handle_t panel, void **fb0, void **fb1) {
  return esp_lcd_rgb_panel_get_frame_buffer(panel, 2, fb0, fb1);
}
