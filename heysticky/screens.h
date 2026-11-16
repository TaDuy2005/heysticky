// HeySticky screens: 01 Home, 01a Apps, 04j Web setup. Coordinates from Figma frames 4:2 / 35:521.
#pragma once
#include <qrcode.h>  // esp_qrcode, bundled with the ESP32 core
#include "ui.h"
#include "kbd.h"
#include "net.h"

enum Screen : uint8_t { SCR_HOME, SCR_APPS, SCR_WEBSETUP, SCR_NEWTASK };
inline Screen g_screen = SCR_HOME;

// ---------- shared state shown on Home ----------
struct HomeState { String date, clock, indoor, humidity; int battery = -2; bool wifi = false; uint32_t wxAt = 1; };
inline HomeState g_shown;  // what is currently on the panel

inline String fmt_date(const tm &t) {
  static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                                 "August", "September", "October", "November", "December"};
  return String(DAYS[t.tm_wday]) + ", " + t.tm_mday + " " + MONTHS[t.tm_mon];
}

inline String now_clock(String *date = nullptr) {
  tm t;
  if (!getLocalTime(&t, 0)) { if (date) *date = ""; return "--:--"; }
  char hm[6];
  strftime(hm, sizeof hm, "%H:%M", &t);
  if (date) *date = fmt_date(t);
  return hm;
}

inline const Icon &wx_icon(WxIcon i) {
  switch (i) {
    case WX_SUN: return ICON_WX_SUN;
    case WX_PARTLY: return ICON_WX_PARTLY;
    case WX_FOG: return ICON_WX_FOG;
    case WX_RAIN: return ICON_WX_RAIN;
    case WX_SNOW: return ICON_WX_SNOW;
    case WX_STORM: return ICON_WX_STORM;
    default: return ICON_WX_CLOUD;
  }
}

inline void read_indoor(String &temp, String &hum) {
  float c, rh;
  if (!hw_sht40_read(c, rh)) { temp = "--"; hum = "--"; return; }
  const bool f = cfg_get("units", "C") == "F";
  temp = String(f ? c * 9 / 5 + 32 : c, 1) + (f ? "°F" : "°C");
  hum = String(lroundf(rh)) + "%";
}

// ---------- Home pieces (each draws its own area) ----------
inline void home_clock(const String &date, const String &clock) {
  display.fillRect(40, 104, 400, 176, PAPER);
  ui_text(F_R30, date, W / 2, 104, 40, AL_CENTER);
  ui_text(F_X120, clock, W / 2, 144, 126, AL_CENTER);
}

inline void home_weather() {
  ui_card(40, 288, 400, 143, 16, 3);
  const String units = cfg_get("units", "C") == "F" ? "°F" : "°";
  if (g_wx.ok) {
    const WxInfo wi = wx_info(g_wx.code);
    ui_icon(wx_icon(wi.icon), 61, 336);
    ui_text(F_B25, wi.label, 121, 309, 33);
    ui_text(F_R25, "H " + String(lroundf(g_wx.hi)) + "°  L " + String(lroundf(g_wx.lo)) + "°", 121, 344, 33);
    ui_text(F_X48, String(lroundf(g_wx.temp)) + "°", 419, 328, 63, AL_RIGHT);
  } else {
    ui_icon(ICON_WX_CLOUD, 61, 336);
    ui_text(F_B25, "Offline", 121, 309, 33);
    ui_text(F_R25, "No weather yet", 121, 344, 33);
  }
  ui_text(F_R25, cfg_get("city"), 121, 377, 33);
}

inline void home_indoor(const String &temp, const String &hum) {
  ui_card(40, 445, 193, 102, 12, 2);
  ui_icon(ICON_THERMO, 58, 479);
  ui_text(F_X28, temp, 104, 461, 37);
  ui_text(F_R25, "Indoor", 104, 498, 33);
  ui_card(247, 445, 193, 102, 12, 2);
  ui_icon(ICON_DROP, 265, 479);
  ui_text(F_X28, hum, 311, 461, 37);
  ui_text(F_R25, "Humidity", 311, 498, 33);
}

inline void home_today(int n) {  // ponytail: always 0 until To-do lands (TASK-08)
  ui_card(40, 561, 400, 72, 12, 2);
  ui_icon(n ? ICON_CHECKLIST : ICON_CHECK_CIRCLE, 58, 584);
  ui_text(F_X30, n == 0 ? "No tasks today" : String(n) + (n == 1 ? " task today" : " tasks today"), 96, 577, 40);
  ui_icon(ICON_CHEV, 402, 584);
}

inline void draw_home() {
  display.fillScreen(PAPER);
  g_shown.battery = ui_battery_pct();
  g_shown.wifi = WiFi.status() == WL_CONNECTED;
  ui_status_bar(g_shown.battery);
  const String name = cfg_get("name");
  ui_text(F_B35, name.length() ? "Hello, " + name : "Hello", W / 2, 58, 46, AL_CENTER);
  g_shown.clock = now_clock(&g_shown.date);
  home_clock(g_shown.date, g_shown.clock);
  home_weather();
  g_shown.wxAt = g_wx.at;
  read_indoor(g_shown.indoor, g_shown.humidity);
  home_indoor(g_shown.indoor, g_shown.humidity);
  home_today(0);
  ui_page_counter("1/2");
}

// Partial updates for Home: clock each minute, weather on fetch, indoor/battery/Wi-Fi on change.
inline void home_tick() {
  String date;
  const String clock = now_clock(&date);
  if (clock == g_shown.clock && date == g_shown.date && g_wx.at == g_shown.wxAt) return;  // checked often, acts once a minute

  if (clock != g_shown.clock || date != g_shown.date) {
    g_shown.clock = clock; g_shown.date = date;
    home_clock(date, clock);
    ui_partial(40, 104, 400, 176);
  }
  if (g_wx.at != g_shown.wxAt) {
    g_shown.wxAt = g_wx.at;
    home_weather();
    ui_partial(40, 288, 400, 143);
  }
  String temp, hum;
  read_indoor(temp, hum);
  if (temp != g_shown.indoor || hum != g_shown.humidity) {
    g_shown.indoor = temp; g_shown.humidity = hum;
    home_indoor(temp, hum);
    ui_partial(40, 445, 400, 102);
  }
  const int pct = ui_battery_pct();
  const bool wifi = WiFi.status() == WL_CONNECTED;
  if (pct != g_shown.battery || wifi != g_shown.wifi) {
    g_shown.battery = pct; g_shown.wifi = wifi;
    ui_status_bar(pct);
    ui_partial(0, 0, W, 40);
  }
}

// ---------- Apps (01a) ----------
struct Tile { int x, y; const Icon *icon; const char *label; bool v3; };
inline const Tile TILES[] = {
  {40, 126, &ICON_APP_TODO, "To-do", false},   {250, 126, &ICON_APP_NOTE, "Notes", false},
  {40, 306, &ICON_APP_GEAR, "Settings", false}, {250, 306, &ICON_APP_HOME, "Home · V3", true},
  {40, 486, &ICON_APP_BOOK, "Reader · V3", true},
};

inline void draw_apps() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, "Apps", 40, 64, 42);
  for (const Tile &t : TILES) {
    if (t.v3) ui_card_dashed(t.x, t.y, 190, 160, 14, 3);
    else ui_card(t.x, t.y, 190, 160, 14, 3);
    ui_icon(*t.icon, t.x + 65, t.y + 26);
    ui_text(F_B25, t.label, t.x + 95, t.y + 101, 33, AL_CENTER);
  }
  ui_page_counter("2/2");
}

inline int apps_hit(int x, int y) {
  for (int i = 0; i < 5; i++)
    if (x >= TILES[i].x && x < TILES[i].x + 190 && y >= TILES[i].y && y < TILES[i].y + 160) return i;
  return -1;
}

// ---------- Web setup (04j) ----------
// ponytail: interim layout in the app fonts; the Figma-exact 04j comes with TASK-10.
static int s_qrY;
static void qr_modules(esp_qrcode_handle_t q) {
  const int n = esp_qrcode_get_size(q), s = 220 / n, x0 = (W - n * s) / 2;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) display.fillRect(x0 + x * s, s_qrY + y * s, s, s, INK);
}

inline void draw_websetup() {
  const bool ap = g_ap;
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, "Web setup", 40, 64, 42);
  display.fillRect(40, 118, 400, 3, INK);
  ui_text(F_R25, ap ? "Join this Wi-Fi" : "Open this on your phone", W / 2, 150, 33, AL_CENTER);
  display.fillRoundRect(40, 192, 400, 72, 12, INK);
  ui_text(ap ? F_B25 : F_B35, ap ? AP_SSID : net_ip(), W / 2, 192, 72, AL_CENTER, PAPER, INK);
  s_qrY = 290;
  const String qr = ap ? String("WIFI:T:nopass;S:") + AP_SSID + ";;" : "http://" + net_ip() + "/";
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = qr_modules;
  esp_qrcode_generate(&cfg, qr.c_str());
  if (ap) {
    ui_text(F_R25, "Scan to join, then open", W / 2, 540, 33, AL_CENTER);
    ui_text(F_B25, net_ip(), W / 2, 576, 33, AL_CENTER);
  } else {
    ui_text(F_R25, "Use the same Wi-Fi.", W / 2, 540, 33, AL_CENTER);
  }
}

// ---------- 02a New task (TASK-07 test bed for the keyboard; TASK-08 makes it real) ----------
struct Editor {
  String text;
  bool kbOpen = true, dirty = false;
  uint32_t dirtyAt = 0;
  Keyboard kb;
};
inline Editor g_ed;
constexpr int FIELD_X = 40, FIELD_Y = 132, FIELD_W = 400, FIELD_H = 76;
constexpr uint32_t KEY_BATCH_MS = 30;   // keys queued during a refresh are applied together anyway
constexpr int KBD_FULL_EVERY = 20;      // full refresh after this many typing refreshes

inline void editor_field() {
  ui_card(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, 10, 3);
  // Show the tail when the text is wider than the field. ponytail: drops one byte at a time,
  // fine for ASCII; make it UTF-8 aware when Vietnamese typing (TASK-14) lands.
  const int maxW = FIELD_W - 2 * 19 - 6;
  String shown = g_ed.text;
  bool cut = false;
  while (shown.length() && ui_width(F_B26, (cut ? "…" : "") + shown) > maxW) { shown.remove(0, 1); cut = true; }
  if (cut) shown = "…" + shown;
  const int tw = ui_text(F_B26, shown, 59, 153, 34);
  display.fillRect(59 + tw + 2, 154, 3, 32, INK);  // cursor
}

inline void draw_newtask() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, "New task", 40, 54, 42);
  ui_text(F_R25, "Cancel", 440, 58, 33, AL_RIGHT);
  display.fillRect(40, 109, 400, 3, INK);  // TopBar bottom stroke
  editor_field();
  if (g_ed.kbOpen) g_ed.kb.draw();
}

inline void editor_key(const KbKey &k) {  // silent: a beep per key cannot keep pace with ~1 refresh/s
  switch (k.kind) {
    case K_CHAR:
    case K_SPACE:
      if (g_ed.text.length() < 120) g_ed.text += g_ed.kb.shift ? char(toupper(k.ch)) : k.ch;
      if (g_ed.kb.shift) { g_ed.kb.shift = false; g_ed.kb.draw(); ui_partial(0, KB_TOP, W, KB_H, KBD_FULL_EVERY); }
      break;
    case K_BKSP:
      if (g_ed.text.length()) g_ed.text.remove(g_ed.text.length() - 1);
      break;
    case K_SHIFT:
    case K_MODE:
      if (k.kind == K_SHIFT) g_ed.kb.shift = !g_ed.kb.shift;
      else { g_ed.kb.numbers = !g_ed.kb.numbers; g_ed.kb.shift = false; }
      g_ed.kb.draw();
      ui_partial(0, KB_TOP, W, KB_H, KBD_FULL_EVERY);
      return;
    case K_DONE:  // §4: Done only hides the keyboard
      Serial.printf("[edit] \"%s\"\n", g_ed.text.c_str());
      g_ed.kbOpen = false;
      display.fillRect(0, KB_TOP, W, KB_H, PAPER);
      ui_partial(0, KB_TOP, W, KB_H, KBD_FULL_EVERY);
      return;
  }
  g_ed.dirty = true;
  g_ed.dirtyAt = millis();
}

inline void editor_flush() {  // one partial refresh per burst of keys
  if (!g_ed.dirty || millis() - g_ed.dirtyAt < KEY_BATCH_MS) return;
  g_ed.dirty = false;
  editor_field();
  ui_partial(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, KBD_FULL_EVERY);
}

// ---------- navigation ----------
inline void show(Screen s) {
  g_screen = s;
  if (s == SCR_HOME) draw_home();
  else if (s == SCR_APPS) draw_apps();
  else if (s == SCR_NEWTASK) draw_newtask();
  else draw_websetup();
  ui_full();
}

inline void open_newtask() {
  g_ed = Editor();
  show(SCR_NEWTASK);
}

// Touch events come from the touch task's queue, so taps made during a refresh are not lost.
// DOWN acts on keys; UP (with the DOWN position) acts on taps, swipes and Cancel. Backspace repeats while held.
inline void on_touch_up(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  const int dx = x1 - x0;
  if (g_screen == SCR_WEBSETUP && !g_ap) { show(SCR_HOME); return; }
  if (g_screen == SCR_NEWTASK) {
    if (x0 >= 340 && y0 >= 40 && y0 < 112) { hw_beep(30); show(SCR_APPS); }  // Cancel
    else if (!g_ed.kbOpen && y0 >= FIELD_Y && y0 < FIELD_Y + FIELD_H) {   // tap field -> keyboard back
      g_ed.kbOpen = true;
      g_ed.kb.draw();
      ui_partial(0, KB_TOP, W, KB_H, KBD_FULL_EVERY);
    }
    return;
  }
  if (abs(dx) > 80 && abs(dx) > abs(y1 - y0)) {  // swipe: left = next page
    if (dx < 0 && g_screen == SCR_HOME) show(SCR_APPS);
    else if (dx > 0 && g_screen == SCR_APPS) show(SCR_HOME);
  } else if (g_screen == SCR_APPS) {
    const int t = apps_hit(x0, y0);
    if (t == 0) { hw_beep(30); open_newtask(); }           // To-do -> New task (test bed)
    else if (t == 2) { hw_beep(30); show(SCR_WEBSETUP); }  // Settings -> Web setup for now
  }
}

inline void screens_input() {
  static int16_t x0, y0;
  static uint32_t t0 = 0, lastRepeat = 0;
  static KbKey held;
  static bool holding = false;
  TouchEv e;
  while (xQueueReceive(g_touchQ, &e, 0) == pdTRUE) {  // drain everything queued during the last refresh
    if (e.down) {
      x0 = e.x; y0 = e.y; t0 = millis(); holding = false;
      if (g_screen == SCR_NEWTASK && g_ed.kbOpen && g_ed.kb.hit(e.x, e.y, held)) { holding = true; editor_key(held); }
    } else {
      holding = false;
      on_touch_up(x0, y0, e.x, e.y);
    }
  }
  // Backspace auto-repeat: after 500 ms, every 150 ms, while the finger stays down.
  if (holding && g_touchHeld && held.kind == K_BKSP && millis() - t0 > 500 && millis() - lastRepeat > 150) {
    lastRepeat = millis();
    editor_key(held);
  }

  if (HwButton b = hw_buttons_poll()) {
    hw_beep(b == BTN_TOP_LONG ? 120 : 30);
    if (g_screen == SCR_WEBSETUP && !g_ap) show(SCR_HOME);
    else if (b == BTN_DOWN && g_screen == SCR_HOME) show(SCR_APPS);
    else if (b == BTN_UP && g_screen == SCR_APPS) show(SCR_HOME);
  }
}

inline void screens_loop() {
  screens_input();
  if (g_screen == SCR_HOME) home_tick();
  else if (g_screen == SCR_NEWTASK) editor_flush();
}
