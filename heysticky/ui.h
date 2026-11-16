// HeySticky drawing helpers: JetBrains Mono text placed like Figma text boxes, icons,
// cards, status bar, page counter, partial-refresh bookkeeping.
#pragma once
#include <Seeed_GFX.h>
#include <WiFi.h>
#include "src/fonts/fonts.h"
#include "src/icons.h"
#include "hw.h"

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
inline int ui_battery_pct() {
  Battery b;
  return hw_battery_read(b) ? constrain(b.percent, 0, 100) : -1;
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
  if (WiFi.status() == WL_CONNECTED) ui_icon(ICON_WIFI, bx - 8 - 28, 6);
}

inline void ui_page_counter(const char *s) { ui_text(F_R20, s, W / 2, 758, 26, AL_CENTER); }

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
