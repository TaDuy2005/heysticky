// HeySticky on-screen keyboard (Figma 02a): y 510..800, letters / 123 pages, one-shot shift.
#pragma once
#include "ui.h"

enum KeyKind : uint8_t { K_CHAR, K_SHIFT, K_BKSP, K_MODE, K_SPACE, K_DONE };
struct KbKey { int16_t x, y, w; KeyKind kind; char ch; };

constexpr int KB_TOP = 510, KB_H = H - KB_TOP;
inline const char *KB_ROWS[2][3] = {{"qwertyuiop", "asdfghjkl", "zxcvbnm"},
                                    {"1234567890", "-/:;()&@\"", ".,?!'#+"}};

struct Keyboard {
  bool numbers = false, shift = false;
  bool enter = false;  // "↵ Done" (multi-line field: tap = new line, hold = hide) instead of "Done"

  // Builds the key list for the current page (max 31 keys).
  int keys(KbKey *out) const {
    int n = 0;
    const char *const *rows = KB_ROWS[numbers];
    const int16_t x0[3] = {12, 35, 81}, y0[3] = {524, 592, 660};
    for (int r = 0; r < 3; r++)
      for (int i = 0; rows[r][i]; i++) out[n++] = {int16_t(x0[r] + 46 * i), y0[r], 42, K_CHAR, rows[r][i]};
    if (!numbers) out[n++] = {13, 660, 64, K_SHIFT, 0};
    out[n++] = {403, 660, 64, K_BKSP, 0};
    out[n++] = {14, 728, 88, K_MODE, 0};
    out[n++] = {106, 728, 260, K_SPACE, ' '};
    out[n++] = {370, 728, 96, K_DONE, 0};
    return n;
  }

  void drawKey(const KbKey &k) const {
    const bool inv = k.kind == K_DONE || (k.kind == K_SHIFT && shift);
    if (inv) display.fillRoundRect(k.x, k.y, k.w, 60, 6, INK);
    else ui_card(k.x, k.y, k.w, 60, 6, 2);
    const uint16_t fg = inv ? PAPER : INK, bg = inv ? INK : PAPER;
    const int cx = k.x + k.w / 2;
    switch (k.kind) {
      case K_CHAR: ui_text(F_B25, String(shift ? char(toupper(k.ch)) : k.ch), cx, k.y + 14, 33, AL_CENTER, fg, bg); break;
      case K_SHIFT: ui_icon(ICON_KB_SHIFT, k.x + 18, k.y + 16, fg); break;
      case K_BKSP: ui_icon(ICON_KB_BACKSPACE, k.x + 17, k.y + 15, fg); break;
      case K_MODE: ui_text(F_B25, numbers ? "ABC" : "123", cx, k.y + 14, 33, AL_CENTER, fg, bg); break;
      case K_SPACE: ui_text(F_R25, "space", cx, k.y + 14, 33, AL_CENTER, fg, bg); break;
      case K_DONE:
        if (!enter) { ui_text(F_X25, "Done", cx, k.y + 14, 33, AL_CENTER, fg, bg); break; }
        ui_icon(ICON_KB_ENTER, k.x + 3, k.y + 18, fg);
        ui_text(F_X25, "Done", k.x + 33, k.y + 14, 33, AL_LEFT, fg, bg);
        break;
    }
  }

  void draw() const {
    display.fillRect(0, KB_TOP, W, KB_H, PAPER);
    display.fillRect(0, KB_TOP, W, 2, INK);  // Figma: 2 px top border
    KbKey ks[32];
    const int n = keys(ks);
    for (int i = 0; i < n; i++) drawKey(ks[i]);
  }

  // Key under the finger (gaps between keys count toward the nearest key in the row).
  bool hit(int x, int y, KbKey &out) const {
    KbKey ks[32];
    const int n = keys(ks);
    for (int i = 0; i < n; i++)
      if (x >= ks[i].x - 2 && x < ks[i].x + ks[i].w + 2 && y >= ks[i].y - 4 && y < ks[i].y + 64) { out = ks[i]; return true; }
    return false;
  }
};

// One key into a text field. Shift / 123 redraw the keyboard here; the caller refreshes the field.
constexpr int KBD_FULL_EVERY = 20;  // full refresh after this many typing refreshes
enum KbResult : uint8_t { KB_TEXT, KB_NONE, KB_DONE };

inline KbResult kb_apply(Keyboard &kb, String &s, unsigned cap, const KbKey &k) {
  switch (k.kind) {
    case K_CHAR:
    case K_SPACE:
      if (s.length() < cap) s += kb.shift ? char(toupper(k.ch)) : k.ch;
      if (kb.shift) { kb.shift = false; kb.draw(); ui_partial(0, KB_TOP, W, KB_H, KBD_FULL_EVERY); }
      return KB_TEXT;
    case K_BKSP:
      if (s.length()) utf8_pop(s);
      return KB_TEXT;
    case K_SHIFT: kb.shift = !kb.shift; break;
    case K_MODE: kb.numbers = !kb.numbers; kb.shift = false; break;
    case K_DONE: return KB_DONE;
  }
  kb.draw();
  ui_partial(0, KB_TOP, W, KB_H, KBD_FULL_EVERY);
  return KB_NONE;
}
