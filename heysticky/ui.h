// HeySticky drawing helpers: JetBrains Mono text placed like Figma text boxes, icons,
// cards, status bar, page counter, partial-refresh bookkeeping.
#pragma once
#include <Seeed_GFX.h>
#include <WiFi.h>
#include "src/fonts/fonts.h"
#include "src/icons.h"
#include "hw.h"
#include "lang.h"

extern Seeed_GFX display;
constexpr int W = 480, H = 800;
constexpr uint16_t INK = TFT_BLACK, PAPER = TFT_WHITE;

enum Align : uint8_t { AL_LEFT, AL_CENTER, AL_RIGHT };

// Text in a Figma text box: x = left / centre / right edge, y + h = the box.
// The glyphs' line box (ascent + descent) is centred in the Figma box, like Figma does.
inline void ui_use(const Font &f) {
  static const Font *loaded = nullptr;
  if (loaded != &f) { display.loadFont(f.data); loaded = &f; }
}

inline int ui_width(const Font &f, String s) {
  ui_use(f);
  s.replace(" ", " ");
  return display.textWidth(s.c_str());
}

inline int ui_text(const Font &f, String s, int x, int y, int h, Align a = AL_LEFT, uint16_t fg = INK, uint16_t bg = PAPER) {
  ui_use(f);
  s.replace(" ", " ");  // the font ships U+00A0 with the real monospace advance
  display.setTextColor(fg, bg);
  display.setTextDatum(a == AL_LEFT ? TL_DATUM : a == AL_CENTER ? TC_DATUM : TR_DATUM);
  return display.drawString(s, x, y + (h - (f.asc + f.desc)) / 2);
}

inline void ui_icon(const Icon &i, int x, int y, uint16_t c = INK) { display.drawBitmap(x, y, i.bits, i.w, i.h, c); }

// Figma frame with an inside stroke.
inline void ui_card(int x, int y, int w, int h, int r, int stroke) {
  display.fillRoundRect(x, y, w, h, r, INK);
  display.fillRoundRect(x + stroke, y + stroke, w - 2 * stroke, h - 2 * stroke, max(r - stroke, 0), PAPER);
}

// Dashed version (Figma dash 8/6): solid outline, then paper gaps along the straight edges.
inline void ui_card_dashed(int x, int y, int w, int h, int r, int stroke) {
  ui_card(x, y, w, h, r, stroke);
  for (int p = x + r + 8; p + 6 <= x + w - r; p += 14) {
    display.fillRect(p, y, 6, stroke, PAPER);
    display.fillRect(p, y + h - stroke, 6, stroke, PAPER);
  }
  for (int p = y + r + 8; p + 6 <= y + h - r; p += 14) {
    display.fillRect(x, p, stroke, 6, PAPER);
    display.fillRect(x + w - stroke, p, stroke, 6, PAPER);
  }
}

// ---------- status bar (Figma StatusBar, 40 px) ----------
inline bool g_plugged = false;  // external power present: the status bar shows the bolt (set by ui_battery_pct)
inline int ui_battery_pct() {
  Battery b;
  const bool ok = hw_battery_read(b);
  g_plugged = b.usb;
  return ok ? constrain(b.percent, 0, 100) : -1;
}

inline void ui_status_bar(int pct) {
  display.fillRect(0, 0, W, 40, PAPER);
  ui_text(F_B20, "HeySticky", 16, 7, 26);
  // Right group flows from the right edge like Figma's auto layout: "%", 8 px, battery (30), 8 px, Wi-Fi (28).
  const int tw = ui_text(F_B20, pct >= 0 ? String(pct) + "%" : "--", 464, 7, 26, AL_RIGHT);
  const int bx = 464 - tw - 8 - 30;  // battery frame left (390 for "82%")
  // battery: body 21x13 (stroke 3), nub, fill up to 13 px wide
  display.fillRoundRect(bx + 2, 13, 23, 15, 3, INK);
  display.fillRect(bx + 5, 16, 17, 9, PAPER);
  display.fillRect(bx + 25, 18, 3, 5, INK);
  if (pct > 0) display.fillRect(bx + 7, 18, max(1, 13 * pct / 100), 5, INK);
  const int lx = g_plugged ? bx - 8 - 16 : bx;  // Figma StatusBar Mode=Charging: bolt between Wi-Fi and battery
  if (g_plugged) ui_icon(ICON_BOLT, lx, 5);
  if (WiFi.status() == WL_CONNECTED) ui_icon(ICON_WIFI, lx - 8 - 28, 6);
}

inline void ui_page_counter(const String &s) { ui_text(F_R20, s, W / 2, 758, 26, AL_CENTER); }

// Figma TopBar: title left, small action right (Cancel, Clear, "4 left"), 3 px rule under it.
inline void ui_topbar(const String &title, const String &right) {
  ui_text(F_X32, title, 40, 54, 42);
  ui_text(F_R25, right, 440, 58, 33, AL_RIGHT);
  display.fillRect(40, 109, 400, 3, INK);
}

inline void utf8_pop(String &s) {  // drop the last character, not just its last byte
  int n = s.length() - 1;
  while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) n--;
  s.remove(max(n, 0));
}

inline String ui_fit(const Font &f, String s, int maxW) {  // cut with "…" to fit
  if (ui_width(f, s) <= maxW) return s;
  while (s.length() && ui_width(f, s + "…") > maxW) utf8_pop(s);
  return s + "…";
}

inline String ui_fit_tail(const Font &f, String s, int maxW) {  // cut the start with "…" to fit
  if (ui_width(f, s) <= maxW) return s;
  while (s.length() && ui_width(f, "…" + s) > maxW) {
    int n = 1;
    while (n < int(s.length()) && (uint8_t(s[n]) & 0xC0) == 0x80) n++;
    s.remove(0, n);
  }
  return "…" + s;
}

// Word-wrapped text, one line per lineH; x is the left edge (or the box's left edge when centred).
inline void ui_wrap(const Font &f, const String &s, int x, int y, int w, int lineH, int maxLines, Align a = AL_LEFT) {
  const int ax = a == AL_CENTER ? x + w / 2 : x;
  String line;
  int n = 0, i = 0;
  while (i <= int(s.length()) && n < maxLines) {
    int j = s.indexOf(' ', i);
    if (j < 0) j = s.length();
    const String word = s.substring(i, j), next = line.length() ? line + " " + word : word;
    if (line.length() && ui_width(f, next) > w) { ui_text(f, line, ax, y + lineH * n++, lineH, a); line = word; }
    else line = next;
    i = j + 1;
  }
  if (line.length() && n < maxLines) ui_text(f, line, ax, y + lineH * n, lineH, a);
}

// Figma Toggle (68x38): On = black pill, white knob right; Off = outline, black knob left.
inline void ui_toggle(int x, int y, bool on) {
  display.fillRoundRect(x, y, 68, 38, 19, INK);
  if (!on) display.fillRoundRect(x + 3, y + 3, 62, 32, 16, PAPER);
  display.fillCircle(x + (on ? 48 : 20), y + 19, 12, on ? PAPER : INK);
}

// Figma Button/PrimaryButton: optional icon + ExtraBold label, centred as one group.
inline void ui_button(int x, int y, int w, int h, bool filled, const Icon *icon, const String &label, int gap = 8) {
  if (filled) display.fillRoundRect(x, y, w, h, 12, INK);
  else ui_card(x, y, w, h, 12, 3);
  const uint16_t fg = filled ? PAPER : INK, bg = filled ? INK : PAPER;
  const int iw = icon ? icon->w + gap : 0, gx = x + (w - iw - ui_width(F_X25, label)) / 2;
  if (icon) ui_icon(*icon, gx, y + (h - icon->h + 1) / 2, fg);
  ui_text(F_X25, label, gx + iw, y + (h - 33 + 1) / 2, 33, AL_LEFT, fg, bg);
}

// ---------- refresh ----------
constexpr int FULL_EVERY = 30;  // calibration knob: partial refreshes between ghost-clearing full refreshes
inline int g_partials = 0;

inline void ui_full() { display.update(); g_partials = 0; }
inline void ui_partial(int x, int y, int w, int h, int fullEvery = FULL_EVERY) {
  if (++g_partials >= fullEvery) { ui_full(); return; }
  const uint32_t t = millis();
  display.updatePartial(x, y, w, h);
  Serial.printf("[refresh] partial %dx%d %lu ms\n", w, h, millis() - t);
}
