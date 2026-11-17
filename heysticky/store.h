// HeySticky settings in device flash (Preferences / NVS). Keys: PLAN.md §8.
// Never in source, never in the .bin.
#pragma once
#include <Preferences.h>

inline String cfg_get(const char *key, const char *def = "") {
  Preferences p;
  p.begin("heysticky", true);
  String v = p.isKey(key) ? p.getString(key, def) : String(def);
  p.end();
  return v;
}

inline void cfg_set(const char *key, const String &value) {
  Preferences p;
  p.begin("heysticky", false);
  p.putString(key, value);
  p.end();
}

inline void cfg_del(const char *key) {
  Preferences p;
  p.begin("heysticky", false);
  if (p.isKey(key)) p.remove(key);
  p.end();
}
