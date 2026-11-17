// Host check for heysticky/settings.h. Run from the repo root:
//   c++ -std=c++17 -I heysticky tools/settings_test.cpp -o /tmp/settings_test && /tmp/settings_test
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

struct String : std::string {  // just enough of Arduino's String
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
};
inline String operator+(const String &a, const char *b) { return String(static_cast<const std::string &>(a) + b); }

#include "settings.h"
#include <vector>

static void put32(std::vector<uint8_t> &v, int32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }

int main() {
  const uint8_t blk3[] = {0x4e, 0xf3, 0x30, 0xa6, 0x4b, 0x9b, 0xb6, 0x01, 0x96, 0xf6};  // a sample eFuse BLOCK3 (not a real device)
  assert(sn_text(blk3) == "123456789012345678");
  const uint8_t zero[8] = {};
  assert(sn_text(zero) == "—");

  assert(utc_text(8 * 3600) == "UTC+8");
  assert(utc_text(19800) == "UTC+5:30");
  assert(utc_text(-3 * 3600) == "UTC−3");
  assert(utc_text(0) == "UTC+0");
  assert(utc_text(LONG_MIN) == "—");

  assert(size_text(300) == "300 B");
  assert(size_text(12000) == "12 KB");
  assert(size_text(31914983424ULL) == "31.9 GB");
  assert(size_text(32000000000ULL) == "32 GB");
  assert(size_text(512000000) == "512 MB");
  assert(size_text(1500) == "1.5 KB");

  assert(ver_newer("v1.0.1", "1.0.0"));
  assert(ver_newer("1.1.0", "1.0.9"));
  assert(ver_newer("v2.0.0", "1.10.3"));
  assert(ver_newer("v1.10.0", "1.9.0"));  // numbers, not text
  assert(!ver_newer("v1.0.0", "1.0.0"));
  assert(!ver_newer("v0.9.9", "1.0.0"));
  assert(!ver_newer("latest", "1.0.0"));
  assert(!ver_newer("v1.1", "1.0.0"));

  // vlw_ok: a well-formed font passes; truncated or out-of-range ones don't
  std::vector<uint8_t> f;
  for (int32_t x : {2, 11, 25, 0, 26, 8}) put32(f, x);                 // header: 2 glyphs
  for (int32_t x : {0xa0, 0, 0, 15, 0, 0, 0}) put32(f, x);             // space: no bitmap
  for (int32_t x : {0x41, 3, 2, 15, 3, 1, 0}) put32(f, x);              // 'A': 2x3 bitmap
  f.insert(f.end(), 6, 255);
  assert(vlw_ok(f.data(), f.size()));
  assert(!vlw_ok(f.data(), f.size() - 1));                // last bitmap cut short
  assert(!vlw_ok(f.data(), 23));                          // no header
  auto bad = f; bad[7] = 12;                              // version
  assert(!vlw_ok(bad.data(), bad.size()));
  bad = f; bad[0] = 0x7f;                                 // glyph count far past the file
  assert(!vlw_ok(bad.data(), bad.size()));
  bad = f; bad[24 + 8 + 2] = 1;                           // first glyph 256+ px wide
  assert(!vlw_ok(bad.data(), bad.size()));

  puts("settings_test OK");
}
