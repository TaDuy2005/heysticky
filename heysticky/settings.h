// Settings text helpers with no hardware: version, SN, UTC offset, sizes.
// Host check: tools/settings_test.cpp
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

constexpr const char *FW_VERSION = "1.0.0";

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
