// Settings text helpers with no hardware: version, SN, UTC offset, sizes.
// Host check: tools/settings_test.cpp
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <strings.h>

constexpr const char *FW_VERSION = "1.1.0";

// GitHub release tag ("v1.2.0" or "1.2.0") newer than the running version? A tag that isn't x.y.z never is.
inline bool ver_newer(const char *tag, const char *cur) {
  int a[3], b[3];
  if (*tag == 'v' || *tag == 'V') tag++;
  if (sscanf(tag, "%d.%d.%d", &a[0], &a[1], &a[2]) != 3 || sscanf(cur, "%d.%d.%d", &b[0], &b[1], &b[2]) != 3) return false;
  for (int i = 0; i < 3; i++)
    if (a[i] != b[i]) return a[i] > b[i];
  return false;
}

// A .vlw font from the card (made on the phone page, so untrusted): every glyph record and bitmap must fit
// inside the file, in the ranges the display library stores them in, before it is ever loaded.
// Layout: tools/make_fonts.py. All int32 big-endian.
inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline bool vlw_ok(const uint8_t *d, size_t n) {
  if (n < 24 || be32(d + 4) != 11) return false;
  const uint32_t count = be32(d);
  if (count == 0 || count > 2000 || be32(d + 16) > 255 || be32(d + 20) > 255) return false;
  size_t need = 24 + size_t(count) * 28;
  if (need > n) return false;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *g = d + 24 + i * 28;
    const uint32_t h = be32(g + 4), w = be32(g + 8), adv = be32(g + 12);
    const int32_t dy = int32_t(be32(g + 16)), dx = int32_t(be32(g + 20));
    if (h > 255 || w > 255 || adv > 255 || dy < -255 || dy > 255 || dx < -128 || dx > 127) return false;
    need += size_t(h) * w;
  }
  return need <= n;
}

// SN: eFuse user block (BLOCK3), first 8 bytes little-endian, 18 digits like the factory firmware (%018llu).
inline String sn_text(const uint8_t *b) {
  unsigned long long v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | b[i];
  if (!v) return "—";
  char s[24];
  snprintf(s, sizeof s, "%018llu", v);
  return s;
}

// Open-Meteo's utc_offset_seconds -> "UTC+8", "UTC+5:30", "UTC−3"; "—" before the first reply.
inline String utc_text(long sec) {
  if (sec == LONG_MIN) return "—";
  const long m = labs(sec) / 60;
  char s[16];
  if (m % 60) snprintf(s, sizeof s, "%ld:%02ld", m / 60, m % 60);
  else snprintf(s, sizeof s, "%ld", m / 60);
  return String(sec < 0 ? "UTC−" : "UTC+") + s;
}

// Bytes -> "31.9 GB", "512 MB", "12 KB", "300 B" (1000-based, like the size printed on the card).
inline String size_text(unsigned long long b) {
  static const char *U[] = {"B", "KB", "MB", "GB", "TB"};
  int u = 0;
  double v = double(b);
  while (v >= 1000 && u < 4) { v /= 1000; u++; }
  char s[24];
  snprintf(s, sizeof s, "%.1f", v);
  const size_t n = strlen(s);
  if (n > 2 && s[n - 1] == '0' && s[n - 2] == '.') s[n - 2] = 0;  // "32.0" -> "32"
  return String(s) + " " + U[u];
}

// 04k SD Files folder paths: "/fonts/NotoSans" -> parent "/fonts", name "NotoSans"; "/fonts" -> "/".
inline String sd_parent(const String &dir) {
  const char *s = dir.c_str(), *p = strrchr(s, '/');
  if (!p || p == s) return String("/");
  char b[256];
  snprintf(b, sizeof b, "%.*s", int(p - s), s);
  return String(b);
}
inline String sd_name(const String &dir) { const char *p = strrchr(dir.c_str(), '/'); return String(p ? p + 1 : dir.c_str()); }
inline bool sd_before(bool aDir, const char *a, bool bDir, const char *b) {  // folders first, then A-Z ignoring case
  return aDir != bDir ? aDir : strcasecmp(a, b) < 0;
}
