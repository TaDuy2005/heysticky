// Report a bug (Figma 04n-04u, 06t-06v): report IDs, the saved list, the GitHub and email links, issue states.
// No I/O here, so tools/report_test.cpp checks it on the Mac. The Sticky side is in settings_ui.h / screens.h,
// the phone side in phone.h, the email path in tools/bug-worker.
#pragma once
#include <cctype>
#include <vector>
#include <ArduinoJson.h>
#include "settings.h"

constexpr const char *REPORT_REPO = "TaDuy2005/heysticky";  // public: issues are read with no token
constexpr const char *REPORT_HOST = "sticky.davidtalab.cc";  // tools/bug-worker
constexpr int REPORTS_MAX = 9;                              // rows that fit 04u; the oldest drops off
constexpr int REPORT_ORS = 5;                               // GitHub search: at most 5 OR per query

// A report the Sticky follows on 04u. st: 'w' Waiting (no issue found yet), 'o' Open, 'f' Fixed, 'x' Won't fix.
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

// 04p: GitHub's New issue page with the title and text filled in; the last line carries the ID 04u searches for.
inline String report_gh_url(const Report &r, const String &body) {
  String b = body;
  if (b.length()) b += "\n\n";
  b += String("Report ") + r.id + ", HeySticky " + FW_VERSION;
  return String("https://github.com/") + REPORT_REPO + "/issues/new?title=" + q_enc(r.title) + "&body=" + q_enc(b);
}

// 04q: the Worker's form (08a), filled in. l: the page's language.
inline String report_mail_url(const Report &r, const String &body, bool vi) {
  String u = String("https://") + REPORT_HOST + "/r?id=" + r.id + "&v=" + FW_VERSION + (vi ? "&l=vi" : "");
  if (r.title.length()) u += "&t=" + q_enc(r.title);
  if (body.length()) u += "&b=" + q_enc(body);
  return u;
}

// GitHub search for reports [from, from + n): repo:… is:issue "HS-AAAA" OR "HS-BBBB"
inline String report_search_url(const std::vector<Report> &v, size_t from, size_t n) {
  String q = String("repo:") + REPORT_REPO + " is:issue";
  for (size_t i = from; i < from + n && i < v.size(); i++) q += String(i > from ? " OR \"" : " \"") + v[i].id + "\"";
  return "https://api.github.com/search/issues?per_page=20&q=" + q_enc(q);
}

inline char issue_state(JsonVariantConst it) {  // closed as not planned or duplicate = Won't fix, else Fixed
  if (String(it["state"] | "") == "open") return 'o';
  const String why = it["state_reason"] | "";
  return why == "not_planned" || why == "duplicate" ? 'x' : 'f';
}

// Search results -> states for reports [from, from + n). A report the search didn't return is Waiting (the issue
// isn't made yet, or was deleted). A report with no title (made on the phone) takes the issue's. True if anything changed.
inline bool reports_apply(std::vector<Report> &v, size_t from, size_t n, JsonVariantConst items) {
  bool changed = false;
  for (size_t i = from; i < from + n && i < v.size(); i++) {
    Report &r = v[i];
    char st = 'w';
    String title;
    for (JsonVariantConst it : items.as<JsonArrayConst>()) {
      const String t = it["title"] | "", b = it["body"] | "";
      if (t.indexOf(r.id.c_str()) < 0 && b.indexOf(r.id.c_str()) < 0) continue;
      st = issue_state(it);
      title = t;
      break;
    }
    if (r.title.isEmpty() && title.length()) { r.title = title.substring(0, 80); changed = true; }
    if (st != r.st) { r.st = st; changed = true; }
  }
  return changed;
}

// Only these fields of the search reply are kept in memory.
inline void report_filter(JsonDocument &f) {
  JsonObject it = f["items"].add<JsonObject>();
  for (const char *k : {"title", "body", "state", "state_reason"}) it[k] = true;
}
