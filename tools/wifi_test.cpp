// Host check for heysticky/wifi_list.h. Run from the repo root:
//   c++ -std=c++17 -I heysticky tools/wifi_test.cpp -o /tmp/wifi_test && /tmp/wifi_test
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

struct String : std::string {  // just enough of Arduino's String
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  int indexOf(char c) const { const auto i = find(c); return i == npos ? -1 : int(i); }
  String substring(int a, int b = -1) const { return substr(a, b < 0 ? npos : size_t(b - a)); }
};
inline String operator+(const String &a, const char *b) { return String(static_cast<const std::string &>(a) + b); }
inline String operator+(const String &a, const String &b) { return String(static_cast<const std::string &>(a) + static_cast<const std::string &>(b)); }

#include "wifi_list.h"

int main() {
  WifiList l;
  wl_add(l, "Home", "pw1");
  wl_add(l, "Office", "pw2");
  assert(l.count == 2 && l.n[0].ssid == "Office" && l.n[1].ssid == "Home");
  wl_add(l, "Home", "new");  // joined again: first, password replaced, no duplicate
  assert(l.count == 2 && l.n[0].ssid == "Home" && l.n[0].pass == "new");

  for (const char *s : {"A", "B", "C", "D"}) wl_add(l, s, "");
  assert(l.count == WIFI_MAX && l.n[0].ssid == "D" && wl_find(l, "Office") < 0);  // oldest dropped
  assert(wl_find(l, "Home") == 4);

  assert(wl_forget(l, "B") && l.count == 4 && wl_find(l, "B") < 0 && l.n[3].ssid == "Home");
  assert(!wl_forget(l, "Nope"));

  const ScanNet scan[] = {{"Stranger", -30, 3}, {"Home", -70, 3}, {"C", -55, 0}, {"Home", -80, 3}};
  assert(wl_pick(l, scan, 4) == wl_find(l, "C"));  // strongest saved one, not the strongest overall
  assert(wl_pick(l, scan, 1) == -1);
  assert(wl_pick(l, scan, 0) == -1);

  const WifiNet w = wl_unpack(wl_pack(WifiNet{"My Net", "p\\a:ss word"}));
  assert(w.ssid == "My Net" && w.pass == "p\\a:ss word");
  assert(wl_unpack("Open").ssid == "Open" && wl_unpack("Open").pass == "");
  assert(wl_unpack("Open\n").pass == "");

  puts("wifi_test OK");
}
