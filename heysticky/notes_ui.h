// Notes screens, Figma 03-03g: list (03/03g), note view (03c). Notes are written by voice or on the phone page.
// 03f reuses draw_delete() from todo_ui.h. Drawing and hit areas only; screens.h navigates.
#pragma once
#include "ui.h"
#include "card.h"
#include "todo_ui.h"  // dmy()

inline String note_title(const String &title, int num) { return title.length() ? title : TR("Note ", "Ghi chú ") + note_tag(num); }
inline std::vector<String> wrap(const Font &f, const String &s, int w) {
  return wrap_lines(s, w, [&](const String &t) { return ui_width(f, t); });
}

// ---------- 03 Notes list / 03g empty ----------
constexpr int NROW_TOP = 112, NROW_H = 96, NROWS = 6;  // 6 rows end at 688, above the page counter
inline int g_notesPage = 0;

inline int notes_pages() { return max(1, (int(g_notes.size()) + NROWS - 1) / NROWS); }

inline void draw_notes() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Notes", "Ghi chú"), String(g_notes.size()));
  if (g_notes.empty()) {
    ui_icon(ICON_NOTE64, 208, 301);
    ui_text(F_X30, TR("No notes yet", "Chưa có ghi chú"), W / 2, 381, 40, AL_CENTER);
    ui_wrap(F_R25, TR("Hold ● to add by voice, or add one on the phone page.", "Giữ ● để thêm bằng giọng nói, hoặc thêm trên trang điện thoại."), 40, 437, 400, 33, 3, AL_CENTER);
    return;
  }
  g_notesPage = constrain(g_notesPage, 0, notes_pages() - 1);
  for (int r = 0; r < NROWS; r++) {
    const int i = g_notesPage * NROWS + r, y = NROW_TOP + r * NROW_H;
    if (i >= int(g_notes.size())) break;
    const NoteHead &n = g_notes[i];
    ui_text(F_X25, ui_fit(F_X25, note_title(n.title, n.num), 343), 40, y + 13, 33);
    ui_text(F_R25, note_tag(n.num), 440, y + 13, 33, AL_RIGHT);
    ui_text(F_R25, ui_fit(F_R25, n.preview, 400), 40, y + 50, 33);
    display.fillRect(40, y + NROW_H - 1, 400, 1, INK);
  }
  if (notes_pages() > 1) ui_page_counter(String(g_notesPage + 1) + "/" + notes_pages());
}

inline int notes_row_at(int y) {  // note number under y, or 0
  if (y < NROW_TOP || y >= NROW_TOP + NROWS * NROW_H) return 0;
  const int i = g_notesPage * NROWS + (y - NROW_TOP) / NROW_H;
  return i < int(g_notes.size()) ? g_notes[i].num : 0;
}

// ---------- 03c Note view: the title and created date stay; the body scrolls a box-full (minus one line) ----------
constexpr int VIEW_TOP = 136, VIEW_BOTTOM = 704, VIEW_LH = 38, VTITLE_LH = 40, VTITLE_MAX = 4;
inline int g_viewTop = 0, g_viewMax = 0, g_viewStep = 1;  // first body line shown; scroll limits (set when drawn)

inline void draw_note_view() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Note", "Ghi chú"), note_tag(g_note.num));
  std::vector<String> title = wrap(F_X30, note_title(g_note.title, g_note.num), 400);
  if (title.size() > VTITLE_MAX) { title.resize(VTITLE_MAX); title.back() = ui_fit(F_X30, title.back() + " …", 400); }
  for (size_t i = 0; i < title.size(); i++) ui_text(F_X30, title[i], 40, VIEW_TOP + i * VTITLE_LH, VTITLE_LH);
  int top = VIEW_TOP + title.size() * VTITLE_LH + 4;
  if (g_note.created.length()) { ui_text(F_R20, dmy(ymd_num(g_note.created)), 440, top, 26, AL_RIGHT); top += 30; }
  top += 8;
  const std::vector<String> body = wrap(F_R25, g_note.body, 400);
  const int n = body.size(), shown = max(1, (VIEW_BOTTOM - top) / VIEW_LH);
  g_viewMax = max(0, n - shown);
  g_viewStep = max(1, shown - 1);
  g_viewTop = constrain(g_viewTop, 0, g_viewMax);
  for (int i = 0; i < shown && g_viewTop + i < n; i++) ui_text(F_R25, body[g_viewTop + i], 40, top + i * VIEW_LH, VIEW_LH);
  if (n > shown) ui_scrollbar(top, shown * VIEW_LH, g_viewTop, shown, n);
  ui_button(40, 720, 400, 64, false, &ICON_TRASH24, TR("Delete", "Xoá"));
}
