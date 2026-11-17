// HeySticky screens: 01 Home, 01a Apps, 04j Web setup, To-do (todo_ui.h), Notes (notes_ui.h); navigation and touch.
// Coordinates from the Figma frames.
#pragma once
#include <qrcode.h>  // esp_qrcode, bundled with the ESP32 core
#include "ui.h"
#include "kbd.h"
#include "net.h"
#include "todo_ui.h"
#include "notes_ui.h"

enum Screen : uint8_t { SCR_HOME, SCR_APPS, SCR_WEBSETUP, SCR_TODO, SCR_TASK, SCR_DATE, SCR_TIME, SCR_DELTASK, SCR_NOSD,
                       SCR_NOTES, SCR_NOTE, SCR_NOTEED, SCR_DELNOTE };
inline Screen g_screen = SCR_HOME;

// ---------- shared state shown on Home ----------
struct HomeState { String date, clock, indoor, humidity; int battery = -2, today = -2; bool wifi = false; uint32_t wxAt = 1; };
inline HomeState g_shown;  // what is currently on the panel

inline String fmt_date(const tm &t) {
  static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
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

inline int today_count() { return g_sdOk ? todo_due_today(g_tasks, now_key()) : -1; }  // -1 = no card

inline void home_today(int n) {
  ui_card(40, 561, 400, 72, 12, 2);
  ui_icon(n > 0 ? ICON_CHECKLIST : ICON_CHECK_CIRCLE, 58, 584);
  ui_text(F_X30, n < 0 ? "No microSD card" : n == 0 ? "No tasks today" : String(n) + (n == 1 ? " task today" : " tasks today"), 96, 577, 40);
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
  home_today(g_shown.today = today_count());
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
    const int n = today_count();  // a task's day can arrive at midnight
    if (n != g_shown.today) { home_today(g_shown.today = n); ui_partial(40, 561, 400, 72); }
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


// ---------- navigation ----------
inline bool g_redraw = false, g_timeDirty = false;  // redraw the current screen with one partial refresh (batched taps)

inline void draw_screen() {
  switch (g_screen) {
    case SCR_HOME: draw_home(); break;
    case SCR_APPS: draw_apps(); break;
    case SCR_WEBSETUP: draw_websetup(); break;
    case SCR_TODO: draw_todo(); break;
    case SCR_TASK: draw_task_editor(); break;
    case SCR_DATE: draw_date(); break;
    case SCR_TIME: draw_time(); break;
    case SCR_DELTASK: draw_delete("Delete task?"); break;
    case SCR_NOSD: draw_nosd(); break;
    case SCR_NOTES: draw_notes(); break;
    case SCR_NOTE: draw_note_view(); break;
    case SCR_NOTEED: draw_note_editor(); break;
    case SCR_DELNOTE: draw_delete("Delete note?"); break;
  }
}

inline void show(Screen s) {  // screen change: full refresh
  g_screen = s;
  g_redraw = false;
  draw_screen();
  ui_full();
}

inline void nosd(bool notes) { g_nosdNotes = notes; show(SCR_NOSD); }

inline void open_todo() {
  g_todoPage = 0;
  if (g_sdOk || todo_retry()) show(SCR_TODO);
  else nosd(false);
}

inline void todo_commit(Screen next) { if (todo_save()) show(next); else nosd(false); }  // after any change to g_tasks

// ---------- Notes ----------
inline void open_notes() {  // the list is read fresh each time: the card may have been edited on a computer
  g_notesPage = 0;
  if (notes_scan() || notes_retry()) show(SCR_NOTES);
  else nosd(true);
}

inline void open_note(int num) {
  g_viewPage = 0;
  if (note_read(num)) show(SCR_NOTE);
  else nosd(true);
}

inline void open_note_editor(bool isNew) {
  g_ne = NoteEdit();
  if (!isNew) { g_ne.n = g_note; g_ne.kbOpen = g_ne.follow = false; }  // body shown from its start
  show(SCR_NOTEED);
}

inline void note_editor_save() {
  Note n = g_ne.n;
  n.title.trim();
  String body = n.body;
  body.trim();
  hw_beep(30);
  if (!n.num && !n.title.length() && !body.length()) { show(SCR_NOTES); return; }  // nothing typed: nothing saved
  g_note = n;
  if (!note_save()) { nosd(true); return; }
  if (n.num) { g_viewPage = 0; show(SCR_NOTE); }
  else { g_notesPage = 0; show(SCR_NOTES); }
}

inline void open_editor(int idx) {  // idx -1 = new task
  g_ed = TaskEdit();
  g_ed.idx = idx;
  if (idx >= 0) { g_ed.text = g_tasks[idx].text; g_ed.kbOpen = false; }
  g_ed.dl = dl_from(idx >= 0 ? g_tasks[idx].due : String(""));
  show(SCR_TASK);
}

inline void editor_save() {
  String s = g_ed.text;
  s.trim();
  String due;
  if (!s.length() || !dl_due(g_ed.dl, due)) { hw_beep(120, 1200); return; }  // no text / Time-only before NTP
  hw_beep(30);
  if (g_ed.idx < 0) {
    Task t;
    t.text = s;
    t.due = due;
    g_tasks.push_back(t);
  } else {
    g_tasks[g_ed.idx].text = s;
    g_tasks[g_ed.idx].due = due;
  }
  todo_commit(SCR_TODO);
}

// ---------- typing ----------
constexpr uint32_t KEY_BATCH_MS = 30;  // keys queued during a refresh are applied together anyway
inline void editor_key(const KbKey &k) {  // silent: a beep per key cannot keep pace with ~1 refresh/s
  const KbResult r = kb_apply(g_ed.kb, g_ed.text, 120, k);
  if (r == KB_DONE) { g_ed.kbOpen = false; g_redraw = true; }  // §4: Done only hides the keyboard
  else if (r == KB_TEXT) { g_ed.touched = g_ed.dirty = true; g_ed.dirtyAt = millis(); }
}

inline void note_key(const KbKey &k) {
  String &s = g_ne.field ? g_ne.n.body : g_ne.n.title;
  const unsigned cap = g_ne.field ? NOTE_BYTES - 3 - g_ne.n.title.length() : TITLE_MAX;
  const KbResult r = kb_apply(g_ne.kb, s, cap, k);
  if (r == KB_DONE) { g_ne.kbOpen = false; g_redraw = true; }
  else if (r == KB_TEXT) { g_ne.touched[g_ne.field] = g_ne.follow = g_ne.dirty = true; g_ne.dirtyAt = millis(); }
}

inline void key_down(const KbKey &k) { if (g_screen == SCR_NOTEED) note_key(k); else editor_key(k); }

inline void note_flush() {
  if (!g_ne.dirty || millis() - g_ne.dirtyAt < KEY_BATCH_MS) return;
  g_ne.dirty = false;
  note_fields();
  ui_partial(0, 112, W, (g_ne.kbOpen ? KB_TOP : NB_BOTTOM + 1) - 112, KBD_FULL_EVERY);
}

inline void editor_flush() {  // one partial refresh per burst of keys
  if (!g_ed.dirty || millis() - g_ed.dirtyAt < KEY_BATCH_MS) return;
  g_ed.dirty = false;
  editor_field();
  ui_partial(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, KBD_FULL_EVERY);
}

// ---------- touch ----------
inline bool in(int x, int y, int x0, int y0, int w, int h) { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; }
inline bool top_right(int x, int y) { return x >= 340 && y >= 40 && y < 112; }  // Cancel / Clear / Back

inline void todo_tap(int x, int y) {
  if (in(x, y, 40, g_addBtnY, 400, 72)) { hw_beep(30); open_editor(-1); return; }
  const int idx = todo_row_at(y);
  if (idx < 0 || x < 40 || x >= 440) return;
  hw_beep(30);
  g_tasks[idx].done = !g_tasks[idx].done;
  if (!todo_save()) { nosd(false); return; }
  g_redraw = true;
}

inline void editor_tap(int x, int y) {
  if (top_right(x, y)) { hw_beep(30); show(SCR_TODO); return; }
  if (in(x, y, FIELD_X, FIELD_Y, FIELD_W, FIELD_H)) {  // tap field -> keyboard back, placeholder gone
    if (!g_ed.kbOpen || !g_ed.touched) g_ed.kbOpen = g_ed.touched = g_redraw = true;
    return;
  }
  if (in(x, y, 40, ROW_DATE_Y, 400, ROW_H)) { hw_beep(30); g_dlUndo = g_ed.dl; show(SCR_DATE); }
  else if (in(x, y, 40, ROW_TIME_Y, 400, ROW_H)) { hw_beep(30); g_dlUndo = g_ed.dl; time_begin(); show(SCR_TIME); }
  else if (!g_ed.kbOpen && y >= 717 && y < 784) {
    if (g_ed.idx >= 0 && in(x, y, 40, 717, 194, 67)) { hw_beep(30); show(SCR_DELTASK); }
    else if (g_ed.idx < 0 ? in(x, y, 40, 717, 400, 67) : in(x, y, 246, 717, 194, 67)) editor_save();
  }
}

inline Screen g_delBack = SCR_NOTE;  // 03f Cancel returns to the view or the editor

inline void notes_tap(int x, int y) {
  if (in(x, y, 40, g_newBtnY, 400, 72)) { hw_beep(30); open_note_editor(true); return; }
  const int num = notes_row_at(y);
  if (num && x >= 40 && x < 440) { hw_beep(30); open_note(num); }
}

inline void note_view_tap(int x, int y) {
  if (in(x, y, 40, g_viewBtnY, 192, 64)) { hw_beep(30); open_note_editor(false); }
  else if (in(x, y, 248, g_viewBtnY, 192, 64)) { hw_beep(30); g_delBack = SCR_NOTE; show(SCR_DELNOTE); }
}

inline void note_editor_tap(int x, int y) {
  if (top_right(x, y)) { hw_beep(30); show(g_ne.n.num ? SCR_NOTE : SCR_NOTES); return; }  // Cancel: g_note untouched
  const int field = in(x, y, 40, NT_Y, 400, NT_H) ? 0 : in(x, y, 40, NB_Y, 400, g_nbBottom - NB_Y) ? 1 : -1;
  if (field >= 0) {  // tap a field: type into it
    if (field != g_ne.field || !g_ne.kbOpen || !g_ne.touched[field]) {
      g_ne.field = field;
      g_ne.touched[field] = g_ne.kbOpen = g_redraw = true;
      if (field == 1) g_ne.follow = true;  // the cursor is at the end: show it
    }
    return;
  }
  if (g_ne.kbOpen || y < 717 || y >= 784) return;
  if (g_ne.n.num && in(x, y, 40, 717, 194, 67)) { hw_beep(30); g_delBack = SCR_NOTEED; show(SCR_DELNOTE); }
  else if (g_ne.n.num ? in(x, y, 246, 717, 194, 67) : in(x, y, 40, 717, 400, 67)) note_editor_save();
}

// 02c / 02h: the toggle row flips; Cancel restores what the editor had; Done keeps the change.
inline bool dl_common_tap(int x, int y, bool &on) {
  if (top_right(x, y)) { hw_beep(30); g_ed.dl = g_dlUndo; show(SCR_TASK); return true; }
  if (in(x, y, 40, 112, 400, 72)) { on = !on; g_redraw = true; return true; }
  return false;
}

inline void date_tap(int x, int y) {
  if (dl_common_tap(x, y, g_ed.dl.dateOn)) return;
  if (g_ed.dl.dateOn && y >= 184 && y < 244 && (x < 110 || x >= 370)) { date_month(x < 110 ? -1 : 1); g_redraw = true; }
  else if (g_ed.dl.dateOn && date_day_at(x, y)) g_redraw = true;
  else if (in(x, y, 40, 711, 400, 73)) { hw_beep(30); show(SCR_TASK); }
}

inline void time_tap(int x, int y) {
  if (dl_common_tap(x, y, g_ed.dl.timeOn)) { if (g_ed.dl.timeOn) time_begin(); return; }
  if (!g_ed.dl.timeOn) { if (in(x, y, 40, 711, 400, 73)) { hw_beep(30); show(SCR_TASK); } return; }
  for (int i = 0; i < 2; i++)
    if (in(x, y, TF_X[i], TF_Y, TF_W, TF_H)) { g_te.active = i; g_te.fresh = true; g_timeDirty = true; }
}

inline void keypad_key(int k) {  // on touch DOWN, like the letter keyboard
  if (k == 11) { hw_beep(30); time_commit(); show(SCR_TASK); return; }
  if (k == 9) time_back(g_te);
  else if (!time_digit(g_te, k == 10 ? '0' : char('1' + k))) hw_beep(80, 1200);  // would be an invalid time
  g_timeDirty = true;
}

// ---------- page moves + Home (PLAN.md §3) ----------
// DOWN = back = swipe left-to-right; UP = forward = swipe right-to-left. Only screens with pages move.
inline void page_back() {
  if (g_screen == SCR_APPS) show(SCR_HOME);
  else if (g_screen == SCR_NOTES) {
    if (g_notesPage > 0) { g_notesPage--; g_redraw = true; }
    else show(SCR_APPS);
  } else if (g_screen == SCR_NOTE) {
    if (g_viewPage > 0) { g_viewPage--; g_redraw = true; }
    else show(SCR_NOTES);
  } else if (g_screen == SCR_NOTEED) note_scroll(-1);
  else if (g_screen == SCR_TODO) {
    if (g_todoPage > 0) { g_todoPage--; g_redraw = true; }
    else show(SCR_APPS);
  }
}
inline void page_fwd() {
  if (g_screen == SCR_HOME) show(SCR_APPS);
  else if (g_screen == SCR_TODO && g_todoPage + 1 < g_todoPages) { g_todoPage++; g_redraw = true; }
  else if (g_screen == SCR_NOTES && g_notesPage + 1 < notes_pages()) { g_notesPage++; g_redraw = true; }
  else if (g_screen == SCR_NOTE && g_viewPage + 1 < g_viewPages) { g_viewPage++; g_redraw = true; }
  else if (g_screen == SCR_NOTEED) note_scroll(1);
}
inline void go_home() { if (g_screen != SCR_HOME) show(SCR_HOME); }

constexpr int EDGE_Y = 785;      // calibration knob: a swipe starting below this line is the Home gesture
constexpr int EDGE_SWIPE = 100;  // ...and must travel this far up

// Touch events come from the touch task's queue, so taps made during a refresh are not lost.
// DOWN acts on keys; UP (with the DOWN position) acts on taps, swipes and buttons.
inline void on_touch_up(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  const int dx = x1 - x0;
  const bool swipe = abs(dx) > 80 && abs(dx) > abs(y1 - y0);
  if (y0 >= EDGE_Y && y0 - y1 >= EDGE_SWIPE) { hw_beep(30); go_home(); return; }  // swipe up from the bottom edge
  const int dy = y1 - y0;
  if (g_screen == SCR_NOTEED && in(x0, y0, 40, NB_Y, 400, g_nbBottom - NB_Y) && abs(dy) > 60 && abs(dy) > abs(dx)) {
    note_scroll(dy < 0 ? 1 : -1);  // drag the text up = see later lines, like a phone
    return;
  }
  if (swipe && (g_screen == SCR_HOME || g_screen == SCR_APPS || g_screen == SCR_TODO || g_screen == SCR_NOTES || g_screen == SCR_NOTE)) {
    if (dx < 0) page_fwd();
    else page_back();
    return;
  }
  switch (g_screen) {
    case SCR_WEBSETUP:
      if (!g_ap) show(SCR_HOME);
      return;
    case SCR_HOME:
      if (in(x0, y0, 40, 561, 400, 72)) { hw_beep(30); open_todo(); }
      return;
    case SCR_APPS: {
      const int t = apps_hit(x0, y0);
      if (t == 0) { hw_beep(30); open_todo(); }
      else if (t == 1) { hw_beep(30); open_notes(); }
      else if (t == 2) { hw_beep(30); show(SCR_WEBSETUP); }  // Settings -> Web setup for now
      return;
    }
    case SCR_TODO: todo_tap(x0, y0); return;
    case SCR_TASK: editor_tap(x0, y0); return;
    case SCR_DATE: date_tap(x0, y0); return;
    case SCR_TIME: time_tap(x0, y0); return;
    case SCR_DELTASK:
      if (in(x0, y0, 40, 705, 193, 79)) {
        hw_beep(30);
        g_tasks.erase(g_tasks.begin() + g_ed.idx);
        todo_commit(SCR_TODO);
      } else if (in(x0, y0, 247, 705, 193, 79)) { hw_beep(30); show(SCR_TASK); }
      return;
    case SCR_NOSD:
      if (top_right(x0, y0)) { hw_beep(30); show(SCR_APPS); }
      else if (in(x0, y0, 40, 712, 400, 72)) {
        hw_beep(30);
        if (g_nosdNotes ? notes_retry() : todo_retry()) show(g_nosdNotes ? SCR_NOTES : SCR_TODO);
        else show(SCR_NOSD);
      }
      return;
    case SCR_NOTES: notes_tap(x0, y0); return;
    case SCR_NOTE: note_view_tap(x0, y0); return;
    case SCR_NOTEED: note_editor_tap(x0, y0); return;
    case SCR_DELNOTE:
      if (in(x0, y0, 40, 705, 193, 79)) {
        hw_beep(30);
        if (note_delete(g_note.num)) { g_notesPage = 0; show(SCR_NOTES); }
        else nosd(true);
      } else if (in(x0, y0, 247, 705, 193, 79)) { hw_beep(30); show(g_delBack); }
      return;
  }
}

constexpr uint32_t ROW_HOLD_MS = 500;    // to-do row: hold = edit
constexpr uint32_t ENTER_HOLD_MS = 500;  // note body: hold ↵ Done = hide the keyboard, tap = new line

inline void screens_input() {
  static int16_t x0, y0;
  static uint32_t t0 = 0, lastRepeat = 0;
  static KbKey held;
  static bool holding = false, pressing = false, consumed = false;  // consumed: DOWN already acted
  static bool enterDown = false;  // ↵ in the note body: acts on UP (tap) or after ENTER_HOLD_MS (hold)
  TouchEv e;
  while (xQueueReceive(g_touchQ, &e, 0) == pdTRUE) {  // drain everything queued during the last refresh
    if (e.down) {
      x0 = e.x; y0 = e.y; t0 = millis(); holding = false; pressing = true; consumed = false;
      int k;
      if (e.y >= EDGE_Y) {}  // may be the Home gesture: no key acts down here
      else if (g_screen == SCR_TASK && g_ed.kbOpen && g_ed.kb.hit(e.x, e.y, held)) { holding = consumed = true; editor_key(held); }
      else if (g_screen == SCR_NOTEED && g_ne.kbOpen && g_ne.kb.hit(e.x, e.y, held)) {
        holding = consumed = true;
        if (held.kind == K_DONE && g_ne.field == 1) enterDown = true;
        else note_key(held);
      }
      else if (g_screen == SCR_TIME && g_ed.dl.timeOn && (k = keypad_hit(e.x, e.y)) >= 0) { consumed = true; keypad_key(k); }
    } else {
      holding = pressing = false;
      if (enterDown) { enterDown = false; note_key({0, 0, 0, K_CHAR, '\n'}); }  // tap ↵ = new line
      if (!consumed) on_touch_up(x0, y0, e.x, e.y);
    }
  }
  // Long press on a to-do row opens Edit task while the finger is still down.
  if (pressing && !consumed && g_touchHeld && g_screen == SCR_TODO && millis() - t0 >= ROW_HOLD_MS) {
    consumed = true;
    const int idx = todo_row_at(y0);
    if (idx >= 0) { hw_beep(60); open_editor(idx); }
  }
  if (enterDown && g_touchHeld && millis() - t0 >= ENTER_HOLD_MS) {  // hold ↵ = hide the keyboard
    enterDown = false;
    g_ne.kbOpen = false;
    g_redraw = true;
  }
  // Backspace auto-repeat: after 500 ms, every 150 ms, while the finger stays down.
  if (holding && g_touchHeld && held.kind == K_BKSP && millis() - t0 > 500 && millis() - lastRepeat > 150) {
    lastRepeat = millis();
    key_down(held);
  }

  if (HwButton b = hw_buttons_poll()) {
    hw_beep(b == BTN_TOP_LONG ? 120 : 30);
    if (b == BTN_TOP) go_home();
    else if (b == BTN_DOWN) page_back();
    else if (b == BTN_UP) page_fwd();
    // BTN_TOP_LONG: hold to talk to the AI (V2); the long beep is all it does in V1
  }
}

inline void screens_loop() {
  screens_input();
  if (g_redraw) {
    g_redraw = false;
    draw_screen();
    ui_partial(0, 0, W, H);
  }
  if (g_screen == SCR_HOME) home_tick();
  else if (g_screen == SCR_TASK) editor_flush();
  else if (g_screen == SCR_NOTEED) note_flush();
  else if (g_screen == SCR_TIME && g_timeDirty) {  // one refresh for all digits typed during the last one
    g_timeDirty = false;
    time_fields();
    ui_partial(40, TF_Y, 400, TF_H, KBD_FULL_EVERY);
  }
}
