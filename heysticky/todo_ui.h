// To-do screens, Figma 02-02g: list (02/02g), task screen (02e), deadline picker (02c), time (02h),
// delete confirm (02f), plus the no-card screen. Drawing and hit areas only; screens.h navigates.
#pragma once
#include "ui.h"
#include "card.h"

constexpr int KB_TOP = 510, KB_H = H - KB_TOP;  // 02h number pad area
constexpr int KBD_FULL_EVERY = 20;           // 02h: a full refresh after this many digit refreshes

inline String now_key() {  // "YYYY-MM-DD HH:MM", "" before NTP
  tm t;
  if (!local_now(t)) return "";
  char b[17];
  strftime(b, sizeof b, "%Y-%m-%d %H:%M", &t);
  return b;
}

inline String hhmm(int mins) {
  char b[6];
  snprintf(b, sizeof b, "%02d:%02d", mins / 60, mins % 60);
  return b;
}

// ---------- 02 To-do list / 02g empty ----------
struct RowHit { int16_t y, h; int idx; };
inline std::vector<RowHit> g_rows;  // rows on the panel now: taps are matched to what the user saw
inline int g_todoPage = 0, g_todoPages = 1;
constexpr int ROWS_TOP = 112, ROWS_BOTTOM = 746;  // above the page counter (no Add button since v1.2: add by voice or on the phone)

// Figma TodoRow: the title wraps to 2 lines, then "…"; the due line under it.
inline const Font &row_font(const Task &t) { return t.done ? F_R25 : F_B25; }
inline std::vector<String> row_lines(const Task &t) { return ui_lines(row_font(t), t.text, 346, 2); }
inline int row_h(const Task &t) { return 61 + (t.due.length() && !t.done ? 32 : 0) + (row_lines(t).size() > 1 ? 33 : 0); }

inline void todo_row(const Task &t, int y, int h, const String &now) {
  const int cb = y + (h - 36) / 2;
  if (t.done) {
    display.fillRoundRect(40, cb, 36, 36, 4, INK);
    ui_icon(ICON_CHECK26, 45, cb + 5, PAPER);
  } else {
    ui_card(40, cb, 36, 36, 4, 3);
  }
  const std::vector<String> ls = row_lines(t);
  const int ty = y + (h > 61 ? 12 : 14);
  for (int i = 0; i < int(ls.size()); i++) {
    const int tw = ui_text(row_font(t), ls[i], 94, ty + 33 * i, 33);
    if (t.done) display.fillRect(94, ty + 33 * i + 16, tw, 2, INK);  // strikethrough
  }
  if (t.due.length() && !t.done) ui_text(F_B25, todo_label(t, now), 94, ty + 33 * max<int>(ls.size(), 1) + 2, 33);
  display.fillRect(40, y + h - 1, 400, 1, INK);
}

inline void draw_todo() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("To-do", "Việc cần làm"), TR(String(todo_left(g_tasks)) + " left", "còn " + String(todo_left(g_tasks))));
  g_rows.clear();
  const std::vector<int> o = todo_order(g_tasks);
  if (o.empty()) {
    ui_icon(ICON_CHECK_CIRCLE64, 208, 318);
    ui_text(F_X30, TR("No tasks yet", "Chưa có việc nào"), W / 2, 398, 40, AL_CENTER);
    ui_wrap(F_R25, TR("Hold ● to add by voice, or add one on the phone page.", "Giữ ● để thêm bằng giọng nói, hoặc thêm trên trang điện thoại."), 40, 454, 400, 33, 3, AL_CENTER);
    g_todoPages = 1;
    return;
  }
  std::vector<int> starts{0};  // first row of each page
  for (int i = 0, y = ROWS_TOP; i < int(o.size()); i++) {
    const int h = row_h(g_tasks[o[i]]);
    if (y + h > ROWS_BOTTOM) { starts.push_back(i); y = ROWS_TOP; }
    y += h;
  }
  g_todoPages = starts.size();
  g_todoPage = constrain(g_todoPage, 0, g_todoPages - 1);
  const int end = g_todoPage + 1 < g_todoPages ? starts[g_todoPage + 1] : o.size();
  const String now = now_key();
  for (int i = starts[g_todoPage], y = ROWS_TOP; i < end; i++) {
    const int h = row_h(g_tasks[o[i]]);
    todo_row(g_tasks[o[i]], y, h, now);
    g_rows.push_back({int16_t(y), int16_t(h), o[i]});
    y += h;
  }
  if (g_todoPages > 1) ui_page_counter(String(g_todoPage + 1) + "/" + g_todoPages);
}

inline int todo_row_at(int y) {
  for (const RowHit &r : g_rows) if (y >= r.y && y < r.y + r.h) return r.idx;
  return -1;
}

// ---------- 02e Task: read-only text, DEADLINE drop-down (Date / Time), Created, Delete | Done ----------
// Deadline = Date and Time, each on/off. Both off = none; Time only = today at that time;
// Date only = a date-only @due.
struct Deadline {
  bool dateOn = false, timeOn = false;
  long sel = 0;     // chosen day (day_num)
  int mins = 9 * 60;
  int y = 2026, m = 1;  // month shown in 02c
};
struct TaskEdit {
  int idx = -1;
  String text, created;
  Deadline dl, dl0;     // dl0: as opened, to tell an edit from no change
  bool dlOpen = false;  // DEADLINE drop-down shows Date and Time
};
inline TaskEdit g_ed;
inline Deadline g_dlUndo;  // restored by Cancel in 02c / 02h
constexpr int FIELD_X = 40, FIELD_Y = 132, FIELD_W = 400, FIELD_H = 76, ROW_H = 72;

inline Deadline dl_from(const String &due) {
  Deadline d;
  const String now = now_key();
  d.dateOn = due.length() > 0;
  d.timeOn = due.length() == 16;
  d.sel = ymd_num(due.length() ? due : now.length() ? now : String("2026-01-01"));
  if (d.timeOn) d.mins = due.substring(11, 13).toInt() * 60 + due.substring(14).toInt();
  int day;
  civil(d.sel, d.y, d.m, day);
  return d;
}

inline bool dl_same(const Deadline &a, const Deadline &b) {  // same deadline (the month shown doesn't count)
  return a.dateOn == b.dateOn && a.timeOn == b.timeOn && (!a.dateOn || a.sel == b.sel) && (!a.timeOn || a.mins == b.mins);
}

inline bool dl_due(const Deadline &d, String &due) {  // false: Time only, but no clock for "today" yet
  if (!d.dateOn && !d.timeOn) { due = ""; return true; }
  const String now = now_key();
  if (!d.dateOn && !now.length()) return false;
  due = (d.dateOn ? ymd(d.sel) : now.substring(0, 10)) + (d.timeOn ? " " + hhmm(d.mins) : String(""));
  return true;
}

inline String dmy(long n) {  // "17/11/26"
  int y, m, d;
  civil(n, y, m, d);
  char b[9];
  snprintf(b, sizeof b, "%02d/%02d/%02d", d, m, y % 100);
  return b;
}

// Settings-style rows (Figma Row/...): Bold 28 label; value + chevron, or a toggle.
inline void link_row(int y, const String &label, const String &value) {
  ui_text(F_B28, label, 40, y + 18, 37);
  ui_text(F_R25, value, 402, y + 20, 33, AL_RIGHT);
  ui_icon(ICON_CHEV, 414, y + 23);
}
inline void toggle_row(int y, const String &label, bool on) {
  ui_text(F_B28, label, 40, y + 18, 37);
  ui_toggle(372, y + 17, on);
}

// The task, wrapped, in its box; the rows below follow its height. 6 lines still leave room for the open drop-down.
inline int g_fieldH = FIELD_H;
constexpr int FIELD_LINES = 6;
inline int row0() { return FIELD_Y + g_fieldH + 10; }  // DEADLINE row; Date, Time, Created follow 80 px apart
inline int date_y() { return row0() + 80; }
inline int time_y() { return row0() + 160; }

inline void chevron_down(int x, int y) {  // ICON_CHEV turned down (26 x 26 box), 3 px stroke
  for (int t = 0; t < 3; t++) { display.drawLine(x + 6, y + 9 + t, x + 13, y + 16 + t, INK); display.drawLine(x + 13, y + 16 + t, x + 20, y + 9 + t, INK); }
}

inline void draw_task_editor() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Task", "Việc"), TR("Cancel", "Huỷ"));
  std::vector<String> ls = ui_lines(F_B26, g_ed.text, FIELD_W - 2 * 19 - 6, FIELD_LINES);
  if (ls.empty()) ls.push_back("");
  g_fieldH = FIELD_H + 34 * (int(ls.size()) - 1);
  ui_card(FIELD_X, FIELD_Y, FIELD_W, g_fieldH, 10, 3);
  for (int i = 0; i < int(ls.size()); i++) ui_text(F_B26, ls[i], 59, 153 + 34 * i, 34);
  int y = row0();
  ui_text(F_B25, TR("DEADLINE (OPTIONAL)", "HẠN CHÓT (TUỲ CHỌN)"), 40, y + 19, 33);
  if (g_ed.dlOpen) chevron_down(414, y + 23);
  else ui_icon(ICON_CHEV, 414, y + 23);
  y += 80;
  if (g_ed.dlOpen) {
    link_row(y, TR("Date", "Ngày"), g_ed.dl.dateOn ? dmy(g_ed.dl.sel) : TR("Off", "Tắt"));
    link_row(y + 80, TR("Time", "Giờ"), g_ed.dl.timeOn ? hhmm(g_ed.dl.mins) : TR("Off", "Tắt"));
    y += 160;
  }
  if (g_ed.created.length()) {
    ui_text(F_B28, TR("Created", "Ngày tạo"), 40, y + 18, 37);
    ui_text(F_R25, dmy(ymd_num(g_ed.created)), 440, y + 20, 33, AL_RIGHT);
  }
  ui_button(40, 717, 194, 67, false, &ICON_TRASH24, TR("Delete", "Xoá"));
  ui_button(246, 717, 194, 67, true, &ICON_CHECK24, TR("Done", "Xong"));
}

// ---------- 02c Date ----------
constexpr int CAL_TOP = 280, CELL_W = 57, CELL_H = 48;

inline int cal_x(int col) { return 40 + col * 400 / 7; }
inline long month_first() { return day_num(g_ed.dl.y, g_ed.dl.m, 1); }
inline int month_lead() { return (weekday(month_first()) + 6) % 7; }  // Monday-first column of day 1
inline int month_days() { return day_num(g_ed.dl.y + (g_ed.dl.m == 12), g_ed.dl.m % 12 + 1, 1) - month_first(); }

inline void date_month(int step) {
  Deadline &d = g_ed.dl;
  d.m += step;
  if (d.m < 1) { d.m = 12; d.y--; }
  if (d.m > 12) { d.m = 1; d.y++; }
}

inline void draw_date() {
  const Deadline &dl = g_ed.dl;
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Date", "Ngày"), TR("Cancel", "Huỷ"));
  toggle_row(112, TR("Date", "Ngày"), dl.dateOn);
  if (dl.dateOn) {
    ui_icon(ICON_LEFT30, 40, 196);
    ui_text(F_X25, (g_vi ? "Tháng " + String(dl.m) : String(MONTHS[dl.m - 1])) + " " + dl.y, W / 2, 194, 33, AL_CENTER);
    ui_icon(ICON_RIGHT30, 410, 196);
    static const char *const WD[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"}, *const WD_VI[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};
    for (int c = 0; c < 7; c++) ui_text(F_B25, TR(WD[c], WD_VI[c]), cal_x(c) + CELL_W / 2, 237, 33, AL_CENTER);
    const String now = now_key();
    const long first = month_first(), today = now.length() ? ymd_num(now) : -9;
    for (int d = 1, n = month_days(); d <= n; d++) {
      const int cell = month_lead() + d - 1, x = cal_x(cell % 7), y = CAL_TOP + cell / 7 * CELL_H;
      const bool sel = first + d - 1 == dl.sel, tod = first + d - 1 == today;
      if (sel) display.fillRoundRect(x, y, CELL_W, CELL_H, 8, INK);
      else if (tod) ui_card(x, y, CELL_W, CELL_H, 8, 2);
      ui_text(sel || tod ? F_X25 : F_R25, String(d), x + CELL_W / 2, y + 8, 33, AL_CENTER, sel ? PAPER : INK, sel ? INK : PAPER);
    }
  }
  ui_button(40, 711, 400, 73, true, nullptr, TR("Done", "Xong"));
}

inline bool date_day_at(int x, int y) {  // tap on a day cell -> select it
  if (x < 40 || x >= 440 || y < CAL_TOP || y >= CAL_TOP + 6 * CELL_H) return false;
  const int d = (y - CAL_TOP) / CELL_H * 7 + (x - 40) * 7 / 400 - month_lead() + 1;
  if (d < 1 || d > month_days()) return false;
  g_ed.dl.sel = month_first() + d - 1;
  return true;
}

// ---------- 02h Time: HH:MM fields + number pad ----------
inline TimeEntry g_te;
constexpr int TF_Y = 224, TF_W = 140, TF_H = 100;
inline const int TF_X[2] = {65, 276};

inline void time_begin() {
  g_te = TimeEntry();
  const String t = hhmm(g_ed.dl.mins);
  g_te.f[0] = t.substring(0, 2);
  g_te.f[1] = t.substring(3);
}

inline void time_commit() { g_ed.dl.mins = g_te.f[0].toInt() * 60 + g_te.f[1].toInt(); }  // "" counts as 0

inline void time_fields() {
  display.fillRect(40, TF_Y, 400, TF_H, PAPER);
  for (int i = 0; i < 2; i++) {
    const bool on = i == g_te.active;
    if (on) display.fillRoundRect(TF_X[i], TF_Y, TF_W, TF_H, 12, INK);
    else ui_card(TF_X[i], TF_Y, TF_W, TF_H, 12, 3);
    ui_text(F_X64, g_te.f[i], TF_X[i] + TF_W / 2, TF_Y + 8, 84, AL_CENTER, on ? PAPER : INK, on ? INK : PAPER);
  }
  ui_text(F_X64, ":", W / 2, TF_Y + 8, 84, AL_CENTER);
}

// Keypad keys 0..11 = 1-9, backspace, 0, Done (Figma Keypad: 3 x 146 px columns, 68 px rows).
inline int pad_x(int i) { return 12 + i % 3 * 155; }
inline int pad_y(int i) { return 524 + i / 3 * 68; }

inline void draw_keypad() {
  display.fillRect(0, KB_TOP, W, KB_H, PAPER);
  display.fillRect(0, KB_TOP, W, 2, INK);
  for (int i = 0; i < 12; i++) {
    const int x = pad_x(i), y = pad_y(i);
    if (i == 11) {
      display.fillRoundRect(x, y, 146, 60, 6, INK);
      ui_icon(ICON_KB_ENTER, x + 61, y + 18, PAPER);  // ↵ only, like the letter keyboard
      continue;
    }
    ui_card(x, y, 146, 60, 6, 2);
    if (i == 9) ui_icon(ICON_KB_BACKSPACE, x + 58, y + 15);
    else ui_text(F_B25, String(i == 10 ? 0 : i + 1), x + 73, y + 14, 33, AL_CENTER);
  }
}

inline int keypad_hit(int x, int y) {
  if (y < 520 || x < 8) return -1;
  const int c = min((x - 8) / 155, 2), r = min((y - 520) / 68, 3);
  return r * 3 + c;
}

inline void draw_time() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Time", "Giờ"), TR("Cancel", "Huỷ"));
  toggle_row(112, TR("Time", "Giờ"), g_ed.dl.timeOn);
  if (g_ed.dl.timeOn) {
    time_fields();
    draw_keypad();
  } else {
    ui_button(40, 711, 400, 73, true, nullptr, TR("Done", "Xong"));
  }
}

// ---------- 02f Delete task / 03f Delete note ----------
inline void draw_delete(const char *question, const char *action = nullptr) {  // also 02j Discard changes
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  display.fillCircle(240, 337, 80, INK);  // badge 160x160, 5 px ring
  display.fillCircle(240, 337, 75, PAPER);
  ui_icon(ICON_TRASH64, 208, 305);
  ui_text(F_X40, question, W / 2, 435, 53, AL_CENTER);
  ui_button(40, 705, 193, 79, false, &ICON_TRASH24, action ? action : TR("Delete", "Xoá"));
  ui_button(247, 705, 193, 79, true, &ICON_X24, TR("Cancel", "Huỷ"));
}

// ---------- No microSD card (Figma 02k To-do / 03j Notes) ----------
inline bool g_nosdNotes = false;  // which app sent us here: Retry returns to it
inline void draw_nosd() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(g_nosdNotes ? TR("Notes", "Ghi chú") : TR("To-do", "Việc cần làm"), "");
  ui_icon(ICON_SD64, 208, 301);
  ui_text(F_X30, TR("No microSD card", "Không có thẻ SD"), W / 2, 381, 40, AL_CENTER);
  ui_wrap(F_R25, TR("Insert a FAT32 card, then tap Retry.", "Lắp thẻ FAT32, rồi chạm Thử lại."), 40, 437, 400, 33, 2, AL_CENTER);
  ui_button(40, 712, 400, 72, true, &ICON_RETRY30, TR("Retry", "Thử lại"), 10);
}

// Figma 02l / 03k: shown at once while a card that is in but not mounted yet is read (the spin icon's grey track drops out on 1-bit).
inline void draw_sd_loading(bool notes) {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(notes ? TR("Notes", "Ghi chú") : TR("To-do", "Việc cần làm"), "");
  ui_icon(ICON_SPIN64, 208, 342);
  ui_text(F_X30, TR("Loading…", "Đang tải…"), W / 2, 422, 40, AL_CENTER);
  ui_partial(0, 0, W, H);
}
