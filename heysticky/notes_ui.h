// Notes screens, Figma 03-03g: list (03/03g), note view (03c), note editor (03a/b/d/e).
// 03f reuses draw_delete() from todo_ui.h. Drawing and hit areas only; screens.h navigates.
#pragma once
#include "ui.h"
#include "kbd.h"
#include "card.h"

inline String note_title(const String &title, int num) { return title.length() ? title : TR("Note ", "Ghi chú ") + note_tag(num); }
inline std::vector<String> wrap(const Font &f, const String &s, int w) {
  return wrap_lines(s, w, [&](const String &t) { return ui_width(f, t); });
}

// ---------- 03 Notes list / 03g empty ----------
constexpr int NROW_TOP = 112, NROW_H = 96, NROWS = 5;
inline int g_notesPage = 0, g_newBtnY = 712;

inline int notes_pages() { return max(1, (int(g_notes.size()) + NROWS - 1) / NROWS); }

inline void draw_notes() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Notes", "Ghi chú"), String(g_notes.size()));
  if (g_notes.empty()) {
    ui_icon(ICON_NOTE64, 208, 301);
    ui_text(F_X30, TR("No notes yet", "Chưa có ghi chú"), W / 2, 381, 40, AL_CENTER);
    ui_wrap(F_R25, TR("Tap New note, or hold ● to speak one.", "Chạm Ghi chú mới, hoặc giữ ● để nói."), 40, 437, 400, 33, 2, AL_CENTER);
    ui_button(40, g_newBtnY = 712, 400, 72, true, &ICON_PLUS30, TR("New note", "Ghi chú mới"), 10);
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
  const bool multi = notes_pages() > 1;
  ui_button(40, g_newBtnY = multi ? 674 : 712, 400, 72, true, &ICON_PLUS30, TR("New note", "Ghi chú mới"), 10);
  if (multi) ui_page_counter(String(g_notesPage + 1) + "/" + notes_pages());
}

inline int notes_row_at(int y) {  // note number under y, or 0
  if (y < NROW_TOP || y >= NROW_TOP + NROWS * NROW_H) return 0;
  const int i = g_notesPage * NROWS + (y - NROW_TOP) / NROW_H;
  return i < int(g_notes.size()) ? g_notes[i].num : 0;
}

// ---------- 03c Note view: title, then the body over as many pages as it needs ----------
constexpr int VIEW_TOP = 136, VIEW_BOTTOM = 666, VIEW_LH = 38, VTITLE_LH = 40, VTITLE_MAX = 4;
inline int g_viewPage = 0, g_viewPages = 1, g_viewBtnY = 720;

inline void draw_note_view() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Note", "Ghi chú"), note_tag(g_note.num));
  std::vector<String> title = wrap(F_X30, note_title(g_note.title, g_note.num), 400);
  if (title.size() > VTITLE_MAX) { title.resize(VTITLE_MAX); title.back() = ui_fit(F_X30, title.back() + " …", 400); }
  const std::vector<String> body = wrap(F_R25, g_note.body, 400);
  const int bodyTop = VIEW_TOP + title.size() * VTITLE_LH + 16;
  const int first = (VIEW_BOTTOM - bodyTop) / VIEW_LH, per = (VIEW_BOTTOM - VIEW_TOP) / VIEW_LH;  // lines per page
  g_viewPages = 1 + (int(body.size()) > first ? (int(body.size()) - first + per - 1) / per : 0);
  g_viewPage = constrain(g_viewPage, 0, g_viewPages - 1);
  int from = 0, y = VIEW_TOP, n = first;
  if (g_viewPage == 0) {
    for (size_t i = 0; i < title.size(); i++) ui_text(F_X30, title[i], 40, VIEW_TOP + i * VTITLE_LH, VTITLE_LH);
    y = bodyTop;
  } else {
    from = first + (g_viewPage - 1) * per;
    n = per;
  }
  for (int i = from; i < from + n && i < int(body.size()); i++, y += VIEW_LH) ui_text(F_R25, body[i], 40, y, VIEW_LH);
  const bool multi = g_viewPages > 1;
  g_viewBtnY = multi ? 682 : 720;
  ui_button(40, g_viewBtnY, 192, 64, true, &ICON_PENCIL24, TR("Edit", "Sửa"));
  ui_button(248, g_viewBtnY, 192, 64, false, &ICON_TRASH24, TR("Delete", "Xoá"));
  if (multi) ui_page_counter(String(g_viewPage + 1) + "/" + g_viewPages);
}

// ---------- 03a/03b New note, 03d/03e Edit note ----------
struct NoteEdit {
  Note n;         // a copy: g_note only changes on Save
  int field = 0;  // 0 title, 1 body
  bool touched[2] = {false, false};  // tapped or typed in: an empty field then shows only the cursor
  bool follow = true;                // body shows its last lines (typing); false: scrolled to `top`
  int top = 0;                       // first body line shown when !follow
  bool kbOpen = true, dirty = false;
  uint32_t dirtyAt = 0;
  Keyboard kb;
};
inline NoteEdit g_ne;
constexpr int NT_Y = 132, NT_H = 67, NB_Y = 211, NB_X = 56, NB_W = 368, NB_LH = 35;
constexpr int NB_BOTTOM_KB = 498, NB_BOTTOM = 701;  // body field bottom with / without the keyboard
inline int g_nbBottom = NB_BOTTOM;                  // body field bottom as drawn (tap target)
inline int g_nbMax = 0, g_nbStep = 1;               // body scroll limits, set by note_fields()

inline void note_fields() {
  const bool cur = g_ne.kbOpen;
  display.fillRect(0, 112, W, (cur ? KB_TOP : NB_BOTTOM + 1) - 112, PAPER);
  // Title (ExtraBold 25). Empty: Italic "Title" (cursor after it when focused) until tapped, then only the cursor.
  const bool titleCur = cur && g_ne.field == 0, titleHint = !g_ne.n.title.length() && !(titleCur && g_ne.touched[0]);
  ui_card(40, NT_Y, 400, NT_H, 10, 3);
  const String t = titleHint ? String(TR("Title", "Tiêu đề")) : titleCur ? ui_fit_tail(F_X25, g_ne.n.title, 360) : ui_fit(F_X25, g_ne.n.title, 360);
  const int tw = t.length() ? ui_text(titleHint ? F_I25 : F_X25, t, 57, 149, 33) : 0;
  if (titleCur) display.fillRect(57 + tw + 2, 150, 3, 32, INK);
  // Body (Regular 25, 35 px lines): grows with the text; while typing in it, the tail stays in view
  const bool bodyCur = cur && g_ne.field == 1, hint = !g_ne.n.body.length() && !bodyCur;
  const std::vector<String> lines = hint ? wrap(F_I25, TR("Start typing…\nor hold ● to speak and let AI write it.", "Bắt đầu gõ…\nhoặc giữ ● để nói và để AI viết."), NB_W)
                                         : wrap(F_R25, g_ne.n.body, NB_W);
  const int maxLines = ((cur ? NB_BOTTOM_KB : NB_BOTTOM) - NB_Y - 28) / NB_LH;
  const int n = lines.size(), shown = min(n, maxLines);
  g_ne.top = g_ne.follow ? n - shown : constrain(g_ne.top, 0, n - shown);
  g_nbMax = n - shown;
  g_nbStep = max(1, maxLines - 1);  // scroll a box-full, keeping one line for context
  g_nbBottom = NB_Y + 28 + max(shown, 3) * NB_LH;
  ui_card(40, NB_Y, 400, g_nbBottom - NB_Y, 10, 2);
  for (int i = 0; i < shown; i++) ui_text(hint ? F_I25 : F_R25, lines[g_ne.top + i], NB_X, NB_Y + 14 + i * NB_LH, NB_LH);
  if (bodyCur && g_ne.top + shown == n)  // cursor at the end, when the end is in view
    display.fillRect(NB_X + ui_width(F_R25, lines.back()) + 2, NB_Y + 14 + (shown - 1) * NB_LH + 1, 3, 32, INK);
  if (n > shown) {  // scrollbar: track + thumb inside the right edge
    const int ty = NB_Y + 12, th = g_nbBottom - NB_Y - 24, bh = max(24, th * shown / n);
    display.fillRect(431, ty, 1, th, INK);
    display.fillRect(429, ty + (th - bh) * g_ne.top / (n - shown), 5, bh, INK);
  }
}

inline void note_scroll(int dir) {  // -1 = earlier lines, +1 = later lines
  const int t = constrain(g_ne.top + dir * g_nbStep, 0, g_nbMax);
  if (t == g_ne.top) return;
  g_ne.follow = false;
  g_ne.top = t;
  g_ne.dirty = true;  // note_flush() redraws the fields with one partial refresh
  g_ne.dirtyAt = 0;
}

inline void draw_note_editor() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(g_ne.n.num ? TR("Edit note", "Sửa ghi chú") : TR("New note", "Ghi chú mới"), TR("Cancel", "Huỷ"));
  note_fields();
  if (g_ne.kbOpen) {
    g_ne.kb.enter = g_ne.field == 1;
    g_ne.kb.shift = cap_next(g_ne.field ? g_ne.n.body : g_ne.n.title);
    g_ne.kb.draw();
  } else if (!g_ne.n.num) {
    ui_button(40, 717, 400, 67, true, &ICON_CHECK24, TR("Save", "Lưu"));
  } else {
    ui_button(40, 717, 194, 67, false, &ICON_TRASH24, TR("Delete", "Xoá"));
    ui_button(246, 717, 194, 67, true, &ICON_CHECK24, TR("Save", "Lưu"));
  }
}
