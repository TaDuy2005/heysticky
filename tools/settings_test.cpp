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

  puts("settings_test OK");
}
