#include "orca/display/display.hpp"

#include <atomic>
#include <mutex>
#include <variant>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "orca/board/board.h"
#include "orca/device/mailbox.hpp"

namespace orca::display {

namespace {

constexpr const char* TAG = "display";

// ORCA palette
constexpr std::uint32_t kNavy = 0x0B1D2A;
constexpr std::uint32_t kPanel = 0x12293A;
constexpr std::uint32_t kTeal = 0x00A7B5;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kMuted = 0x8FA7B5;

std::uint32_t tone_color(hal::Tone t) {
  switch (t) {
    case hal::Tone::Success: return 0x1FBF5B;
    case hal::Tone::Bonus: return 0xF7C948;
    case hal::Tone::Warning: return 0xF5A524;
    case hal::Tone::Error: return 0xE5484D;
    case hal::Tone::Neutral: return 0x3A5568;
  }
  return 0x3A5568;
}

// Dark text on light tones, white on dark ones.
std::uint32_t tone_text(hal::Tone t) {
  return (t == hal::Tone::Bonus || t == hal::Tone::Warning || t == hal::Tone::Success) ? 0x06140C : kWhite;
}

struct BoardMsg {
  std::vector<BoardLine> lines;
};
struct FooterMsg {
  std::string text;
};
using UiMsg = std::variant<hal::ScreenCmd, BoardMsg, FooterMsg>;

device::Mailbox<UiMsg>* g_mailbox = nullptr;
std::atomic<std::uint32_t> g_led_rgb{0};

// --- LVGL objects (only touched by the UI task, under the port lock)
lv_obj_t* idle_scr;
lv_obj_t* idle_game;
lv_obj_t* idle_detail;  // big prompt: TAP CARD HERE / PAUSED / ...
lv_obj_t* idle_note;    // active event, gold
lv_obj_t* idle_board;
lv_obj_t* idle_footer;
lv_obj_t* idle_card;  // animated "tap here" card
lv_obj_t* result_scr;
lv_obj_t* result_headline;
lv_obj_t* result_detail;
lv_obj_t* result_player;
lv_obj_t* banner;
lv_obj_t* banner_headline;
lv_obj_t* banner_detail;
lv_obj_t* fault_scr;
lv_obj_t* fault_detail;
lv_obj_t* led_bars[3];  // one per screen so the preview is always visible

std::int64_t result_until = 0;
std::int64_t banner_until = 0;

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, std::uint32_t color, lv_align_t align, int x, int y) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, "");
  lv_obj_align(l, align, x, y);
  return l;
}

lv_obj_t* screen(std::uint32_t bg) {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(s, false);
  return s;
}

lv_obj_t* led_bar(lv_obj_t* parent) {
  lv_obj_t* bar = lv_obj_create(parent);
  lv_obj_set_size(bar, BOARD_LCD_H_RES, 14);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
  return bar;
}

void card_anim(void* obj, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(obj), v); }

void build_idle() {
  idle_scr = screen(kNavy);

  // ORCA wordmark
  lv_obj_t* logo = lv_obj_create(idle_scr);
  lv_obj_set_size(logo, 150, 56);
  lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 24, 20);
  lv_obj_set_style_bg_color(logo, lv_color_hex(kTeal), 0);
  lv_obj_set_style_radius(logo, 12, 0);
  lv_obj_set_style_border_width(logo, 0, 0);
  lv_obj_t* logo_text = label(logo, &lv_font_montserrat_28, kNavy, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(logo_text, "ORCA");

  idle_game = label(idle_scr, &lv_font_montserrat_28, kWhite, LV_ALIGN_TOP_LEFT, 196, 32);

  // "Tap here" target with a bobbing card.
  lv_obj_t* target = lv_obj_create(idle_scr);
  lv_obj_set_size(target, 300, 200);
  lv_obj_align(target, LV_ALIGN_LEFT_MID, 70, 10);
  lv_obj_set_style_bg_color(target, lv_color_hex(kPanel), 0);
  lv_obj_set_style_border_color(target, lv_color_hex(kTeal), 0);
  lv_obj_set_style_border_width(target, 4, 0);
  lv_obj_set_style_radius(target, 24, 0);
  lv_obj_set_scrollable(target, false);
  idle_card = lv_obj_create(target);
  lv_obj_set_size(idle_card, 130, 82);
  lv_obj_set_style_bg_color(idle_card, lv_color_hex(kTeal), 0);
  lv_obj_set_style_radius(idle_card, 10, 0);
  lv_obj_set_style_border_width(idle_card, 0, 0);
  lv_obj_set_x(idle_card, 70);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, idle_card);
  lv_anim_set_exec_cb(&a, card_anim);
  lv_anim_set_values(&a, 0, 40);
  lv_anim_set_duration(&a, 900);
  lv_anim_set_playback_duration(&a, 900);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);

  idle_detail = label(idle_scr, &lv_font_montserrat_48, kWhite, LV_ALIGN_LEFT_MID, 40, 150);
  idle_note = label(idle_scr, &lv_font_montserrat_28, 0xF7C948, LV_ALIGN_TOP_LEFT, 196, 66);

  // Top riders
  lv_obj_t* board_title = label(idle_scr, &lv_font_montserrat_20, kMuted, LV_ALIGN_TOP_RIGHT, -40, 110);
  lv_label_set_text(board_title, "TOP RIDERS");
  idle_board = label(idle_scr, &lv_font_montserrat_28, kWhite, LV_ALIGN_TOP_RIGHT, -40, 145);
  lv_obj_set_style_text_align(idle_board, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_line_space(idle_board, 10, 0);

  idle_footer = label(idle_scr, &lv_font_montserrat_20, kMuted, LV_ALIGN_BOTTOM_LEFT, 24, -24);
  led_bars[0] = led_bar(idle_scr);
}

void build_result() {
  result_scr = screen(tone_color(hal::Tone::Success));
  result_headline = label(result_scr, &lv_font_montserrat_48, kWhite, LV_ALIGN_CENTER, 0, -70);
  result_detail = label(result_scr, &lv_font_montserrat_28, kWhite, LV_ALIGN_CENTER, 0, 10);
  result_player = label(result_scr, &lv_font_montserrat_28, kWhite, LV_ALIGN_CENTER, 0, 80);
  led_bars[1] = led_bar(result_scr);
}

void build_fault() {
  fault_scr = screen(0x5A1418);
  lv_obj_t* h = label(fault_scr, &lv_font_montserrat_48, kWhite, LV_ALIGN_CENTER, 0, -40);
  lv_label_set_text(h, "OUT OF SERVICE");
  fault_detail = label(fault_scr, &lv_font_montserrat_28, kWhite, LV_ALIGN_CENTER, 0, 30);
  led_bars[2] = led_bar(fault_scr);
}

void build_banner() {
  // On the top layer so it shows over whichever screen is active.
  banner = lv_obj_create(lv_layer_top());
  lv_obj_set_size(banner, 720, 130);
  lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_set_style_radius(banner, 16, 0);
  lv_obj_set_style_border_width(banner, 0, 0);
  lv_obj_set_scrollable(banner, false);
  banner_headline = label(banner, &lv_font_montserrat_48, kNavy, LV_ALIGN_TOP_MID, 0, 0);
  banner_detail = label(banner, &lv_font_montserrat_28, kNavy, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_hidden(banner, true);
}

void apply(const hal::ScreenCmd& c, std::int64_t now) {
  switch (c.kind) {
    case hal::ScreenKind::Idle:
      lv_label_set_text(idle_game, ascii_only(c.headline).c_str());
      lv_label_set_text(idle_detail, ascii_only(c.detail).c_str());
      lv_label_set_text(idle_note, ascii_only(c.note).c_str());
      if (now >= result_until) lv_screen_load(idle_scr);
      break;
    case hal::ScreenKind::Result:
      lv_obj_set_style_bg_color(result_scr, lv_color_hex(tone_color(c.tone)), 0);
      for (lv_obj_t* l : {result_headline, result_detail, result_player})
        lv_obj_set_style_text_color(l, lv_color_hex(tone_text(c.tone)), 0);
      lv_label_set_text(result_headline, ascii_only(c.headline).c_str());
      lv_label_set_text(result_detail, ascii_only(c.detail).c_str());
      lv_label_set_text(result_player, ascii_only(c.player_label).c_str());
      lv_screen_load(result_scr);
      result_until = now + (c.hold_ms ? c.hold_ms : 2500);
      break;
    case hal::ScreenKind::Banner:
      lv_obj_set_style_bg_color(banner, lv_color_hex(tone_color(c.tone)), 0);
      lv_label_set_text(banner_headline, ascii_only(c.headline).c_str());
      lv_label_set_text(banner_detail, ascii_only(c.detail).c_str());
      lv_obj_set_hidden(banner, false);
      banner_until = now + (c.hold_ms ? c.hold_ms : 4000);
      break;
    case hal::ScreenKind::Fault:
      lv_label_set_text(fault_detail, ascii_only(c.detail).c_str());
      lv_screen_load(fault_scr);
      result_until = INT64_MAX;  // stays until another screen replaces it
      break;
  }
}

void ui_task(void*) {
  std::uint32_t shown_led = UINT32_MAX;
  while (true) {
    auto msg = g_mailbox->take(pdMS_TO_TICKS(30));
    const std::int64_t now = esp_timer_get_time() / 1000;
    if (!lvgl_port_lock(100)) continue;
    if (msg) {
      if (auto* c = std::get_if<hal::ScreenCmd>(msg.get())) {
        apply(*c, now);
      } else if (auto* b = std::get_if<BoardMsg>(msg.get())) {
        std::string text;
        for (const auto& l : b->lines) {
          char line[64];
          std::snprintf(line, sizeof line, "%d. %s   %lld pts\n", l.rank, ascii_only(l.name).c_str(),
                        static_cast<long long>(l.points));
          text += line;
        }
        lv_label_set_text(idle_board, text.empty() ? "Be the first!" : text.c_str());
      } else if (auto* f = std::get_if<FooterMsg>(msg.get())) {
        lv_label_set_text(idle_footer, ascii_only(f->text).c_str());
      }
    }
    if (result_until != INT64_MAX && result_until && now >= result_until) {
      result_until = 0;
      lv_screen_load(idle_scr);
    }
    if (banner_until && now >= banner_until) {
      banner_until = 0;
      lv_obj_set_hidden(banner, true);
    }
    const std::uint32_t rgb = g_led_rgb.load();
    if (rgb != shown_led) {
      shown_led = rgb;
      for (lv_obj_t* bar : led_bars) lv_obj_set_style_bg_color(bar, lv_color_hex(rgb), 0);
    }
    lvgl_port_unlock();
  }
}

}  // namespace

std::string ascii_only(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xB7) {
      out.push_back('-');  // middle dot
      ++i;
    } else if ((c & 0xC0) == 0xC0) {
      out.push_back('?');  // start of another multi-byte character; skip its continuation bytes
    }
  }
  return out;
}

esp_err_t start(esp_lcd_panel_handle_t panel) {
  lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  port_cfg.task_affinity = 1;  // keep rendering off the Wi-Fi core
  port_cfg.task_priority = 4;
  ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port init");

  lvgl_port_display_cfg_t disp_cfg = {};
  disp_cfg.panel_handle = panel;
  disp_cfg.buffer_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES;
  disp_cfg.double_buffer = true;
  disp_cfg.hres = BOARD_LCD_H_RES;
  disp_cfg.vres = BOARD_LCD_V_RES;
  disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
  disp_cfg.flags.buff_spiram = 1;
  disp_cfg.flags.direct_mode = 1;
  lvgl_port_display_rgb_cfg_t rgb_cfg = {};
  rgb_cfg.flags.bb_mode = 1;
  rgb_cfg.flags.avoid_tearing = 1;
  if (!lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg)) return ESP_FAIL;

  if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
  build_idle();
  build_result();
  build_fault();
  build_banner();
  lv_label_set_text(idle_board, "Be the first!");
  lv_screen_load(idle_scr);
  lvgl_port_unlock();

  g_mailbox = new device::Mailbox<UiMsg>(16);
  xTaskCreatePinnedToCore(ui_task, "ui", 6144, nullptr, 3, nullptr, 1);
  ESP_LOGI(TAG, "UI started");
  return ESP_OK;
}

void show(const hal::ScreenCmd& cmd) {
  if (g_mailbox && !g_mailbox->post(UiMsg{cmd})) ESP_LOGW(TAG, "UI mailbox full, dropped screen");
}

void set_board(std::vector<BoardLine> top) {
  if (g_mailbox) g_mailbox->post(UiMsg{BoardMsg{std::move(top)}});
}

void set_footer(const std::string& text) {
  if (g_mailbox) g_mailbox->post(UiMsg{FooterMsg{text}});
}

void set_led_preview(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  g_led_rgb.store(std::uint32_t(r) << 16 | std::uint32_t(g) << 8 | b);
}

}  // namespace orca::display
