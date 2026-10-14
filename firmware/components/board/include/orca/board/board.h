#pragma once

// Elecrow CrowPanel 5.0" (ESP32-S3-WROOM-1-N4R8) pin map and bring-up.
// Pins from the Elecrow wiki, see docs/ARCHITECTURE.md §2.1.

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_LCD_H_RES 800
#define BOARD_LCD_V_RES 480

#define BOARD_PIN_BACKLIGHT 2
#define BOARD_PIN_I2C_SDA 19  // PN532 + (unused) GT911 touch
#define BOARD_PIN_I2C_SCL 20
#define BOARD_PIN_LED_DATA 38  // GPIO_D header, WS2812
#define BOARD_PIN_I2S_LRCLK 18
#define BOARD_PIN_I2S_BCLK 42
#define BOARD_PIN_I2S_DOUT 17
#define BOARD_PIN_SD_CS 10
#define BOARD_PIN_SD_MOSI 11
#define BOARD_PIN_SD_CLK 12
#define BOARD_PIN_SD_MISO 13

// Creates the RGB panel with two PSRAM framebuffers and an SRAM bounce buffer.
esp_err_t board_display_init(esp_lcd_panel_handle_t *out_panel);

// Returns the panel's framebuffers (RGB565, BOARD_LCD_H_RES * BOARD_LCD_V_RES pixels each).
esp_err_t board_display_framebuffers(esp_lcd_panel_handle_t panel, void **fb0, void **fb1);

// Backlight brightness, 0-100 %.
esp_err_t board_backlight_set(int percent);

#ifdef __cplusplus
}
#endif
