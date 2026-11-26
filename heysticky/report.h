// Report a bug (Figma 04n-04u, 06t-06v): report IDs, the saved list, the GitHub and email links, issue states.
// No I/O here, so tools/report_test.cpp checks it on the Mac. The Sticky side is in settings_ui.h / screens.h,
// the phone side in phone.h. Email goes to REPORT_MAIL from the phone's mail app (a Gmail filter sends the auto reply);
// only GitHub reports are followed on 04u.
#pragma once
#include <cctype>
#include <vector>
#include <ArduinoJson.h>
#include "settings.h"

constexpr const char *REPORT_REPO = "TaDuy2005/heysticky";        // public: issues are read with no token
constexpr const char *REPORT_MAIL = "heystickyreport@gmail.com";
constexpr int REPORTS_MAX = 9;      // rows that fit 04u; the oldest drops off
constexpr int REPORT_ORS = 5;       // GitHub search: at most 5 OR per query
constexpr uint32_t REPORT_TTL = 3600;  // a report still not on GitHub after this many seconds was never sent: dropped
constexpr uint32_t CLOCK_OK = 1700000000;  // time() below this: the clock isn't set yet

// A report the Sticky follows on 04u. st: 'w' Waiting (no issue found yet), 'o' Open, 'f' Fixed, 'x' Won't fix.
// at: when it was made (unix seconds, 0 = unknown).
struct Report {
  String id, title;
  char st = 'w';
  uint32_t at = 0;
};

// "HS-7K2P": 4 characters with no 0/O, 1/I/L look-alikes, so it can be read off the screen.
constexpr char REPORT_ABC[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
inline String report_id(uint32_t r) {
  String s = "HS-";
  for (int i = 0; i < 4; i++, r /= 31) s += REPORT_ABC[r % 31];
  return s;
}

// Saved list (flash key "reports"): one "HS-7K2P<tab>st<tab>at<tab>title" line per report, newest first.
// A line from before at ("HS-7K2P<tab>st<tab>title") reads as at 0.
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
    const int t = r.title.indexOf('\t');
    if (t > 0) {
      const String n = r.title.substring(0, t);
      bool digits = true;
      for (unsigned i = 0; i < n.length(); i++) digits &= isdigit(uint8_t(n[i])) != 0;
      if (digits) { r.at = uint32_t(n.toInt()); r.title = r.title.substring(t + 1); }
    }
    v.push_back(r);
  }
  return v;
}
inline String reports_text(const std::vector<Report> &v) {
  String s;
  for (const Report &r : v) s += r.id + "\t" + String(r.st) + "\t" + String((unsigned long)r.at) + "\t" + r.title + "\n";
  return s;
}
inline void reports_add(std::vector<Report> &v, const Report &r) {  // newest first; the same ID again moves up
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].id == r.id) { v.erase(v.begin() + i); break; }
  v.insert(v.begin(), r);
  if (int(v.size()) > REPORTS_MAX) v.resize(REPORTS_MAX);
}

// Query-string value: spaces as "+" (shorter than %20, so the QR stays coarse), the rest of anything but
// A-Z a-z 0-9 - _ . ~ as %XX. mail: spaces as %20, since a mail app keeps "+" as a plus sign.
inline String q_enc(const String &s, bool mail = false) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    const uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += char(c);
    else if (c == ' ' && !mail) o += '+';
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

// What a blank report (no AI text) starts with, in the Sticky's language.
inline String report_guide(bool vi) {
  return vi ? "Lỗi gì đã xảy ra:\n\nCác bước để gặp lại lỗi:\n\nBạn mong đợi điều gì:\n\n(Bạn có thể đính kèm ảnh chụp màn hình.)"
            : "What happened:\n\nSteps to make it happen:\n\nWhat you expected:\n\n(You can attach a photo of the screen.)";
}

// 04p: GitHub's New issue page with the title and text (or the guide) filled in; the last line carries the ID 04u
// searches for. A blank report has no title, so GitHub asks for one.
inline String report_gh_url(const Report &r, const String &body, bool vi) {
  const String b = (body.length() ? body : report_guide(vi)) + "\n\nReport " + r.id + ", HeySticky " + FW_VERSION;
  return String("https://github.com/") + REPORT_REPO + "/issues/new?title=" + q_enc(r.title) + "&body=" + q_enc(b);
}

// 04q: the phone's mail app, to REPORT_MAIL; the subject carries the ID, the body the AI's title and text (or the guide).
inline String report_mail_url(const Report &r, const String &body, bool vi) {
  const String subj = "Confirmation Bug Report #" + r.id + ", HeySticky " + FW_VERSION;
  const String b = body.length() ? (r.title.length() ? r.title + "\n\n" : String("")) + body : report_guide(vi);
  return String("mailto:") + REPORT_MAIL + "?subject=" + q_enc(subj, true) + "&body=" + q_enc(b, true);
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

// After a full search: drop reports still Waiting REPORT_TTL after they were made (the QR was scanned and the
// issue never created, or not scanned at all). Unknown at (an old line, or made before the clock was set) counts as
// old. Nothing is dropped while the clock isn't set. True if anything was dropped.
inline bool reports_prune(std::vector<Report> &v, uint32_t now) {
  if (now < CLOCK_OK) return false;
  const size_t n = v.size();
  for (size_t i = 0; i < v.size();)
    if (v[i].st == 'w' && now > v[i].at + REPORT_TTL) v.erase(v.begin() + i);
    else i++;
  return v.size() != n;
}

// Only these fields of the search reply are kept in memory.
inline void report_filter(JsonDocument &f) {
  JsonObject it = f["items"].add<JsonObject>();
  for (const char *k : {"title", "body", "state", "state_reason"}) it[k] = true;
}
