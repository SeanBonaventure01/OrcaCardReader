#include "orca/display/display.hpp"

#include <atomic>
#include <cctype>
#include <cmath>
#include <variant>

#include "assets/orca_splash.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "orca/board/board.h"
#include "orca/device/mailbox.hpp"
#include "orca/fx/particles.hpp"
#include "orca/fx/rainbow.hpp"
#include "sdkconfig.h"

extern const std::uint8_t orca_splash_start[] asm("_binary_orca_splash_rgb565_start");

namespace orca::display {

namespace {

constexpr const char* TAG = "display";

constexpr int kBarH = 40;  // status bar height
constexpr std::uint32_t kBlack = 0x000000;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kGray = 0xA8A8A8;
constexpr std::uint32_t kGreen = 0x3DC24F;  // the reader's check mark
constexpr std::uint32_t kGold = 0xF5C33B;   // the reader's arrow
constexpr std::uint32_t kAmber = 0xF5A524;
constexpr std::uint32_t kRed = 0xE5484D;

struct BoardMsg {
  std::vector<BoardLine> lines;
};
struct FooterMsg {
  std::string text;
};
struct ClockMsg {
  std::int64_t epoch_ms;
  int utc_offset_min;
};
using UiMsg = std::variant<hal::ScreenCmd, BoardMsg, FooterMsg, ClockMsg>;

device::Mailbox<UiMsg>* g_mailbox = nullptr;
std::atomic<std::uint32_t> g_led_rgb{0};
std::atomic<bool> g_effects{true};

// ---- LVGL objects: only the UI task touches these, under the port lock
lv_obj_t* bar_date;
lv_obj_t* bar_label;
lv_obj_t* bar_time;

lv_obj_t* idle_scr;
lv_obj_t* idle_status;  // "Not in service" etc., covers the arrow
lv_obj_t* idle_status_text;

lv_obj_t* board_scr;
lv_obj_t* board_list;
lv_obj_t* board_footer;

lv_obj_t* result_scr;
lv_obj_t* icon_check;
lv_obj_t* icon_x[2];
lv_obj_t* icon_clock;
lv_obj_t* icon_ring;  // neutral: circle with a slash
lv_obj_t* result_headline;
lv_obj_t* result_detail;

lv_obj_t* banner;
lv_obj_t* banner_headline;
lv_obj_t* banner_detail;
lv_obj_t* fault_scr;
lv_obj_t* fault_detail;
[[maybe_unused]] lv_obj_t* led_bar;

std::string game_label;  // status bar centre: the game's name, or the active event
std::string event_note;
bool taking_fares = false;
bool have_board = false;
std::int64_t clock_offset_s = 0;  // local seconds = esp_timer seconds + offset
bool clock_set = false;
std::int64_t result_until = 0;
std::int64_t banner_until = 0;
std::int64_t next_board_at = 0;
std::int64_t board_until = 0;

// Points for the drawn icons (LVGL keeps pointers, so these must outlive the objects).
lv_point_precise_t check_pts[] = {{0, 50}, {36, 86}, {110, 6}};
lv_point_precise_t x1_pts[] = {{0, 0}, {84, 84}};
lv_point_precise_t x2_pts[] = {{84, 0}, {0, 84}};
lv_point_precise_t hands_pts[] = {{46, 18}, {46, 46}, {66, 58}};
lv_point_precise_t slash_pts[] = {{16, 16}, {76, 76}};

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, std::uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, "");
  return l;
}

lv_obj_t* screen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, lv_color_hex(kBlack), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(s, false);
  return s;
}

lv_obj_t* line(lv_obj_t* parent, lv_point_precise_t* pts, int n, std::uint32_t color, int width) {
  lv_obj_t* l = lv_line_create(parent);
  lv_line_set_points(l, pts, n);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
  return l;
}

// A plain ring (used for the clock and "not in service" icons).
lv_obj_t* ring(lv_obj_t* parent, std::uint32_t color) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_set_size(r, 92, 92);
  lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(r, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(r, 12, 0);
  lv_obj_set_style_pad_all(r, 0, 0);
  lv_obj_set_scrollable(r, false);
  lv_obj_align(r, LV_ALIGN_TOP_MID, 0, kBarH + 56);
  return r;
}

void build_status_bar() {
  // On the top layer so it shows over every screen, like the real reader.
  lv_obj_t* top = lv_layer_top();
  bar_date = label(top, &lv_font_montserrat_20, kWhite);
  lv_obj_align(bar_date, LV_ALIGN_TOP_LEFT, 24, 10);
  bar_label = label(top, &lv_font_montserrat_14, kGray);
  lv_obj_align(bar_label, LV_ALIGN_TOP_MID, 0, 14);
  bar_time = label(top, &lv_font_montserrat_20, kWhite);
  lv_obj_align(bar_time, LV_ALIGN_TOP_RIGHT, -24, 10);
}

void build_idle() {
  idle_scr = screen();
  static lv_image_dsc_t splash = {};
  splash.header.magic = LV_IMAGE_HEADER_MAGIC;
  splash.header.cf = LV_COLOR_FORMAT_RGB565;
  splash.header.w = ORCA_SPLASH_W;
  splash.header.h = ORCA_SPLASH_H;
  splash.header.stride = ORCA_SPLASH_W * 2;
  splash.data_size = ORCA_SPLASH_W * ORCA_SPLASH_H * 2;
  splash.data = orca_splash_start;
  lv_obj_t* img = lv_image_create(idle_scr);
  lv_image_set_src(img, &splash);
  lv_obj_align(img, LV_ALIGN_TOP_MID, 0, kBarH + (BOARD_LCD_V_RES - kBarH - ORCA_SPLASH_H) / 2);

  // Covers the yellow arrow when the reader isn't taking fares.
  idle_status = lv_obj_create(idle_scr);
  lv_obj_set_size(idle_status, 560, 96);
  lv_obj_align(idle_status, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_color(idle_status, lv_color_hex(kBlack), 0);
  lv_obj_set_style_border_width(idle_status, 0, 0);
  lv_obj_set_scrollable(idle_status, false);
  idle_status_text = label(idle_status, &lv_font_montserrat_48, kWhite);
  lv_obj_center(idle_status_text);
  lv_obj_set_hidden(idle_status, true);
}

void build_board() {
  board_scr = screen();
  lv_obj_t* title = label(board_scr, &lv_font_montserrat_28, kGold);
  lv_label_set_text(title, "Top riders");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kBarH + 20);
  board_list = label(board_scr, &lv_font_montserrat_28, kWhite);
  lv_obj_set_style_text_line_space(board_list, 14, 0);
  lv_obj_align(board_list, LV_ALIGN_TOP_MID, 0, kBarH + 80);
  board_footer = label(board_scr, &lv_font_montserrat_14, kGray);
  lv_obj_align(board_footer, LV_ALIGN_BOTTOM_MID, 0, -14);
}

void build_result() {
  result_scr = screen();
  // Icons share one spot above the text; only one is visible at a time.
  icon_check = line(result_scr, check_pts, 3, kGreen, 16);
  lv_obj_align(icon_check, LV_ALIGN_TOP_MID, 0, kBarH + 60);
  icon_x[0] = line(result_scr, x1_pts, 2, kRed, 16);
  icon_x[1] = line(result_scr, x2_pts, 2, kRed, 16);
  for (auto* x : icon_x) lv_obj_align(x, LV_ALIGN_TOP_MID, 0, kBarH + 64);
  icon_clock = ring(result_scr, kAmber);
  line(icon_clock, hands_pts, 3, kAmber, 10);
  lv_obj_align(lv_obj_get_child(icon_clock, 0), LV_ALIGN_TOP_LEFT, -12, -12);
  icon_ring = ring(result_scr, kGray);
  line(icon_ring, slash_pts, 2, kGray, 12);
  lv_obj_align(lv_obj_get_child(icon_ring, 0), LV_ALIGN_TOP_LEFT, -12, -12);

  result_headline = label(result_scr, &lv_font_montserrat_48, kWhite);
  lv_obj_set_width(result_headline, BOARD_LCD_H_RES - 40);
  lv_obj_set_style_text_align(result_headline, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(result_headline, LV_ALIGN_TOP_MID, 0, kBarH + 190);
  result_detail = label(result_scr, &lv_font_montserrat_28, kGray);
  lv_obj_set_width(result_detail, BOARD_LCD_H_RES - 40);
  lv_obj_set_style_text_align(result_detail, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(result_detail, 8, 0);
  lv_obj_align(result_detail, LV_ALIGN_TOP_MID, 0, kBarH + 270);
}

void build_fault() {
  fault_scr = screen();
  lv_obj_t* h = label(fault_scr, &lv_font_montserrat_48, kWhite);
  lv_label_set_text(h, "Out of service");
  lv_obj_align(h, LV_ALIGN_CENTER, 0, -30);
  fault_detail = label(fault_scr, &lv_font_montserrat_28, kGray);
  lv_obj_align(fault_detail, LV_ALIGN_CENTER, 0, 40);
}

void build_banner() {
  banner = lv_obj_create(lv_layer_top());
  lv_obj_set_size(banner, 720, 128);
  lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, kBarH + 8);
  lv_obj_set_style_radius(banner, 18, 0);
  lv_obj_set_style_bg_color(banner, lv_color_hex(kBlack), 0);
  lv_obj_set_style_border_color(banner, lv_color_hex(kGold), 0);
  lv_obj_set_style_border_width(banner, 4, 0);
  lv_obj_set_scrollable(banner, false);
  banner_headline = label(banner, &lv_font_montserrat_48, kGold);
  lv_obj_align(banner_headline, LV_ALIGN_TOP_MID, 0, -2);
  banner_detail = label(banner, &lv_font_montserrat_28, kWhite);
  lv_obj_align(banner_detail, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_hidden(banner, true);
}

// ---- confetti / fireworks: one small rectangle per particle slot, above every screen
fx::Particles* particles;
lv_obj_t* sparks[fx::Particles::kMax];
std::int64_t particles_ms = 0;  // when they were last stepped
bool sparks_shown = false;

void build_effects() {
  particles = new fx::Particles(BOARD_LCD_H_RES, BOARD_LCD_V_RES, static_cast<std::uint32_t>(esp_timer_get_time()));
  for (auto& o : sparks) {
    o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_remove_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_hidden(o, true);
  }
}

// ---- Rush Hour party mode: a rainbow "LED ring" chasing around the screen edge (above every
// screen), the event name and multiplier pulsing in the columns beside the artwork, and
// fireworks now and then while the reader is idle.
constexpr int kRingSegments = 40;  // 12 along the top and bottom, 8 down each side
constexpr int kRingThickness = 12;
lv_obj_t* rush_ring[kRingSegments];
lv_obj_t* rush_name[2];  // left and right columns
lv_obj_t* rush_mult[2];
bool rush_on = false;
std::int64_t rush_since = 0;
std::int64_t next_ambient_burst = 0;

void build_rush() {
  // Segments clockwise from the top-left corner.
  const int w = BOARD_LCD_H_RES, h = BOARD_LCD_V_RES, t = kRingThickness;
  int i = 0;
  auto seg = [&](int x, int y, int sw, int sh) {
    lv_obj_t* o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, sw, sh);
    lv_obj_set_hidden(o, true);
    rush_ring[i++] = o;
  };
  for (int k = 0; k < 12; ++k) seg(k * w / 12, 0, w / 12 + 1, t);                      // top, left to right
  for (int k = 0; k < 8; ++k) seg(w - t, t + k * (h - 2 * t) / 8, t, (h - 2 * t) / 8 + 1);  // right, down
  for (int k = 11; k >= 0; --k) seg(k * w / 12, h - t, w / 12 + 1, t);                 // bottom, right to left
  for (int k = 7; k >= 0; --k) seg(0, t + k * (h - 2 * t) / 8, t, (h - 2 * t) / 8 + 1);    // left, up

  for (int side = 0; side < 2; ++side) {
    const int x = side == 0 ? -(BOARD_LCD_H_RES / 2 - 82) : BOARD_LCD_H_RES / 2 - 82;
    rush_name[side] = label(idle_scr, &lv_font_montserrat_28, kWhite);
    lv_obj_set_style_text_align(rush_name[side], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(rush_name[side], LV_ALIGN_CENTER, x, -60);
    rush_mult[side] = label(idle_scr, &lv_font_montserrat_48, kGold);
    lv_obj_align(rush_mult[side], LV_ALIGN_CENTER, x, 40);
    lv_obj_set_hidden(rush_name[side], true);
    lv_obj_set_hidden(rush_mult[side], true);
  }
}

// "Rush hour 2x" -> name "RUSH\nHOUR" (one word per line, to fit a column) and "2x".
void set_rush_text(const std::string& note) {
  std::string name = note, mult;
  const auto sp = note.rfind(' ');
  if (sp != std::string::npos && note.size() > sp + 1 && note.back() == 'x') {
    name = note.substr(0, sp);
    mult = note.substr(sp + 1);
  }
  if (const auto plus = name.find(" + "); plus != std::string::npos) name = name.substr(0, plus);
  for (auto& ch : name) ch = ch == ' ' ? '\n' : static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  for (int side = 0; side < 2; ++side) {
    lv_label_set_text(rush_name[side], ascii_only(name).c_str());
    lv_label_set_text(rush_mult[side], ascii_only(mult).c_str());
    lv_obj_align(rush_name[side], LV_ALIGN_CENTER, side == 0 ? -(BOARD_LCD_H_RES / 2 - 82) : BOARD_LCD_H_RES / 2 - 82, -60);
  }
}

void set_rush(bool on, const std::string& note, std::int64_t now) {
  if (on) set_rush_text(note);
  if (on == rush_on) return;
  rush_on = on;
  rush_since = now;
  next_ambient_burst = now + 1500;
  for (auto* o : rush_ring) lv_obj_set_hidden(o, !on);
  for (int side = 0; side < 2; ++side) {
    lv_obj_set_hidden(rush_name[side], !on);
    lv_obj_set_hidden(rush_mult[side], !on);
  }
}

void tick_rush(std::int64_t now) {
  if (!rush_on) return;
  const auto colors = fx::rainbow_ring(kRingSegments, now - rush_since);
  for (int i = 0; i < kRingSegments; ++i) lv_obj_set_style_bg_color(rush_ring[i], lv_color_hex(colors[i]), 0);
  // The words cycle through the rainbow; the multiplier pulses bright and dim.
  const float t = static_cast<float>(now - rush_since) / 1000.0f;
  const lv_color_t name_color = lv_color_hex(fx::hsv(t * 0.5f, 0.8f, 1.0f));
  const lv_opa_t pulse = static_cast<lv_opa_t>(150 + 105 * (0.5f + 0.5f * std::sin(t * 6.0f)));
  for (int side = 0; side < 2; ++side) {
    lv_obj_set_style_text_color(rush_name[side], name_color, 0);
    lv_obj_set_style_text_opa(rush_mult[side], pulse, 0);
  }
  // Fireworks every few seconds while nobody's tapping (paid fares get their own).
  if (g_effects && now >= next_ambient_burst && lv_screen_active() == idle_scr && !particles->active()) {
    particles->fireworks(1, 20);
    particles_ms = now;
    next_ambient_burst = now + 3000;
  }
}

// A paid fare gets confetti or fireworks (at random); a bonus gets both.
void start_effects(hal::Tone tone, std::int64_t now) {
  if (!g_effects || (tone != hal::Tone::Success && tone != hal::Tone::Bonus)) return;
  particles->clear();
  const bool both = tone == hal::Tone::Bonus;
  const bool fireworks = both || (esp_timer_get_time() / 1000) % 2;
  if (fireworks) particles->fireworks(both ? 2 : 3, 24);
  if (!fireworks || both) particles->confetti(both ? 40 : 60);
  particles_ms = now;
}

void tick_effects(std::int64_t now) {
  if (!particles->active()) {
    if (sparks_shown) {
      for (auto& o : sparks) lv_obj_set_hidden(o, true);
      sparks_shown = false;
    }
    return;
  }
  particles->step(static_cast<float>(std::min<std::int64_t>(now - particles_ms, 100)) / 1000.0f);
  particles_ms = now;
  const auto& ps = particles->particles();
  for (std::size_t i = 0; i < fx::Particles::kMax; ++i) {
    if (i >= ps.size() || !fx::Particles::visible(ps[i])) {
      lv_obj_set_hidden(sparks[i], true);
      continue;
    }
    const auto& p = ps[i];
    const int w = static_cast<int>(fx::Particles::drawn_width(p)), h = static_cast<int>(p.h);
    lv_obj_set_size(sparks[i], w, h);
    lv_obj_set_pos(sparks[i], static_cast<int>(p.x) - w / 2, static_cast<int>(p.y) - h / 2);
    lv_obj_set_style_bg_color(sparks[i], lv_color_hex(p.color), 0);
    lv_obj_set_style_bg_opa(sparks[i], fx::Particles::opacity(p), 0);
    lv_obj_set_hidden(sparks[i], false);
  }
  sparks_shown = true;
}

void build_led_bar() {
#if CONFIG_ORCA_LED_PREVIEW
  led_bar = lv_obj_create(lv_layer_top());
  lv_obj_set_size(led_bar, BOARD_LCD_H_RES, 10);
  lv_obj_align(led_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(led_bar, 0, 0);
  lv_obj_set_style_border_width(led_bar, 0, 0);
#endif
}

void show_icon(hal::Tone tone) {
  const bool check = tone == hal::Tone::Success || tone == hal::Tone::Bonus;
  lv_obj_set_hidden(icon_check, !check);
  lv_obj_set_style_line_color(icon_check, lv_color_hex(tone == hal::Tone::Bonus ? kGold : kGreen), 0);
  for (auto* x : icon_x) lv_obj_set_hidden(x, tone != hal::Tone::Error);
  lv_obj_set_hidden(icon_clock, tone != hal::Tone::Warning);
  lv_obj_set_hidden(icon_ring, tone != hal::Tone::Neutral);
}

void update_bar_label() {
  const bool event = taking_fares && !event_note.empty();
  lv_label_set_text(bar_label, ascii_only(event ? event_note : game_label).c_str());
  lv_obj_set_style_text_color(bar_label, lv_color_hex(event ? kGold : kGray), 0);
  lv_obj_set_style_text_font(bar_label, event ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
  lv_obj_align(bar_label, LV_ALIGN_TOP_MID, 0, event ? 10 : 14);
}

void apply(const hal::ScreenCmd& c, std::int64_t now) {
  switch (c.kind) {
    case hal::ScreenKind::Idle:
      set_rush(c.rush, c.note, now);
      game_label = c.headline;
      event_note = c.note;
      taking_fares = c.detail.empty();
      update_bar_label();
      lv_obj_set_hidden(idle_status, taking_fares);
      lv_label_set_text(idle_status_text, ascii_only(c.detail).c_str());
      if (now >= result_until && now >= board_until) lv_screen_load(idle_scr);
      break;
    case hal::ScreenKind::Result: {
      start_effects(c.tone, now);
      show_icon(c.tone);
      lv_label_set_text(result_headline, ascii_only(c.headline).c_str());
      std::string detail = c.detail;
      if (!c.player_label.empty()) detail += (detail.empty() ? "" : "\n") + c.player_label;
      lv_label_set_text(result_detail, ascii_only(detail).c_str());
      lv_screen_load(result_scr);
      result_until = now + (c.hold_ms ? c.hold_ms : 2500);
      board_until = 0;
      // The tap result matters most: don't let an announcement cover it.
      banner_until = 0;
      lv_obj_set_hidden(banner, true);
      break;
    }
    case hal::ScreenKind::Banner:
      lv_label_set_text(banner_headline, ascii_only(c.headline).c_str());
      lv_label_set_text(banner_detail, ascii_only(c.detail).c_str());
      lv_obj_set_hidden(banner, false);
      banner_until = now + (c.hold_ms ? c.hold_ms : 4000);
      break;
    case hal::ScreenKind::Fault:
      lv_label_set_text(fault_detail, ascii_only(c.detail).c_str());
      lv_screen_load(fault_scr);
      result_until = INT64_MAX;
      break;
  }
}

void tick_clock(std::int64_t now_ms) {
  if (!clock_set) return;
  const std::int64_t local = now_ms / 1000 + clock_offset_s;
  static std::int64_t shown_minute = -1;
  if (local / 60 == shown_minute) return;
  shown_minute = local / 60;
  lv_label_set_text(bar_date, format_date(local).c_str());
  lv_label_set_text(bar_time, format_time(local).c_str());
}

// While a game runs, briefly show the top riders now and then.
void tick_board_rotation(std::int64_t now) {
  const int every = CONFIG_ORCA_BOARD_ROTATION_SECONDS;
  if (board_until && now >= board_until) {
    board_until = 0;
    if (now >= result_until) lv_screen_load(idle_scr);
  }
  if (every <= 0 || !taking_fares || !have_board || now < result_until) return;
  if (next_board_at == 0) next_board_at = now + every * 1000LL;
  if (now >= next_board_at && !board_until) {
    lv_screen_load(board_scr);
    board_until = now + 6000;
    next_board_at = now + every * 1000LL;
  }
}

void ui_task(void*) {
  [[maybe_unused]] std::uint32_t shown_led = UINT32_MAX;
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
          char row[80];
          std::snprintf(row, sizeof row, "%d.  %s  -  %lld pts\n", l.rank, ascii_only(l.name).c_str(),
                        static_cast<long long>(l.points));
          text += row;
        }
        have_board = !b->lines.empty();
        if (!text.empty()) text.pop_back();
        lv_label_set_text(board_list, text.c_str());
      } else if (auto* f = std::get_if<FooterMsg>(msg.get())) {
        lv_label_set_text(board_footer, ascii_only(f->text).c_str());
      } else if (auto* k = std::get_if<ClockMsg>(msg.get())) {
        clock_offset_s = k->epoch_ms / 1000 + k->utc_offset_min * 60 - now / 1000;
        clock_set = true;
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
    tick_clock(now);
    tick_board_rotation(now);
    tick_rush(now);
    tick_effects(now);
#if CONFIG_ORCA_LED_PREVIEW
    const std::uint32_t rgb = g_led_rgb.load();
    if (rgb != shown_led) {
      shown_led = rgb;
      lv_obj_set_style_bg_color(led_bar, lv_color_hex(rgb), 0);
    }
#endif
    lvgl_port_unlock();
  }
}

// Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm).
void civil_from_days(std::int64_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int>(yy + (m <= 2));
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
      out.push_back('?');  // start of another multi-byte character; continuation bytes are skipped
    }
  }
  return out;
}

std::string format_date(std::int64_t local_seconds) {
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int y;
  unsigned m, d;
  const std::int64_t days = local_seconds >= 0 ? local_seconds / 86400 : (local_seconds - 86399) / 86400;
  civil_from_days(days, y, m, d);
  char buf[24];
  std::snprintf(buf, sizeof buf, "%s %u, %d", months[m - 1], d, y);
  return buf;
}

std::string format_time(std::int64_t local_seconds) {
  const int minute_of_day = static_cast<int>(((local_seconds / 60) % 1440 + 1440) % 1440);
  const int h24 = minute_of_day / 60;
  char buf[16];
  std::snprintf(buf, sizeof buf, "%d:%02d %s", h24 % 12 == 0 ? 12 : h24 % 12, minute_of_day % 60, h24 < 12 ? "am" : "pm");
  return buf;
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
  build_board();
  build_result();
  build_fault();
  build_status_bar();
  build_banner();
  build_led_bar();
  build_rush();     // ring under the sparks: created first, so drawn first
  build_effects();
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

void set_effects(bool on) { g_effects = on; }

void set_wall_clock(std::int64_t epoch_ms, int utc_offset_min) {
  if (g_mailbox) g_mailbox->post(UiMsg{ClockMsg{epoch_ms, utc_offset_min}});
}

void set_led_preview(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  g_led_rgb.store(std::uint32_t(r) << 16 | std::uint32_t(g) << 8 | b);
}

}  // namespace orca::display
