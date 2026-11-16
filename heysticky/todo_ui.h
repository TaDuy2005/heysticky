// To-do screens, Figma 02-02g: list (02/02g), task editor (02a/b/d/e), deadline picker (02c),
// delete confirm (02f), plus the no-card screen. Drawing and hit areas only; screens.h navigates.
#pragma once
#include "ui.h"
#include "kbd.h"
#include "card.h"

inline String now_key() {  // "YYYY-MM-DD HH:MM", "" before NTP
  tm t;
  if (!getLocalTime(&t, 0)) return "";
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
inline int g_todoPage = 0, g_todoPages = 1, g_addBtnY = 712;
constexpr int ROWS_TOP = 112, ROWS_BOTTOM = 664;

inline int row_h(const Task &t) { return t.due.length() && !t.done ? 93 : 61; }

inline void todo_row(const Task &t, int y, int h, const String &now) {
  const bool two = h > 61;
  const int cb = y + (two ? 28 : 12);
  if (t.done) {
    display.fillRoundRect(40, cb, 36, 36, 4, INK);
    ui_icon(ICON_CHECK26, 45, cb + 5, PAPER);
  } else {
    ui_card(40, cb, 36, 36, 4, 3);
  }
  const Font &f = t.done ? F_R25 : F_B25;
  const int ty = y + (two ? 12 : 14);
  const int tw = ui_text(f, ui_fit(f, t.text, 346), 94, ty, 33);
  if (t.done) display.fillRect(94, ty + 16, tw, 2, INK);  // strikethrough
  if (two) ui_text(F_B25, todo_label(t, now), 94, y + 47, 33);
  display.fillRect(40, y + h - 1, 400, 1, INK);
}

inline void draw_todo() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("To-do", String(todo_left(g_tasks)) + " left");
  g_rows.clear();
  const std::vector<int> o = todo_order(g_tasks);
  if (o.empty()) {
    ui_icon(ICON_CHECK_CIRCLE64, 208, 318);
    ui_text(F_X30, "No tasks yet", W / 2, 398, 40, AL_CENTER);
    ui_text(F_R25, "Tap Add task to make one.", W / 2, 454, 33, AL_CENTER);
    g_todoPages = 1;
    ui_button(40, g_addBtnY = 712, 400, 72, true, &ICON_PLUS30, "Add task", 10);
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
  const bool multi = g_todoPages > 1;
  ui_button(40, g_addBtnY = multi ? 674 : 712, 400, 72, true, &ICON_PLUS30, "Add task", 10);
  if (multi) ui_page_counter(String(g_todoPage + 1) + "/" + g_todoPages);
}

inline int todo_row_at(int y) {
  for (const RowHit &r : g_rows) if (y >= r.y && y < r.y + r.h) return r.idx;
  return -1;
}

// ---------- 02a/02b New task, 02d/02e Edit task ----------
struct TaskEdit {
  int idx = -1;  // -1 = new task
  String text, due;
  bool kbOpen = true, dirty = false;
  uint32_t dirtyAt = 0;
  Keyboard kb;
};
inline TaskEdit g_ed;
constexpr int FIELD_X = 40, FIELD_Y = 132, FIELD_W = 400, FIELD_H = 76;
constexpr int TODAY_DUE_HOUR = 18;  // "Today" chip: 18:00, or the next full hour after that

struct Chip { int16_t x, y, w; };
inline Chip g_chips[4];  // None, Today, Tomorrow, Pick date

inline void editor_field() {
  ui_card(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, 10, 3);
  const int maxW = FIELD_W - 2 * 19 - 6;  // show the tail when the text is wider than the field
  String shown = g_ed.text;
  bool cut = false;
  while (shown.length() && ui_width(F_B26, (cut ? "…" : "") + shown) > maxW) {
    int n = 1;
    while (n < int(shown.length()) && (uint8_t(shown[n]) & 0xC0) == 0x80) n++;
    shown.remove(0, n);
    cut = true;
  }
  if (cut) shown = "…" + shown;
  const int tw = ui_text(F_B26, shown, 59, 153, 34);
  if (g_ed.kbOpen) display.fillRect(59 + tw + 2, 154, 3, 32, INK);  // cursor
}

inline int chip_selected(const String &now) {
  if (!g_ed.due.length()) return 0;
  const long d = ymd_num(g_ed.due), today = now.length() ? ymd_num(now) : -9;
  return d == today ? 1 : d == today + 1 ? 2 : 3;
}

inline void editor_chips() {
  display.fillRect(40, 259, 400, 122, PAPER);
  const String now = now_key(), hm = short_hm(g_ed.due), sp = hm.length() ? " " : "";
  const int sel = chip_selected(now);
  const String label[4] = {"None", sel == 1 ? "Today" + sp + hm : "Today", sel == 2 ? "Tomorrow" + sp + hm : "Tomorrow",
                           sel == 3 ? day_month(ymd_num(g_ed.due)) + sp + hm : "Pick date"};
  int x = 40, y = 259;
  for (int k = 0; k < 4; k++) {
    const int w = ui_width(F_B25, label[k]) + 32 + (k == 3 ? 26 : 0);
    if (x + w > 440) { x = 40; y = 324; }
    const bool on = k == sel;
    if (on) display.fillRoundRect(x, y, w, 57, 22, INK);
    else ui_card(x, y, w, 57, 22, 2);
    int tx = x + 16;
    if (k == 3) { ui_icon(ICON_CAL, tx, y + 19, on ? PAPER : INK); tx += 26; }
    ui_text(F_B25, label[k], tx, y + 12, 33, AL_LEFT, on ? PAPER : INK, on ? INK : PAPER);
    g_chips[k] = {int16_t(x), int16_t(y), int16_t(w)};
    x += w + 8;
  }
}

inline int chip_at(int x, int y) {
  for (int k = 0; k < 4; k++)
    if (x >= g_chips[k].x && x < g_chips[k].x + g_chips[k].w && y >= g_chips[k].y && y < g_chips[k].y + 57) return k;
  return -1;
}

// Chip 1/2: today at TODAY_DUE_HOUR (or the next hour, if later) / tomorrow 09:00. Needs the clock.
inline bool chip_due(int k, String &due) {
  const String now = now_key();
  if (!now.length()) return false;
  if (k == 2) { due = ymd(ymd_num(now) + 1) + " 09:00"; return true; }
  const int h = max(TODAY_DUE_HOUR, int(now.substring(11, 13).toInt()) + 1);
  due = now.substring(0, 10) + (h > 23 ? String(" 23:59") : " " + String(h) + ":00");
  return true;
}

inline void draw_task_editor() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(g_ed.idx < 0 ? "New task" : "Edit task", "Cancel");
  editor_field();
  ui_text(F_B25, "DEADLINE (OPTIONAL)", 40, 218, 33);
  editor_chips();
  if (g_ed.kbOpen) {
    g_ed.kb.draw();
  } else if (g_ed.idx < 0) {
    ui_button(40, 717, 400, 67, true, &ICON_CHECK24, "Save");
  } else {
    ui_button(40, 717, 194, 67, false, &ICON_TRASH24, "Delete");
    ui_button(246, 717, 194, 67, true, &ICON_CHECK24, "Save");
  }
}

// ---------- 02c Deadline picker ----------
struct Picker { int y, m, mins; long sel; };
inline Picker g_pk;
constexpr int CAL_TOP = 212, CELL_W = 57, CELL_H = 48, TIME_STEP = 15;

inline int cal_x(int col) { return 40 + col * 400 / 7; }
inline long month_first() { return day_num(g_pk.y, g_pk.m, 1); }
inline int month_lead() { return (weekday(month_first()) + 6) % 7; }  // Monday-first column of day 1
inline int month_days() { return day_num(g_pk.y + (g_pk.m == 12), g_pk.m % 12 + 1, 1) - month_first(); }

inline void picker_open() {
  const String now = now_key();
  g_pk.sel = ymd_num(g_ed.due.length() ? g_ed.due : now.length() ? now : String("2026-01-01"));
  int d;
  civil(g_pk.sel, g_pk.y, g_pk.m, d);
  g_pk.mins = g_ed.due.length() == 16 ? g_ed.due.substring(11, 13).toInt() * 60 + g_ed.due.substring(14).toInt() : 9 * 60;
}

inline void picker_month(int step) {
  g_pk.m += step;
  if (g_pk.m < 1) { g_pk.m = 12; g_pk.y--; }
  if (g_pk.m > 12) { g_pk.m = 1; g_pk.y++; }
}

inline String picker_due() { return ymd(g_pk.sel) + " " + hhmm(g_pk.mins); }

inline void draw_picker() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("Deadline", "Clear");
  ui_icon(ICON_LEFT30, 40, 128);
  ui_text(F_X25, String(MONTHS[g_pk.m - 1]) + " " + g_pk.y, W / 2, 126, 33, AL_CENTER);
  ui_icon(ICON_RIGHT30, 410, 128);
  static const char *const WD[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
  for (int c = 0; c < 7; c++) ui_text(F_B25, WD[c], cal_x(c) + CELL_W / 2, 169, 33, AL_CENTER);
  const String now = now_key();
  const long first = month_first(), today = now.length() ? ymd_num(now) : -9;
  for (int d = 1, n = month_days(); d <= n; d++) {
    const int cell = month_lead() + d - 1, x = cal_x(cell % 7), y = CAL_TOP + cell / 7 * CELL_H;
    const bool sel = first + d - 1 == g_pk.sel, tod = first + d - 1 == today;
    if (sel) display.fillRoundRect(x, y, CELL_W, CELL_H, 8, INK);
    else if (tod) ui_card(x, y, CELL_W, CELL_H, 8, 2);
    ui_text(sel || tod ? F_X25 : F_R25, String(d), x + CELL_W / 2, y + 8, 33, AL_CENTER, sel ? PAPER : INK, sel ? INK : PAPER);
  }
  ui_card(40, 510, 400, 69, 12, 2);
  ui_icon(ICON_CLOCK28, 58, 531);
  ui_text(F_B25, "Time", 98, 528, 33);
  ui_icon(ICON_DOWN30, 236, 530);
  ui_text(F_X34, hhmm(g_pk.mins), 278, 522, 45);
  ui_icon(ICON_UP30, 392, 530);
  display.fillRoundRect(40, 711, 400, 73, 12, INK);
  ui_text(F_X25, String("Set · ") + DAY3[weekday(g_pk.sel)] + " " + day_month(g_pk.sel) + ", " + hhmm(g_pk.mins),
          W / 2, 731, 33, AL_CENTER, PAPER, INK);
}

inline bool picker_day_at(int x, int y) {  // tap on a day cell -> select it
  if (x < 40 || x >= 440 || y < CAL_TOP || y >= CAL_TOP + 6 * CELL_H) return false;
  const int d = (y - CAL_TOP) / CELL_H * 7 + (x - 40) * 7 / 400 - month_lead() + 1;
  if (d < 1 || d > month_days()) return false;
  g_pk.sel = month_first() + d - 1;
  return true;
}

// ---------- 02f Delete task ----------
inline void draw_delete_task() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  display.fillCircle(240, 237, 80, INK);
  display.fillCircle(240, 237, 75, PAPER);
  ui_icon(ICON_TRASH64, 208, 205);
  ui_text(F_X40, "Delete task?", W / 2, 335, 53, AL_CENTER);
  ui_card(40, 406, 400, 182, 14, 3);
  ui_text(F_X25, ui_fit(F_X25, g_ed.text, 354), 63, 425, 33);
  ui_wrap(F_R25, "It will be removed from /todo.md on the SD card.", 63, 464, 354, 35, 3);
  ui_button(40, 705, 193, 79, false, &ICON_TRASH24, "Delete");
  ui_button(247, 705, 193, 79, true, &ICON_X24, "Cancel");
}

// ---------- No microSD card ----------
// ponytail: no Figma frame yet (PLAN.md §10 open question); plain layout in the house style.
inline void draw_nosd() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar("To-do", "Back");
  ui_text(F_X30, "No microSD card", W / 2, 318, 40, AL_CENTER);
  ui_wrap(F_R25, "Insert a FAT32 card, then tap Retry.", 40, 374, 400, 35, 3, AL_CENTER);
  ui_button(40, 712, 400, 72, true, nullptr, "Retry");
}
