// Saved Wi-Fi networks with no hardware: up to 5, most recently joined first (PLAN.md §8, §9).
// Host check: tools/wifi_test.cpp
#pragma once

constexpr int WIFI_MAX = 5;
struct WifiNet { String ssid, pass; };
struct WifiList { WifiNet n[WIFI_MAX]; int count = 0; };
struct ScanNet { String ssid; int rssi; uint8_t enc; };  // enc: wifi_auth_mode_t, 0 = open

inline int wl_find(const WifiList &l, const String &ssid) {
  for (int i = 0; i < l.count; i++)
    if (l.n[i].ssid == ssid) return i;
  return -1;
}

inline bool wl_forget(WifiList &l, const String &ssid) {
  const int i = wl_find(l, ssid);
  if (i < 0) return false;
  for (int k = i; k + 1 < l.count; k++) l.n[k] = l.n[k + 1];
  l.n[--l.count] = WifiNet{"", ""};
  return true;
}

// A joined network goes first; a sixth pushes out the one joined longest ago.
inline void wl_add(WifiList &l, const String &ssid, const String &pass) {
  wl_forget(l, ssid);
  if (l.count == WIFI_MAX) l.count--;
  for (int k = l.count; k > 0; k--) l.n[k] = l.n[k - 1];
  l.n[0] = WifiNet{ssid, pass};
  l.count++;
}

// Strongest saved network in range -> its index in the list, -1 when none.
inline int wl_pick(const WifiList &l, const ScanNet *s, int ns) {
  int best = -1, rssi = 0;
  for (int i = 0; i < ns; i++) {
    const int k = wl_find(l, s[i].ssid);
    if (k >= 0 && (best < 0 || s[i].rssi > rssi)) { best = k; rssi = s[i].rssi; }
  }
  return best;
}

// One flash value per network: "ssid\npass". An SSID from the page never holds a line break (checked there).
inline String wl_pack(const WifiNet &w) { return w.ssid + "\n" + w.pass; }
inline WifiNet wl_unpack(const String &v) {
  const int i = v.indexOf('\n');
  if (i < 0) return WifiNet{v, ""};
  return WifiNet{v.substring(0, i), v.substring(i + 1)};
}
