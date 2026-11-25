// Report a bug (Figma 04n-04u, 06t-06v): report IDs, the saved list, the report server's links and states.
// No I/O here, so tools/report_test.cpp checks it on the Mac. The Sticky side is in settings_ui.h / screens.h,
// the phone side in phone.h. The server (heysticky-report-server repo, on the Pi) has the form for both ways to send
// (email, or GitHub sign-in), opens the issues, and knows which reports were sent, cancelled or left unfinished.
#pragma once
#include <cctype>
#include <vector>
#include <ArduinoJson.h>
#include "settings.h"

constexpr const char *REPORT_HOST = "sticky.davidtalab.cc";  // the report server
constexpr int REPORTS_MAX = 9;                              // rows that fit 04u; the oldest drops off

// A report the Sticky follows on 04u. st: 'w' Waiting (the form is open, not sent yet), 'o' Open, 'f' Fixed, 'x' Won't fix.
struct Report {
  String id, title;
  char st = 'w';
};

// "HS-7K2P": 4 characters with no 0/O, 1/I/L look-alikes, so it can be read off the screen.
constexpr char REPORT_ABC[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
inline String report_id(uint32_t r) {
  String s = "HS-";
  for (int i = 0; i < 4; i++, r /= 31) s += REPORT_ABC[r % 31];
  return s;
}

// Saved list (flash key "reports"): one "HS-7K2P<tab>st<tab>title" line per report, newest first.
inline std::vector<Report> reports_parse(const String &s) {
  std::vector<Report> v;
  unsigned a = 0;
  while (a < s.length() && int(v.size()) < REPORTS_MAX) {
    int e = s.indexOf('\n', a);
    if (e < 0) e = s.length();
    const String l = s.substring(a, e);
    a = e + 1;
    if (l.length() < 10 || l[7] != '\t' || l[9] != '\t' || !strchr("wofx", l[8])) continue;
    Report r;
    r.id = l.substring(0, 7);
    r.st = l[8];
    r.title = l.substring(10);
    v.push_back(r);
  }
  return v;
}
inline String reports_text(const std::vector<Report> &v) {
  String s;
  for (const Report &r : v) s += r.id + "\t" + String(r.st) + "\t" + r.title + "\n";
  return s;
}
inline void reports_add(std::vector<Report> &v, const Report &r) {  // newest first; the same ID again moves up
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].id == r.id) { v.erase(v.begin() + i); break; }
  v.insert(v.begin(), r);
  if (int(v.size()) > REPORTS_MAX) v.resize(REPORTS_MAX);
}

// Query-string value: spaces as "+", the rest of anything but A-Z a-z 0-9 - _ . ~ as %XX. Shorter than %20, so the QR stays coarse.
inline String q_enc(const String &s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    const uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += char(c);
    else if (c == ' ') o += '+';
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

// 04p / 04q: the server's form, filled in; gh: Send with GitHub is the black button; vi: the page in Vietnamese.
inline String report_url(const Report &r, const String &body, bool vi, bool gh) {
  String u = String("https://") + REPORT_HOST + "/r?id=" + r.id + "&v=" + FW_VERSION + (gh ? "&via=gh" : "") + (vi ? "&l=vi" : "");
  if (r.title.length()) u += "&t=" + q_enc(r.title);
  if (body.length()) u += "&b=" + q_enc(body);
  return u;
}

// The states of every followed report in one request: {"HS-AAAA":{"st":"o","t":"title"}, ...}
inline String report_status_url(const std::vector<Report> &v) {
  String u = String("https://") + REPORT_HOST + "/s?ids=";
  for (size_t i = 0; i < v.size(); i++) {
    if (i) u += ",";
    u += v[i].id;
  }
  return u;
}

// The server's reply -> the list. A report it leaves out was cancelled, left unfinished for an hour, or never
// opened (Done without scanning), so it is dropped. A report with no title (made on the phone) takes the form's.
// True if anything changed.
inline bool reports_apply(std::vector<Report> &v, JsonObjectConst st) {
  bool changed = false;
  for (size_t i = 0; i < v.size();) {
    JsonObjectConst s = st[v[i].id.c_str()];
    const String c = s["st"] | "";
    if (s.isNull() || c.length() != 1 || !strchr("wofx", c[0])) { v.erase(v.begin() + i); changed = true; continue; }
    Report &r = v[i++];
    const String t = s["t"] | "";
    if (r.title.isEmpty() && t.length()) { r.title = t.substring(0, 80); changed = true; }
    if (c[0] != r.st) { r.st = c[0]; changed = true; }
  }
  return changed;
}
