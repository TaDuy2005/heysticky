// HeySticky screens: 01 Home, 01a Apps, To-do (todo_ui.h), Notes (notes_ui.h), Reader (reader_ui.h), Settings (settings_ui.h);
// navigation and touch.
// Coordinates from the Figma frames.
#pragma once
#include "ui.h"
#include "net.h"
#include "todo_ui.h"
#include "notes_ui.h"
#include "reader_ui.h"
#include "settings_ui.h"
#include "ai_ui.h"
#include "ai.h"

enum Screen : uint8_t { SCR_HOME, SCR_APPS, SCR_WEBSETUP, SCR_TODO, SCR_TASK, SCR_DATE, SCR_TIME, SCR_DELTASK, SCR_NOSD,
                       SCR_NOTES, SCR_NOTE, SCR_DELNOTE, SCR_DISCARD,
                       SCR_LIBRARY, SCR_BOOK, SCR_CONTENTS, SCR_BOOKERR, SCR_RDFONT, SCR_RDNOSD, SCR_LISTEN, SCR_WORKING, SCR_REVIEW, SCR_DONE, SCR_FAILED,
                       SCR_SETTINGS, SCR_CONFIG, SCR_TEMP, SCR_ABOUT, SCR_SDCARD, SCR_FILES, SCR_UPDATE,
                       SCR_LANGFONT, SCR_SYSLANG, SCR_USERFONT, SCR_SLEEP, SCR_SOUND,
                       SCR_BUGNEW, SCR_BUGGH, SCR_BUGMAIL, SCR_BUGREVIEW };  // Settings last
inline Screen g_screen = SCR_HOME;

// ---------- shared state shown on Home ----------
struct HomeState { String date, clock, indoor, humidity; int battery = -2, today = -2; bool wifi = false, plugged = false; uint32_t wxAt = 1; };
inline HomeState g_shown;  // what is currently on the panel

inline String fmt_date(const tm &t) {
  static const char *DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *VI[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
  if (g_vi) return String(VI[t.tm_wday]) + ", " + t.tm_mday + " tháng " + (t.tm_mon + 1);  // "Thứ Hai, 16 tháng 11"
  return String(DAYS[t.tm_wday]) + ", " + t.tm_mday + " " + MONTHS[t.tm_mon];
}

inline String now_clock(String *date = nullptr) {
  tm t;
  if (!local_now(t)) { if (date) *date = ""; return "--:--"; }
  if (date) *date = fmt_date(t);
  return hm12(t.tm_hour * 60 + t.tm_min);  // "9:41 AM"
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
  const int sp = clock.indexOf(' ');  // "9:41 AM": big digits, Bold AM / PM beside them, the pair centred (Figma 01)
  if (sp < 0) { ui_text(F_X120, clock, W / 2, 144, 126, AL_CENTER); return; }  // "--:--" before the clock is set
  const String hm = clock.substring(0, sp), ap = clock.substring(sp + 1);
  const int wd = ui_width(F_X120, hm), x = W / 2 - (wd + 10 + ui_width(F_B35, ap)) / 2;
  ui_text(F_X120, hm, x, 144, 126);
  ui_text(F_B35, ap, x + wd + 10, 210, 46);
}

inline void home_weather() {
  ui_card(40, 288, 400, 143, 16, 3);
  const String units = cfg_get("units", "C") == "F" ? "°F" : "°";
  if (g_wx.ok) {
    const WxInfo wi = wx_info(g_wx.code);
    ui_icon(wx_icon(wi.icon), 61, 336);
    ui_text(F_B25, wi.label, 121, 309, 33);
    ui_text(F_R25, TR("H ", "C ") + String(lroundf(g_wx.hi)) + TR("°  L ", "°  T ") + String(lroundf(g_wx.lo)) + "°", 121, 344, 33);
    ui_text(F_X48, String(lroundf(g_wx.temp)) + "°", 419, 328, 63, AL_RIGHT);
  } else {
    ui_icon(ICON_WX_CLOUD, 61, 336);
    ui_text(F_B25, TR("Offline", "Ngoại tuyến"), 121, 309, 33);
    ui_text(F_R25, TR("No weather yet", "Chưa có thời tiết"), 121, 344, 33);
  }
  ui_text(F_R25, cfg_get("city"), 121, 377, 33);
}

inline void home_indoor(const String &temp, const String &hum) {
  ui_card(40, 445, 193, 102, 12, 2);
  ui_icon(ICON_THERMO, 58, 479);
  ui_text(F_X28, temp, 104, 461, 37);
  ui_text(F_R25, TR("Indoor", "Phòng"), 104, 498, 33);
  ui_card(247, 445, 193, 102, 12, 2);
  ui_icon(ICON_DROP, 265, 479);
  ui_text(F_X28, hum, 311, 461, 37);
  ui_text(F_R25, TR("Humidity", "Độ ẩm"), 311, 498, 33);
}

inline int today_count() { return g_sdOk ? todo_due_today(g_tasks, now_key()) : -1; }  // -1 = no card

inline void home_today(int n) {
  ui_card(40, 561, 400, 72, 12, 2);
  ui_icon(n > 0 ? ICON_CHECKLIST : ICON_CHECK_CIRCLE, 58, 584);
  ui_text(F_X30, n < 0 ? TR("No microSD card", "Không có thẻ SD") : n == 0 ? TR("No tasks today", "Không có việc") : String(n) + TR(n == 1 ? " task today" : " tasks today", " việc hôm nay"), 96, 577, 40);
  ui_icon(ICON_CHEV, 402, 584);
}

inline void draw_home() {
  display.fillScreen(PAPER);
  g_shown.battery = ui_battery_pct();
  g_shown.wifi = !g_asleep && WiFi.status() == WL_CONNECTED;
  g_shown.plugged = g_plugged;
  ui_status_bar(g_shown.battery);
  const String name = cfg_get("name");
  ui_text(F_B35, name.length() ? TR("Hello, ", "Xin chào, ") + name : TR("Hello", "Xin chào"), W / 2, 58, 46, AL_CENTER);
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
  if (clock == g_shown.clock && date == g_shown.date && g_wx.at == g_shown.wxAt &&
      bool(digitalRead(PIN_USB_PRESENT)) == g_shown.plugged) return;  // checked often, acts once a minute (or on plug/unplug)

  // Asleep: partial updates don't count toward the ghost-clearing full refresh; one full refresh on the hour instead.
  const int sp = clock.indexOf(' ');  // "9:41 AM"; "--:--" has none
  const bool hourly = g_asleep && clock != g_shown.clock && sp > 0 && clock.substring(sp - 3, sp) == ":00";
  const int every = g_asleep ? INT_MAX : FULL_EVERY;
  auto part = [&](int x, int y, int w, int h) { if (!hourly) ui_partial(x, y, w, h, every); };
  if (clock != g_shown.clock || date != g_shown.date) {
    g_shown.clock = clock; g_shown.date = date;
    home_clock(date, clock);
    part(40, 104, 400, 176);
    const int n = today_count();  // a task's day can arrive at midnight
    if (n != g_shown.today) { home_today(g_shown.today = n); part(40, 561, 400, 72); }
  }
  if (g_wx.at != g_shown.wxAt) {
    g_shown.wxAt = g_wx.at;
    home_weather();
    part(40, 288, 400, 143);
  }
  String temp, hum;
  if (g_asleep && !(sp > 0 && clock[sp - 1] == '0')) temp = g_shown.indoor, hum = g_shown.humidity;  // asleep: every 10 min (one refresh less a minute)
  else read_indoor(temp, hum);
  if (temp != g_shown.indoor || hum != g_shown.humidity) {
    g_shown.indoor = temp; g_shown.humidity = hum;
    home_indoor(temp, hum);
    part(40, 445, 400, 102);
  }
  const int pct = ui_battery_pct();
  const bool wifi = !g_asleep && WiFi.status() == WL_CONNECTED;  // asleep: the moon stays, Wi-Fi comes and goes
  if (pct != g_shown.battery || wifi != g_shown.wifi || g_plugged != g_shown.plugged) {
    g_shown.battery = pct; g_shown.wifi = wifi; g_shown.plugged = g_plugged;
    ui_status_bar(pct);
    part(0, 0, W, 40);
  }
  if (hourly) ui_full();
}

// ---------- Apps (01a) ----------
struct Tile { int x, y; const Icon *icon; const char *label, *vi; bool later; };  // later: Phase 3, not built yet
inline const Tile TILES[] = {
  {40, 126, &ICON_APP_TODO, "To-do", "Việc cần làm", false},   {250, 126, &ICON_APP_NOTE, "Notes", "Ghi chú", false},
  {40, 306, &ICON_APP_GEAR, "Settings", "Cài đặt", false}, {250, 306, &ICON_APP_HOME, "Home", "Nhà", true},
  {40, 486, &ICON_APP_BOOK, "Reader", "Đọc", false},
};

inline void draw_apps() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_text(F_X32, TR("Apps", "Ứng dụng"), 40, 64, 42);
  for (const Tile &t : TILES) {
    if (t.later) {  // dashed, with "Phase 3" under the name (the name and phase don't fit on one line)
      ui_card_dashed(t.x, t.y, 190, 160, 14, 3);
      ui_icon(*t.icon, t.x + 65, t.y + 16);
      ui_text(F_B25, TR(t.label, t.vi), t.x + 95, t.y + 84, 33, AL_CENTER);
      ui_text(F_R20, TR("Phase 3", "Giai đoạn 3"), t.x + 95, t.y + 117, 27, AL_CENTER);
      continue;
    }
    ui_card(t.x, t.y, 190, 160, 14, 3);
    ui_icon(*t.icon, t.x + 65, t.y + 26);
    ui_text(F_B25, TR(t.label, t.vi), t.x + 95, t.y + 101, 33, AL_CENTER);
  }
  ui_page_counter("2/2");
}

inline int apps_hit(int x, int y) {
  for (int i = 0; i < 5; i++)
    if (x >= TILES[i].x && x < TILES[i].x + 190 && y >= TILES[i].y && y < TILES[i].y + 160) return i;
  return -1;
}

// ---------- navigation ----------
inline Screen g_discardBack = SCR_TASK;  // 02j Cancel returns here
inline uint32_t g_lastInput = 0;  // millis() of the last touch or button: Auto-sleep counts from here
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
    case SCR_DELTASK: draw_delete(TR("Delete task?", "Xoá việc này?")); break;
    case SCR_NOSD: draw_nosd(); break;
    case SCR_NOTES: draw_notes(); break;
    case SCR_NOTE: draw_note_view(); break;
    case SCR_DELNOTE: draw_delete(TR("Delete note?", "Xoá ghi chú?")); break;
    case SCR_DISCARD: draw_delete(TR("Discard changes?", "Bỏ thay đổi?"), TR("Discard", "Bỏ")); break;
    case SCR_LIBRARY: draw_library(); break;
    case SCR_BOOK: draw_book(); break;
    case SCR_CONTENTS: draw_contents(); break;
    case SCR_BOOKERR: draw_book_error(); break;
    case SCR_RDFONT: draw_reader_fonts(); break;
    case SCR_RDNOSD: draw_reader_nosd(); break;
    case SCR_SETTINGS: draw_settings(); break;
    case SCR_CONFIG: draw_config(); break;
    case SCR_TEMP: draw_temp(); break;
    case SCR_ABOUT: draw_about(); break;
    case SCR_SDCARD: draw_sdcard(); break;
    case SCR_FILES: draw_files(); break;
    case SCR_UPDATE: draw_update(); break;
    case SCR_LANGFONT: draw_langfont(); break;
    case SCR_SYSLANG: draw_syslang(); break;
    case SCR_USERFONT: draw_userfont(); break;
    case SCR_SLEEP: draw_sleep(); break;
    case SCR_SOUND: draw_sound(); break;
    case SCR_BUGNEW: draw_bugnew(); break;
    case SCR_BUGGH: draw_bugqr(false); break;
    case SCR_BUGMAIL: draw_bugqr(true); break;
    case SCR_BUGREVIEW: draw_bugreview(); break;
    case SCR_LISTEN: draw_listen(); break;
    case SCR_WORKING: draw_working(); break;
    case SCR_REVIEW: draw_review(); break;
    case SCR_DONE: draw_done(); break;
    case SCR_FAILED: draw_failed(); break;
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
  if (!g_sdOk && sd_inserted()) draw_sd_loading(false);
  if (g_sdOk || todo_retry()) show(SCR_TODO);
  else nosd(false);
}

inline void todo_commit(Screen next) { if (todo_save()) show(next); else nosd(false); }  // after any change to g_tasks

// ---------- Notes ----------
inline void open_notes() {  // the list is read fresh each time: the card may have been edited on a computer
  g_notesPage = 0;
  if (!g_sdOk && sd_inserted()) draw_sd_loading(true);
  if (notes_scan() || notes_retry()) show(SCR_NOTES);
  else nosd(true);
}

inline void open_note(int num) {
  g_viewTop = 0;
  if (note_read(num)) show(SCR_NOTE);
  else nosd(true);
}

inline void open_editor(int idx) {  // 02e for task idx (long press on its row)
  g_ed = TaskEdit();
  g_ed.idx = idx;
  g_ed.text = g_tasks[idx].text;
  g_ed.created = g_tasks[idx].created;
  g_ed.dl = g_ed.dl0 = dl_from(g_tasks[idx].due);
  g_ed.dlOpen = g_tasks[idx].due.length() > 0;  // a deadline shows at once; none: the drop-down starts closed
  show(SCR_TASK);
}

inline void editor_done() {  // Done: keeps a changed deadline, back to the list
  String due;
  if (!dl_due(g_ed.dl, due)) { hw_beep(120, 1200); return; }  // Time only, before the clock is set
  hw_beep();
  if (dl_same(g_ed.dl, g_ed.dl0)) { show(SCR_TODO); return; }
  g_tasks[g_ed.idx].due = due;
  todo_commit(SCR_TODO);
}

// ---------- Settings (04) ----------
inline bool settings_screen(Screen s) { return s >= SCR_SETTINGS || s == SCR_WEBSETUP; }
inline Screen settings_parent() {  // DOWN / swipe right goes up one level
  switch (g_screen) {
    case SCR_SETTINGS: return SCR_APPS;
    case SCR_TEMP: return SCR_CONFIG;
    case SCR_SDCARD: return SCR_ABOUT;
    case SCR_FILES: return SCR_SDCARD;
    case SCR_SYSLANG: case SCR_USERFONT: return SCR_LANGFONT;
    case SCR_BUGREVIEW: return SCR_BUGNEW;
    case SCR_BUGGH: case SCR_BUGMAIL: return g_bugBody.length() ? SCR_BUGREVIEW : SCR_BUGNEW;  // the AI's report stays
    default: return SCR_SETTINGS;  // Configuration, About, Web setup
  }
}

inline void open_sdcard() { sd_files_scan(); show(SCR_SDCARD); }  // mounts a card put in since boot
inline void files_open() { hw_beep(); g_filesTop = 0; sd_dir_scan(); show(SCR_FILES); }  // 04k at g_dir, from the top
inline void files_tap(int y) {  // a folder row opens it; files do nothing
  const int r = srow_at(y, IROW_H, FILES_ROWS), i = g_filesTop + r;
  if (r < 0 || i >= int(g_files.size()) || g_files[i].items < 0) return;
  g_dir = (g_dir == "/" ? "" : g_dir) + "/" + g_files[i].name;
  files_open();
}

inline void temp_tap(int y) {
  const int r = srow_at(y, SROW_H, 2);
  if (r < 0) return;
  const String u = r ? "F" : "C";
  if (u != units()) { cfg_set("units", u); g_wx_next = 0; }  // weather again, in the new units
  hw_beep();
  g_redraw = true;
}

inline void sleep_tap(int y) {
  const int r = srow_at(y, SROW_H, SLEEP_N);
  if (r < 0) return;
  hw_beep();
  g_sleepMin = SLEEP_CHOICES[r];
  cfg_set("sleep_min", String(g_sleepMin));
  g_redraw = true;
}

// ---------- touch ----------
inline bool in(int x, int y, int x0, int y0, int w, int h) { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; }
inline bool top_right(int x, int y) { return x >= 340 && y >= 40 && y < 112; }  // Cancel / Clear / Back

// ---------- Reader (07) ----------
inline bool reader_screen(Screen s) { return s >= SCR_LIBRARY && s <= SCR_RDNOSD; }

inline void open_library() {  // read fresh each time: books may have come from a computer or the phone page
  rd::close();
  g_menu = false;
  if (!g_sdOk && sd_inserted()) draw_reader_wait(TR("Library", "Thư viện"), TR("Loading…", "Đang tải…"));
  if (books_scan()) show(SCR_LIBRARY);
  else show(SCR_RDNOSD);
}

inline void open_book(int i) {  // 07g while the book (and its chapter, the first time) is laid out
  const Book b = g_books[i];
  draw_reader_wait(b.title, TR("Opening…", "Đang mở…"));
  rd::set_font(reader_font());
  if (!rd::open(b.path) || !rd::show(b.seq ? b.chapter : rd::start_chapter(), b.seq ? b.offset : 0, reader_large())) {
    rd::close();
    show(SCR_BOOKERR);
    return;
  }
  g_bookPath = b.path;
  g_books[i].seq = next_seq(g_books);
  library_order(g_books);
  g_libPage = 0;
  g_menu = false;
  book_remember();
  show(SCR_BOOK);
}

inline void book_turn(int d) {  // nothing further = silent
  if (!(d > 0 ? rd::next() : rd::prev())) return;
  hw_beep();
  book_remember();
  g_redraw = true;
}

inline void book_tap(int x, int y) {
  if (!g_menu) {
    const PageZone z = page_zone(x);
    if (z == ZONE_MENU) { hw_beep(); g_menu = true; g_redraw = true; }
    else book_turn(z == ZONE_NEXT ? 1 : -1);
    return;
  }
  if (y < MENU_Y) { hw_beep(); g_menu = false; g_redraw = true; return; }  // tap the page: close the menu
  if (in(x, y, 40, MENU_ROW0, 400, 72)) {
    hw_beep();
    g_menu = false;
    g_tocPage = max(0, rd::toc_current()) / TOC_ROWS;
    show(SCR_CONTENTS);
  } else if (in(x, y, 40, MENU_ROW0 + 72, 400, 72)) {  // Font: 07j
    hw_beep();
    g_menu = false;
    g_rdFonts = rfont_scan();
    g_rdFontPage = 0;
    for (size_t i = 0; i < g_rdFonts.size(); i++)
      if (g_rdFonts[i].name == reader_font()) g_rdFontPage = int(i + 1) / RDFONT_ROWS;
    show(SCR_RDFONT);
  } else if (y >= MENU_ROW0 + 144 && y < MENU_ROW0 + 216 && x >= SIZE_S_X - 8 && x < SIZE_M_X + SIZE_W + 8) {
    const bool large = x >= SIZE_M_X - 8;
    if (large == reader_large()) return;
    hw_beep();
    cfg_set("rd_size", large ? "M" : "S");
    draw_reader_wait(book_title_clean(rd::title()), TR("Opening…", "Đang mở…"));  // the chapter is laid out again in the new size
    if (!rd::show(rd::chapter(), rd::offset(), large)) { show(SCR_BOOKERR); return; }
    g_menu = false;
    book_remember();
    show(SCR_BOOK);
  } else if (in(x, y, 40, MENU_ROW0 + 216, 400, 72)) { hw_beep(); open_library(); }
}

// Back from 07j: the book is laid out again in the font now set, at the place saved with the last page turn.
inline void font_back() {
  const Book *b = open_book();
  draw_reader_wait(book_title_clean(rd::title()), TR("Opening…", "Đang mở…"));
  rd::set_font(reader_font());
  if (!b || !rd::show(b->chapter, b->offset, reader_large())) { rd::close(); show(SCR_BOOKERR); return; }
  show(SCR_BOOK);
}

inline void rdfont_tap(int x, int y) {
  const int i = rdfont_hit(x, y);
  if (i < 0) return;
  const String name = i ? g_rdFonts[i - 1].name : "";
  if (name == reader_font()) return;
  hw_beep();
  cfg_set("rd_font", name);
  g_redraw = true;
}

inline void contents_tap(int x, int y) {
  const int i = contents_hit(x, y);
  if (i < 0) return;
  hw_beep();
  draw_reader_wait(book_title_clean(rd::title()), TR("Opening…", "Đang mở…"));
  if (!rd::to_toc(i, reader_large())) { show(SCR_BOOKERR); return; }
  book_remember();
  show(SCR_BOOK);
}


inline void todo_tap(int x, int y) {  // tap = tick; hold = 02e (screens_input)
  const int idx = todo_row_at(y);
  if (idx < 0 || x < 40 || x >= 440) return;
  hw_beep();
  g_tasks[idx].done = !g_tasks[idx].done;
  if (!todo_save()) { nosd(false); return; }
  g_redraw = true;
}

inline void editor_tap(int x, int y) {
  if (top_right(x, y)) { hw_beep(); show(SCR_TODO); return; }  // Cancel: nothing saved
  if (in(x, y, 40, row0(), 400, ROW_H)) { hw_beep(); g_ed.dlOpen = !g_ed.dlOpen; g_redraw = true; return; }
  if (g_ed.dlOpen && in(x, y, 40, date_y(), 400, ROW_H)) { hw_beep(); g_dlUndo = g_ed.dl; show(SCR_DATE); }
  else if (g_ed.dlOpen && in(x, y, 40, time_y(), 400, ROW_H)) { hw_beep(); g_dlUndo = g_ed.dl; time_begin(); show(SCR_TIME); }
  else if (in(x, y, 40, 717, 194, 67)) { hw_beep(); show(SCR_DELTASK); }
  else if (in(x, y, 246, 717, 194, 67)) editor_done();
}


inline void settings_tap(int x, int y) {
  if (x < 40 || x >= 440) return;
  switch (srow_at(y, SROW_H, SR_COUNT)) {
    case SR_ROTATE: cfg_set("rotate", set_rotate() ? "0" : "1"); g_redraw = true; break;
    case SR_SOUND: hw_beep(); show(SCR_SOUND); break;
    case SR_CONFIG: hw_beep(); show(SCR_CONFIG); break;
    case SR_SLEEP: hw_beep(); show(SCR_SLEEP); break;
    case SR_WEB: hw_beep(); g_wsNote = WS_NONE; show(SCR_WEBSETUP); break;
    case SR_BUG: hw_beep(); show(SCR_BUGNEW); break;
    case SR_ABOUT: hw_beep(); show(SCR_ABOUT); break;
    case SR_UPDATE: hw_beep(); show(SCR_UPDATE); break;
    case SR_LANG: hw_beep(); show(SCR_LANGFONT); break;
    default: break;
  }
}

// 04m Sound: the top-bar toggle turns sound on / off; a volume row saves it and plays a sample at that level.
inline void sound_tap(int x, int y) {
  if (top_right(x, y)) { g_sound = !g_sound; cfg_set("sound", g_sound ? "1" : "0"); hw_beep(); g_redraw = true; return; }
  const int r = g_sound ? srow_at(y, SROW_H, VOL_N) : -1;
  if (r < 0) return;
  g_vol = r;
  cfg_set("vol", String(r));
  hw_beep();  // sample at the new volume
  g_redraw = true;
}

inline void websetup_hotspot() {  // On (or back from Home Wi-Fi): scans first (about 3 s), new password each time
  draw_websetup(true);
  ui_partial(0, 0, W, H);  // show On + Loading... before the start
  net_web(true);
}

// 04j: toggle; then Hotspot | Home Wi-Fi (Home Wi-Fi asks first: warning, or "No Wi-Fi").
inline void websetup_tap(int x, int y) {
  const WsNote note = ws_note();
  if (top_right(x, y)) {
    hw_beep();
    g_wsNote = WS_NONE;
    if (g_webOn) net_web(false);
    else websetup_hotspot();  // always starts on Hotspot
  } else if (!g_webOn) {
    return;
  } else if (in(x, y, 40, 128, 194, 67) && ws_home()) {  // Hotspot
    hw_beep();
    g_wsNote = WS_NONE;
    if (g_webHome) websetup_hotspot();
  } else if (in(x, y, 246, 128, 194, 67) && !ws_home()) {  // Home Wi-Fi: the hotspot runs until Turn on
    hw_beep();
    g_wsNote = WiFi.status() == WL_CONNECTED ? WS_WARN : WS_NOWIFI;
  } else if (note && in(x, y, 40, 705, 400, 79)) {
    hw_beep();
    g_wsNote = WS_NONE;
    if (note == WS_WARN && x < 240) net_web(true, true);  // Turn on
    else if (g_webHome) websetup_hotspot();                // Use Hotspot after Home Wi-Fi lost its network
    // Cancel / Use Hotspot from the notice: the hotspot never stopped
  } else {
    return;
  }
  g_redraw = true;
}

inline void langfont_tap(int y) {
  const int r = srow_at(y, SROW_H, 2);
  if (r < 0) return;
  hw_beep();
  if (r == 0) { show(SCR_SYSLANG); return; }
  g_fontNames = font_list();  // a card put in since boot, or fonts added from the phone
  show(SCR_USERFONT);
}

inline void syslang_tap(int y) {
  const int r = srow_at(y, SROW_H, 2);
  if (r < 0 || (r == 1) == g_vi) return;
  hw_beep();
  g_vi = r == 1;
  cfg_set("lang", g_vi ? "vi" : "en");
  show(SCR_SYSLANG);  // every word changes: full refresh
}

inline void userfont_tap(int y) {
  const int r = srow_at(y, SROW_H, 1 + int(g_fontNames.size()));
  if (r < 0) return;
  hw_beep();
  const String name = r ? g_fontNames[r - 1] : "";
  if (name == cfg_get("font")) return;
  draw_userfont(r, true);
  ui_partial(0, 0, W, H);  // tick + Loading... now; reading 13 files from the card takes a few seconds
  cfg_set("font", font_apply(name) ? name : "");  // a bad or missing font file falls back to built-in
  show(SCR_USERFONT);
}

inline void update_redraw() { draw_update(); ui_partial(0, 0, W, H); }
inline void update_tap(int x, int y) {  // Check / Install / Try again; both block until done (install restarts)
  if (!update_has_button() || !in(x, y, 40, UPD_BTN_Y, 400, 72)) return;
  hw_beep();
  const bool install = g_ota == OTA_AVAIL || g_ota == OTA_FAILED;
  g_ota = install ? OTA_INSTALLING : OTA_CHECKING;
  g_otaPct = 0;
  update_redraw();
  if (install) ota_install(update_redraw);
  else ota_check();
  g_redraw = true;
}

// ---------- 04p-04t Report a bug ----------
inline void bugnew_tap(int x, int y) {  // the first card is the top button, not a tap
  for (int i = 1; i < 3; i++)
    if (in(x, y, 40, BUGOPT_Y + i * BUGOPT_STEP, 400, BUGOPT_H)) { hw_beep(); bug_draft(); show(i == 1 ? SCR_BUGGH : SCR_BUGMAIL); }
}

inline void bugqr_tap(int x, int y) {  // Done: back to 04t
  if (!in(x, y, 40, 705, 400, 79)) return;
  hw_beep();
  g_bugBody = "";
  show(SCR_BUGNEW);
}

inline void bugreview_tap(int x, int y) {  // one ID for this report, whichever way it is sent
  if (in(x, y, 40, 619, 193, 72)) { hw_beep(); show(SCR_BUGGH); }
  else if (in(x, y, 247, 619, 193, 72)) { hw_beep(); show(SCR_BUGMAIL); }
  else if (in(x, y, 40, 705, 400, 79)) { hw_beep(); g_bugBody = ""; show(SCR_BUGNEW); }  // then hold the top button again
}

inline void notes_tap(int x, int y) {
  const int num = notes_row_at(y);
  if (num && x >= 40 && x < 440) { hw_beep(); open_note(num); }
}

inline void note_view_tap(int x, int y) {
  if (in(x, y, 40, 720, 400, 64)) { hw_beep(); show(SCR_DELNOTE); }
}

// 02c / 02h: the toggle row flips; Cancel restores what the editor had; Done keeps the change.
inline bool dl_common_tap(int x, int y, bool &on) {
  if (top_right(x, y)) { hw_beep(); g_ed.dl = g_dlUndo; show(SCR_TASK); return true; }
  if (in(x, y, 40, 112, 400, 72)) { on = !on; g_redraw = true; return true; }
  return false;
}

inline void date_tap(int x, int y) {
  if (dl_common_tap(x, y, g_ed.dl.dateOn)) return;
  if (g_ed.dl.dateOn && y >= 184 && y < 244 && (x < 110 || x >= 370)) { date_month(x < 110 ? -1 : 1); g_redraw = true; }
  else if (g_ed.dl.dateOn && date_day_at(x, y)) g_redraw = true;
  else if (in(x, y, 40, 711, 400, 73)) { hw_beep(); show(SCR_TASK); }
}

inline void time_tap(int x, int y) {
  if (dl_common_tap(x, y, g_ed.dl.timeOn)) { if (g_ed.dl.timeOn) time_begin(); return; }
  if (!g_ed.dl.timeOn) { if (in(x, y, 40, 711, 400, 73)) { hw_beep(); show(SCR_TASK); } return; }
  for (int i = 0; i < 2; i++)
    if (in(x, y, TF_X[i], TF_Y, TF_W, TF_H)) { g_te.active = i; g_te.fresh = true; g_timeDirty = true; }
  for (int i = 0; i < 2; i++)
    if (in(x, y, AP_X[i], AP_Y, AP_W, AP_H) && g_te.pm != bool(i)) { hw_beep(); g_te.pm = i; g_timeDirty = true; }
}

inline void keypad_key(int k) {  // on touch DOWN, like the letter keyboard
  if (k == 11) { hw_beep(); time_commit(); show(SCR_TASK); return; }
  if (k == 9) time_back(g_te);
  else if (!time_digit(g_te, k == 10 ? '0' : char('1' + k))) hw_beep(80, 1200);  // would be an invalid time
  g_timeDirty = true;
}

// ---------- page moves + Home (PLAN.md §3, v1.2) ----------
// Back = swipe left-to-right, or DOWN where DOWN doesn't scroll. To-do / Notes lists: swipe up / down turns the page,
// looping 1-2-3-1 (UP does nothing there). 03c Note view and 04k SD Files scroll a screenful minus one line:
// swipe up or DOWN = later, swipe down or UP = earlier. Every move beeps; nothing to move = silent.
inline void go(Screen s) { hw_beep(); show(s); }
inline void loop_page(int &page, int pages, int d) { if (pages < 2) return; hw_beep(); page = (page + d + pages) % pages; g_redraw = true; }
inline void scroll_by(int &top, int max, int step, int d) {
  const int t = constrain(top + d * step, 0, max);
  if (t == top) return;
  hw_beep();
  top = t;
  g_redraw = true;
}
inline bool scrolls(int d) {  // d = +1 next / later, -1 previous / earlier; false = this screen doesn't scroll
  if (g_screen == SCR_TODO) loop_page(g_todoPage, g_todoPages, d);
  else if (g_screen == SCR_NOTES) loop_page(g_notesPage, notes_pages(), d);
  else if (g_screen == SCR_NOTE) scroll_by(g_viewTop, g_viewMax, g_viewStep, d);
  else if (g_screen == SCR_FILES) scroll_by(g_filesTop, files_max(), FILES_ROWS - 1, d);
  else if (g_screen == SCR_LIBRARY) loop_page(g_libPage, lib_pages(g_books), d);
  else if (g_screen == SCR_CONTENTS) loop_page(g_tocPage, toc_pages(), d);
  else if (g_screen == SCR_RDFONT) loop_page(g_rdFontPage, rdfont_pages(), d);
  else if (g_screen == SCR_BOOK && g_menu) { hw_beep(); g_menu = false; g_redraw = true; }  // any move closes the menu
  else if (g_screen == SCR_BOOK) book_turn(d);
  else return false;
  return true;
}
inline void back() {
  if (g_screen == SCR_FILES && g_dir != "/") { g_dir = sd_parent(g_dir); g_filesTop = 0; sd_dir_scan(); go(SCR_FILES); }  // up one folder
  else if (settings_screen(g_screen)) go(settings_parent());
  else if (g_screen == SCR_APPS || g_screen == SCR_NOSD || g_screen == SCR_TODO || g_screen == SCR_NOTES) go(g_screen == SCR_APPS ? SCR_HOME : SCR_APPS);
  else if (g_screen == SCR_NOTE) go(SCR_NOTES);
  else if (g_screen == SCR_BOOK && g_menu) { hw_beep(); g_menu = false; g_redraw = true; }
  else if (g_screen == SCR_BOOK || g_screen == SCR_BOOKERR) { hw_beep(); open_library(); }
  else if (g_screen == SCR_CONTENTS) go(SCR_BOOK);
  else if (g_screen == SCR_RDFONT) { hw_beep(); font_back(); }
  else if (g_screen == SCR_LIBRARY || g_screen == SCR_RDNOSD) { rd::close(); go(SCR_APPS); }
}
inline bool scroll_screen() { return g_screen == SCR_NOTE || g_screen == SCR_FILES || g_screen == SCR_BOOK; }  // UP / DOWN scroll (book: turn pages)
inline void button_down() { if (scroll_screen()) scrolls(1); else back(); }
inline void button_up() { if (scroll_screen()) scrolls(-1); else if (g_screen == SCR_HOME) go(SCR_APPS); }
// Home asks first (02j) instead of losing a changed deadline.
inline bool unsaved() {
  return (g_screen == SCR_TASK || g_screen == SCR_DATE || g_screen == SCR_TIME) && !dl_same(g_ed.dl, g_ed.dl0);
}
// ---------- AI voice (PLAN_AI.md): hold top = 05 Listening -> 05a Working -> 05c Done / 05b Review / 05d Failed ----------
inline void go_home();
inline Screen g_listenBack = SCR_HOME;          // where the AI screens return to (editors keep their state)
inline size_t g_lastWav = 0;                    // the take, kept for Try again on 05d

inline bool ai_screen(Screen s) { return s >= SCR_LISTEN && s <= SCR_FAILED; }

inline bool g_bugTake = false;  // the take started on 04t / 04r: a bug report, back to 04t

inline void listen_start() {
  if (g_screen == SCR_WORKING || !mic_start()) { hw_beep(BEEP_MS, 1200); return; }  // the job still reads the last take
  if (!ai_screen(g_screen)) {
    g_bugTake = g_screen == SCR_BUGNEW || g_screen == SCR_BUGREVIEW;
    g_listenBack = g_bugTake ? SCR_BUGNEW : g_screen;
  }
  show(SCR_LISTEN);  // recording already runs in the mic task during this full refresh
}

inline void ai_fail(AiFail f, int code) {
  g_failKind = f;
  g_failCode = code;
  Serial.printf("[ai] failed: %d (HTTP %d)\n", f, code);
  hw_beep(BEEP_MS, 1200);
  show(SCR_FAILED);
}

inline void ai_begin() {  // 05a opens with Heard you ticked; the calls run in their task
  const AiFail f = ai_start(g_lastWav, g_bugTake);
  if (f) return ai_fail(f, 0);
  g_aiStep = 1;
  show(SCR_WORKING);
}

inline void working_steps(int step) {
  g_aiStep = step;
  draw_working_steps();
  ui_partial(40, WORK_Y, 400, WORK_H, INT_MAX);
}

// The transcript as is (05b Note, 06h Plain text): titled with its first words; characters the fonts can't draw are dropped.
inline Note plain_note(String text) {
  text = ui_drawable(F_R25, text);
  text.trim();
  Note n;
  n.title = one_line(clip_chars(text, 30), TITLE_MAX);
  n.body = utf8_cut(text, NOTE_BYTES - 3 - n.title.length());
  return n;
}

// Saves on the main loop (the card shares the panel's SPI bus), ticks Saving on 05a, then 05c.
inline void ai_save(const AiResult &r) {
  g_doneItems.clear();
  g_doneNote = r.kind == AI_NOTE;
  if (r.kind == AI_TODO) {
    const String now = now_key();  // "" before the clock is set: no created date
    for (Task t : r.todos) { t.created = now; g_tasks.push_back(t); g_doneItems.push_back(t.text); }
    if (!todo_save()) return nosd(false);
  } else {
    notes_scan();  // the next note number comes from the list, which is only read when Notes opens
    const Note keep = g_note;  // the note being viewed, if any: Done goes back to it
    g_note = r.note;
    g_note.num = 0;
    g_note.created = now_key();
    const bool ok = note_save();
    g_doneItems.push_back(g_note.title.length() ? g_note.title : note_preview(g_note.body));
    g_note = keep;
    if (!ok) return nosd(true);
  }
  if (g_screen == SCR_WORKING) working_steps(3);
  hw_beep();
  show(SCR_DONE);
}

// Runs instead of the normal loop on 05a: the dots move until the calls finish; taps and buttons are dropped.
inline void working_loop() {
  static uint32_t last = 0;
  static int frame = 0;
  g_lastInput = millis();
  TouchEv e;
  while (xQueueReceive(g_touchQ, &e, 0) == pdTRUE) {}
  hw_buttons_poll();
  if (!g_job.done) {
    if (millis() - last >= 300) {
      last = millis();
      draw_working_dots(++frame);
      ui_partial(DOTS_X, DOTS_Y, DOTS_W, DOTS_H, INT_MAX);
    }
    return;
  }
  if (g_job.fail) return ai_fail(g_job.fail, g_job.code);
  g_aiText = g_job.text;
  if (g_job.r.kind == AI_BUG) { bug_draft(g_job.r.note.title, g_job.r.note.body); hw_beep(); show(SCR_BUGREVIEW); return; }
  g_aiSecs = mic_seconds();
  if (g_job.r.kind == AI_NOTE && g_job.cfg.plain) {  // 06h Plain text: your words, minus what the fonts can't draw
    g_job.r.note = plain_note(g_aiText);
    if (g_job.r.note.body.isEmpty()) return ai_fail(AF_EMPTY, 0);
  }
  if (g_job.r.kind == AI_UNSURE) { hw_beep(); show(SCR_REVIEW); return; }
  working_steps(2);  // Sorting it out ticked, Saving in progress
  ai_save(g_job.r);
}

// 05b: save the transcript as it is (no AI) as one to-do or a note, or go back to record again.
inline void review_tap(int x, int y) {
  AiResult r;
  if (in(x, y, 40, 619, 193, 72)) {
    Task t;
    t.text = one_line(g_aiText, AI_TASK_MAX);
    r.kind = AI_TODO;
    r.todos.push_back(t);
  } else if (in(x, y, 247, 619, 193, 72)) {
    r.kind = AI_NOTE;
    r.note = plain_note(g_aiText);
    if (r.note.body.isEmpty()) return ai_fail(AF_EMPTY, 0);
  } else if (in(x, y, 40, 705, 400, 79)) { hw_beep(); show(g_listenBack); return; }  // then hold the top button again
  else return;
  hw_beep();
  ai_save(r);
}

inline void ai_home() {  // Home from an AI screen, as if from the screen the take started on (Discard asks first)
  g_screen = g_listenBack;
  if (g_screen == SCR_HOME || g_screen == SCR_DISCARD) show(g_screen);
  else go_home();
}

// 05c: Go to To-do / Notes opens the list it went into; Done goes back.
inline void done_tap(int x, int y) {
  if (in(x, y, 40, 705, 193, 79)) {
    hw_beep();
    if (g_doneNote) open_notes();
    else open_todo();
  } else if (in(x, y, 247, 705, 193, 79)) { hw_beep(); show(g_listenBack); }
}

// 05d: Try again resends the same take when the network or the service failed; otherwise it goes back to record again.
inline void failed_tap(int x, int y) {
  if (in(x, y, 40, 705, 193, 79)) {
    hw_beep();
    if (g_lastWav && (g_failKind == AF_NONET || g_failKind == AF_STT || g_failKind == AF_AI || g_failKind == AF_REPLY)) ai_begin();
    else show(g_listenBack);
  } else if (in(x, y, 247, 705, 193, 79)) { hw_beep(); ai_home(); }
}

// Runs instead of the normal loop while listening: touches and other buttons are ignored.
inline void listen_loop() {
  static uint32_t last = 0;
  static int step = 0;
  g_lastInput = millis();
  TouchEv e;
  while (xQueueReceive(g_touchQ, &e, 0) == pdTRUE) {}
  if (digitalRead(PIN_BTN_TOP) != g_btn[0].idle && !mic_full()) {
    if (millis() - last >= WAVE_EVERY_MS) {
      last = millis();
      draw_wave(wave_level(), ++step);
      ui_partial(WAVE_X, WAVE_Y, WAVE_W, WAVE_H, INT_MAX);  // no ghost-clearing full refresh mid-take; show() does one
    }
    return;
  }
  g_lastWav = mic_stop();  // released (the button poll ignores this release: it was reported as long)
  ai_begin();
}

inline void go_home() {
  if (g_screen == SCR_HOME || g_screen == SCR_DISCARD) return;  // on the warning, only its buttons act
  if (unsaved()) { g_discardBack = g_screen; show(SCR_DISCARD); }
  else show(SCR_HOME);
}

constexpr int EDGE_Y = 785;      // calibration knob: a swipe starting below this line is the Home gesture
constexpr int EDGE_SWIPE = 100;  // ...and must travel this far up

// Touch events come from the touch task's queue, so taps made during a refresh are not lost.
// DOWN acts on keys; UP (with the DOWN position) acts on taps, swipes and buttons.
inline void on_touch_up(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  const int dx = x1 - x0;
  const bool swipe = abs(dx) > 80 && abs(dx) > abs(y1 - y0);
  if (y0 >= EDGE_Y && y0 - y1 >= EDGE_SWIPE) { hw_beep(); go_home(); return; }  // swipe up from the bottom edge
  const int dy = y1 - y0;
  if (abs(dy) > 80 && abs(dy) > abs(dx) && scrolls(dy < 0 ? 1 : -1)) return;  // drag up = later / next page, like a phone
  if (swipe && (g_screen == SCR_HOME || g_screen == SCR_APPS || g_screen == SCR_TODO || g_screen == SCR_NOTES || g_screen == SCR_NOTE || g_screen == SCR_NOSD ||
                settings_screen(g_screen) || reader_screen(g_screen))) {
    if (dx > 0) back();
    else if (g_screen == SCR_HOME) go(SCR_APPS);
    return;
  }
  switch (g_screen) {
    case SCR_WEBSETUP: websetup_tap(x0, y0); return;
    case SCR_SETTINGS: settings_tap(x0, y0); return;
    case SCR_CONFIG: if (srow_at(y0, SROW_H, 4) == 3) { hw_beep(); show(SCR_TEMP); } return;  // City: set on the phone setup page
    case SCR_TEMP: temp_tap(y0); return;
    case SCR_ABOUT: if (srow_at(y0, IROW_H, 6) == ABOUT_SD_ROW && sd_inserted()) { hw_beep(); open_sdcard(); } return;
    case SCR_SDCARD: if (g_sdOk && srow_at(y0, IROW_H, 5) == SD_FILES_ROW) { g_dir = "/"; files_open(); } return;
    case SCR_FILES: files_tap(y0); return;
    case SCR_UPDATE: update_tap(x0, y0); return;
    case SCR_LANGFONT: langfont_tap(y0); return;
    case SCR_SYSLANG: syslang_tap(y0); return;
    case SCR_USERFONT: userfont_tap(y0); return;
    case SCR_SLEEP: sleep_tap(y0); return;
    case SCR_SOUND: sound_tap(x0, y0); return;
    case SCR_BUGNEW: bugnew_tap(x0, y0); return;
    case SCR_BUGGH: case SCR_BUGMAIL: bugqr_tap(x0, y0); return;
    case SCR_BUGREVIEW: bugreview_tap(x0, y0); return;
    case SCR_HOME:
      if (in(x0, y0, 40, 561, 400, 72)) { hw_beep(); open_todo(); }
      return;
    case SCR_APPS: {
      const int t = apps_hit(x0, y0);
      if (t == 0) { hw_beep(); open_todo(); }
      else if (t == 1) { hw_beep(); open_notes(); }
      else if (t == 2) { hw_beep(); show(SCR_SETTINGS); }
      else if (t == 4) { hw_beep(); open_library(); }
      return;
    }
    case SCR_TODO: todo_tap(x0, y0); return;
    case SCR_TASK: editor_tap(x0, y0); return;
    case SCR_DATE: date_tap(x0, y0); return;
    case SCR_TIME: time_tap(x0, y0); return;
    case SCR_DELTASK:
      if (in(x0, y0, 40, 705, 193, 79)) {
        hw_beep();
        g_tasks.erase(g_tasks.begin() + g_ed.idx);
        todo_commit(SCR_TODO);
      } else if (in(x0, y0, 247, 705, 193, 79)) { hw_beep(); show(SCR_TASK); }
      return;
    case SCR_NOSD:
      if (in(x0, y0, 40, 712, 400, 72)) {
        hw_beep();
        if (sd_inserted()) draw_sd_loading(g_nosdNotes);
        if (g_nosdNotes ? notes_retry() : todo_retry()) show(g_nosdNotes ? SCR_NOTES : SCR_TODO);
        else show(SCR_NOSD);
      }
      return;
    case SCR_NOTES: notes_tap(x0, y0); return;
    case SCR_LIBRARY: { const int i = library_hit(x0, y0); if (i >= 0) { hw_beep(); open_book(i); } return; }
    case SCR_RDFONT: rdfont_tap(x0, y0); return;
    case SCR_BOOK: book_tap(x0, y0); return;
    case SCR_CONTENTS: contents_tap(x0, y0); return;
    case SCR_BOOKERR: if (in(x0, y0, 40, 712, 400, 72)) { hw_beep(); open_library(); } return;
    case SCR_RDNOSD: if (in(x0, y0, 40, 712, 400, 72)) { hw_beep(); open_library(); } return;
    case SCR_REVIEW: review_tap(x0, y0); return;
    case SCR_DONE: done_tap(x0, y0); return;
    case SCR_FAILED: failed_tap(x0, y0); return;
    case SCR_NOTE: note_view_tap(x0, y0); return;
    case SCR_DELNOTE:
      if (in(x0, y0, 40, 705, 193, 79)) {
        hw_beep();
        if (note_delete(g_note.num)) { g_notesPage = 0; show(SCR_NOTES); }
        else nosd(true);
      } else if (in(x0, y0, 247, 705, 193, 79)) { hw_beep(); show(SCR_NOTE); }
      return;
    case SCR_DISCARD:
      if (in(x0, y0, 40, 705, 193, 79)) { hw_beep(); show(SCR_HOME); }  // Discard: the changed deadline is dropped
      else if (in(x0, y0, 247, 705, 193, 79)) { hw_beep(); show(g_discardBack); }  // Cancel: back as it was
      return;
  }
}

constexpr uint32_t ROW_HOLD_MS = 500;    // to-do row: hold = edit

inline void screens_input() {
  static int16_t x0, y0;
  static uint32_t t0 = 0;
  static bool pressing = false, consumed = false;  // consumed: DOWN already acted
  TouchEv e;
  while (xQueueReceive(g_touchQ, &e, 0) == pdTRUE) {  // drain everything queued during the last refresh
    g_lastInput = millis();
    if (e.down) {
      x0 = e.x; y0 = e.y; t0 = millis(); pressing = true; consumed = false;
      int k;
      if (e.y >= EDGE_Y) {}  // may be the Home gesture: no key acts down here
      else if (g_screen == SCR_TIME && g_ed.dl.timeOn && (k = keypad_hit(e.x, e.y)) >= 0) { consumed = true; keypad_key(k); }
    } else {
      pressing = false;
      if (!consumed) on_touch_up(x0, y0, e.x, e.y);
    }
  }
  // Long press on a to-do row opens Edit task while the finger is still down.
  if (pressing && !consumed && g_touchHeld && g_screen == SCR_TODO && millis() - t0 >= ROW_HOLD_MS &&
      abs(g_touchX - x0) < 30 && abs(g_touchY - y0) < 30) {  // a slow swipe turns the page instead
    consumed = true;
    const int idx = todo_row_at(y0);
    if (idx >= 0) { hw_beep(); open_editor(idx); }
  }
  if (HwButton b = hw_buttons_poll()) {
    g_lastInput = millis();
    if (b == BTN_TOP || b == BTN_TOP_LONG) hw_beep(b == BTN_TOP_LONG ? 120 : BEEP_MS);  // UP / DOWN beep where they act
    if (b == BTN_TOP) go_home();
    else if (b == BTN_DOWN) button_down();
    else if (b == BTN_UP) button_up();
    else if (b == BTN_TOP_LONG) listen_start();
  }
}

// Card in / out (detect switch, held 300 ms so a half-pushed card doesn't flicker): mount or drop it,
// and redraw the screens that show it (REQ-16 live status). A pin read per loop: no measurable power.
inline void sd_watch() {
  static bool shown = sd_inserted();
  static int8_t pending = -1;
  static uint32_t at = 0;
  const bool in = sd_inserted();
  if (in == shown) { pending = -1; return; }
  if (pending != in) { pending = in; at = millis(); return; }
  if (millis() - at < 300) return;
  shown = in;
  pending = -1;
  Serial.printf("[sd] card %s\n", in ? "in" : "out");
  if (!in) { rd::close(); SD.end(); g_sdOk = false; }
  else if (g_screen == SCR_SDCARD) sd_files_scan();  // mounts, and 04i shows the file count
  else sd_mount();
  if (g_screen == SCR_ABOUT || g_screen == SCR_SDCARD) g_redraw = true;
  else if (g_screen == SCR_FILES && !in) show(SCR_SDCARD);
  else if (reader_screen(g_screen) && !in) show(SCR_RDNOSD);  // the book was on the card
}

inline void screens_loop() {
  if (g_screen == SCR_LISTEN) { listen_loop(); return; }
  if (g_screen == SCR_WORKING) { working_loop(); return; }
  screens_input();
  sd_watch();
  if (g_pageDirty) { g_pageDirty = false; show(g_screen); }  // the font or Wi-Fi switch changed from the phone page
  if (g_redraw) {
    g_redraw = false;
    const bool left = g_screen == SCR_BOOK && rd::image_page();  // the book page being left
    draw_screen();
    if (left || (g_screen == SCR_BOOK && rd::image_page())) ui_full();  // dithered pictures ghost under a partial refresh
    else ui_partial(0, 0, W, H);
  }
  if (g_screen == SCR_HOME) home_tick();
  else if (g_screen == SCR_TIME && g_timeDirty) {  // one refresh for all digits typed during the last one
    g_timeDirty = false;
    time_fields();
    ui_partial(40, TF_Y, 400, AP_Y + AP_H - TF_Y, KBD_FULL_EVERY);
  }
}
