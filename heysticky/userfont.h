// User fonts (TASK-14): /fonts/<Name>/<slot>.vlw on the card, converted and uploaded by the phone page (06n).
// The chosen font replaces the text fonts below; digit-only fonts (clock, temperatures, 04j address) stay built-in.
// Files are checked with vlw_ok() before the display library ever reads them.
#pragma once
#include <vector>
#include "card.h"
#include "ui.h"
#include "settings.h"

struct FontSlot { Font *f; const char *file; };
inline const FontSlot FONT_SLOTS[] = {{&F_R20, "R20"}, {&F_R25, "R25"}, {&F_R30, "R30"}, {&F_B20, "B20"}, {&F_B25, "B25"},
                                      {&F_B26, "B26"}, {&F_B28, "B28"}, {&F_B35, "B35"}, {&F_X25, "X25"}, {&F_X30, "X30"},
                                      {&F_X32, "X32"}, {&F_X40, "X40"}, {&F_I25, "I25"}};
constexpr int FONT_SLOTS_N = sizeof FONT_SLOTS / sizeof FONT_SLOTS[0];
constexpr size_t FONT_FILE_MAX = 512 * 1024;
constexpr int FONTS_MAX = 8;  // 04c rows under "Default (built-in)"
inline bool g_pageDirty = false;  // changed from the phone page (font in use, Wi-Fi switch): redraw the screen

inline bool font_name_ok(const String &s) {  // folder name: 1-24 of A-Z a-z 0-9 space . _ -, not hidden
  if (s.isEmpty() || s.length() > 24 || s[0] == '.' || s[0] == ' ') return false;
  for (const char c : s)
    if (!isalnum(uint8_t(c)) && c != ' ' && c != '.' && c != '_' && c != '-') return false;
  return true;
}
inline bool font_slot_ok(const String &s) {
  for (const FontSlot &f : FONT_SLOTS)
    if (s == f.file) return true;
  return false;
}
inline String font_path(const String &name, const char *slot, const char *ext = ".vlw") { return "/fonts/" + name + "/" + slot + ext; }

// Whole file into PSRAM if it is a sane .vlw; nullptr otherwise. Caller frees.
inline uint8_t *font_read(const String &path, size_t &n) {
  File f = g_sdOk ? SD.open(path) : File();
  n = f ? f.size() : 0;
  uint8_t *b = n && n <= FONT_FILE_MAX ? (uint8_t *)ps_malloc(n) : nullptr;
  const bool ok = b && f.read(b, n) == n && vlw_ok(b, n);
  if (f) f.close();
  if (!ok) { free(b); return nullptr; }
  return b;
}

// "" = built-in. On any missing or bad file: built-in, false.
inline bool font_apply(const String &name) {
  static Font def[FONT_SLOTS_N];
  static uint8_t *buf[FONT_SLOTS_N];
  static bool saved = false;
  if (!saved) { for (int i = 0; i < FONT_SLOTS_N; i++) def[i] = *FONT_SLOTS[i].f; saved = true; }
  g_fontLoaded = nullptr;  // the display may point into a buffer freed below: reload before the next draw
  for (int i = 0; i < FONT_SLOTS_N; i++) { *FONT_SLOTS[i].f = def[i]; free(buf[i]); buf[i] = nullptr; }
  if (name.isEmpty()) return true;
  for (int i = 0; i < FONT_SLOTS_N; i++) {
    size_t n;
    uint8_t *b = font_read(font_path(name, FONT_SLOTS[i].file), n);
    if (!b) { Serial.printf("[font] %s: %s missing or bad, using built-in\n", name.c_str(), FONT_SLOTS[i].file); font_apply(""); return false; }
    buf[i] = b;
    *FONT_SLOTS[i].f = {b, uint8_t(be32(b + 16)), uint8_t(be32(b + 20))};
  }
  Serial.printf("[font] using %s\n", name.c_str());
  return true;
}

// A folder of Reader fonts (.cpfont, phone page Reader › Fonts) only: not a UI font.
inline bool reader_font_dir(File &dir) {
  bool cp = false;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    const String n = e.name();
    e.close();
    if (!n.endsWith(".cpfont")) return false;
    cp = true;
  }
  return cp;
}

inline std::vector<String> font_list() {  // folder names under /fonts, sorted
  std::vector<String> v;
  File d = g_sdOk ? SD.open("/fonts") : File();
  if (!d) return v;
  for (File f = d.openNextFile(); f && int(v.size()) < FONTS_MAX; f = d.openNextFile()) {
    if (f.isDirectory() && font_name_ok(f.name()) && !reader_font_dir(f)) v.push_back(f.name());
    f.close();
  }
  d.close();
  std::sort(v.begin(), v.end());
  return v;
}

inline bool font_delete(const String &name) {
  if (!font_name_ok(name) || !g_sdOk) return false;
  for (const FontSlot &s : FONT_SLOTS) { SD.remove(font_path(name, s.file)); SD.remove(font_path(name, s.file, ".tmp")); }
  const bool ok = SD.rmdir("/fonts/" + name);
  if (cfg_get("font") == name) { cfg_set("font", ""); font_apply(""); g_pageDirty = true; }
  return ok;
}
