// Host check for heysticky/report.h and the bug parts of ai_parse.h. Run from the repo root:
//   c++ -std=c++17 -I heysticky -I "$(ls -d ~/Library/Arduino15/internal/ArduinoJson_7.4.3_*/ArduinoJson/src)" \
//       tools/report_test.cpp -o /tmp/report_test && /tmp/report_test
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Just enough of Arduino's String for report.h, settings.h and ai_parse.h.
struct String : std::string {
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  String(char c) : std::string(1, c) {}
  String(int n) : std::string(std::to_string(n)) {}
  String(unsigned long n) : std::string(std::to_string(n)) {}
  unsigned length() const { return unsigned(size()); }
  bool isEmpty() const { return empty(); }
  String substring(size_t a, size_t b = npos) const { return a >= size() ? "" : String(substr(a, b == npos ? npos : b - a)); }
  int indexOf(char c, size_t from = 0) const { auto p = find(c, from); return p == npos ? -1 : int(p); }
  int indexOf(const char *s) const { auto p = find(s); return p == npos ? -1 : int(p); }
  int lastIndexOf(char c) const { auto p = rfind(c); return p == npos ? -1 : int(p); }
  int lastIndexOf(const char *s) const { auto p = rfind(s); return p == npos ? -1 : int(p); }
  bool startsWith(const String &s) const { return rfind(s, 0) == 0; }
  bool endsWith(const char *s) const { std::string t(s); return size() >= t.size() && compare(size() - t.size(), t.size(), t) == 0; }
  long toInt() const { return atol(c_str()); }
  void trim() {
    const auto a = find_first_not_of(" \t\r\n"), b = find_last_not_of(" \t\r\n");
    *this = a == npos ? String("") : String(substr(a, b - a + 1));
  }
};
inline String operator+(const String &a, const String &b) { return String(static_cast<const std::string &>(a) + static_cast<const std::string &>(b)); }
inline String operator+(const String &a, const char *b) { return String(static_cast<const std::string &>(a) + b); }
inline String operator+(const char *a, const String &b) { return String(a + static_cast<const std::string &>(b)); }

#include "report.h"
#include "ai_parse.h"

int main() {
  // IDs: HS- + 4 from the look-alike-free set
  assert(report_id(0) == "HS-2222" && report_id(30) == "HS-Z222" && report_id(31) == "HS-2322");
  for (uint32_t r : {1u, 12345u, 4294967295u}) {
    const String id = report_id(r);
    assert(id.length() == 7 && id.startsWith("HS-"));
    for (int i = 3; i < 7; i++) assert(strchr(REPORT_ABC, id[i]));
  }

  // saved list: round trip, newest first, bad lines skipped, at most REPORTS_MAX
  std::vector<Report> v;
  reports_add(v, {"HS-AAAA", "Clock stops in sleep", 'w'});
  reports_add(v, {"HS-BBBB", "", 'o'});
  assert(v[0].id == "HS-BBBB" && v[1].id == "HS-AAAA");
  reports_add(v, {"HS-AAAA", "Clock stops in sleep", 'f'});  // same ID moves up, not twice
  assert(v.size() == 2 && v[0].id == "HS-AAAA" && v[0].st == 'f');
  v[1].at = 1764000000;
  const String t = reports_text(v);
  assert(t == "HS-AAAA\tf\t0\tClock stops in sleep\nHS-BBBB\to\t1764000000\t\n");
  auto w = reports_parse(t + "garbage\nHS-CCCC\tq\tbad state\n");
  assert(w.size() == 2 && w[0].title == "Clock stops in sleep" && w[1].title == "" && w[1].st == 'o' && w[1].at == 1764000000);
  w = reports_parse("HS-AAAA\tw\tOld line\nHS-BBBB\to\t\nHS-CCCC\tw\t5\tTab\tin title\n");  // before at: at 0
  assert(w.size() == 3 && w[0].title == "Old line" && w[0].at == 0 && w[1].title == "" && w[2].at == 5 && w[2].title == "Tab\tin title");
  for (int i = 0; i < 20; i++) reports_add(v, {report_id(i), "x", 'w'});
  assert(int(v.size()) == REPORTS_MAX && int(reports_parse(reports_text(v)).size()) == REPORTS_MAX);

  // links
  assert(q_enc("Clock stops, 9:41 AM") == "Clock+stops%2C+9%3A41+AM" && q_enc("a~b_c-d.e") == "a~b_c-d.e");
  assert(q_enc("a b+c", true) == "a%20b%2Bc");
  Report r{"HS-7K2P", "Clock stops in sleep", 'w'};
  const String V = FW_VERSION;
  assert(report_gh_url(r, "It sleeps.") == "https://github.com/TaDuy2005/heysticky/issues/new?title=Clock+stops+in+sleep&body=It+sleeps.%0A%0AReport+HS-7K2P%2C+HeySticky+" + V);
  assert(report_gh_url({"HS-7K2P", "", 'w'}, "") == "https://github.com/TaDuy2005/heysticky/issues/new?title=&body=Report+HS-7K2P%2C+HeySticky+" + V);
  assert(report_mail_url(r, "It sleeps.") == "mailto:heystickyreport@gmail.com?subject=%5BHS-7K2P%5D%20Clock%20stops%20in%20sleep&body=It%20sleeps.%0A%0AReport%20HS-7K2P%2C%20HeySticky%20" + V);
  assert(report_mail_url({"HS-7K2P", "", 'w'}, "") == "mailto:heystickyreport@gmail.com?subject=%5BHS-7K2P%5D%20HeySticky%20bug&body=Report%20HS-7K2P%2C%20HeySticky%20" + V);
  // a full AI report must stay a QR the 260 px frame draws at 3 px a module (version 16, 586 bytes at ECC low)
  const String title40(std::string(40, 'w')), body300 = [] {
    std::string b;
    while (b.size() < 300) b += "words ";
    return String(b.substr(0, 300));
  }();
  assert(report_gh_url({"HS-7K2P", title40, 'w'}, body300).length() < 586);
  assert(report_mail_url({"HS-7K2P", title40, 'w'}, body300).length() < 586);

  // search: one query per REPORT_ORS + 1 IDs
  std::vector<Report> s = {{"HS-AAAA", "A", 'w'}, {"HS-BBBB", "", 'w'}, {"HS-CCCC", "C", 'o'}};
  assert(report_search_url(s, 0, 2) == "https://api.github.com/search/issues?per_page=20&q=repo%3ATaDuy2005%2Fheysticky+is%3Aissue+%22HS-AAAA%22+OR+%22HS-BBBB%22");
  JsonDocument d;
  deserializeJson(d, R"([{"title":"Clock stops","body":"x\n\nReport HS-AAAA, HeySticky 1.2.0","state":"closed","state_reason":"completed"},
                        {"title":"Weather shows --","body":"Report HS-BBBB","state":"open","state_reason":null}])");
  assert(reports_apply(s, 0, 3, d.as<JsonVariantConst>()));
  assert(s[0].st == 'f' && s[0].title == "A" && s[1].st == 'o' && s[1].title == "Weather shows --" && s[2].st == 'w');  // C's issue is gone
  assert(!reports_apply(s, 0, 3, d.as<JsonVariantConst>()));  // nothing new
  deserializeJson(d, R"([{"title":"x","body":"Report HS-AAAA","state":"closed","state_reason":"not_planned"}])");
  assert(reports_apply(s, 0, 1, d.as<JsonVariantConst>()) && s[0].st == 'x');

  // never sent: Waiting an hour after it was made drops off; not before, not while the clock isn't set
  const uint32_t now = 1764000000;
  std::vector<Report> p = {{"HS-AAAA", "new", 'w', now - 60}, {"HS-BBBB", "old", 'w', now - 3601}, {"HS-CCCC", "open", 'o', 0},
                           {"HS-DDDD", "unknown", 'w', 0}, {"HS-EEEE", "future", 'w', now + 99}};
  assert(!reports_prune(p, 5) && p.size() == 5);
  assert(reports_prune(p, now) && p.size() == 3 && p[0].id == "HS-AAAA" && p[1].id == "HS-CCCC" && p[2].id == "HS-EEEE");
  assert(!reports_prune(p, now));

  // AI bug report: bug / unclear, the prompt switches and the transcript goes alone
  AiResult a = ai_parse("```json\n{\"type\":\"bug\",\"title\":\"Clock stops in sleep\",\"body\":\"After an hour asleep\\nthe clock stays.\"}\n```");
  assert(a.kind == AI_BUG && a.note.title == "Clock stops in sleep" && a.note.body == "After an hour asleep the clock stays.");
  assert(ai_parse("{\"type\":\"bug\",\"title\":\"\",\"body\":\"x\"}").kind == AI_BAD);
  assert(ai_parse("{\"type\":\"unclear\"}").kind == AI_UNCLEAR);
  assert(ai_parse(String("{\"type\":\"bug\",\"title\":\"t\",\"body\":\"") + String(std::string(900, 'b')) + "\"}").note.body.length() == BUG_BODY_MAX);
  deserializeJson(d, ai_body("m", Route(), true, "2026-11-25 10:00", "# Title", "the clock stops", true).c_str());
  assert(String(d["messages"][0]["content"] | "") == BUG_PROMPT && String(d["messages"][1]["content"] | "") == "Transcript: the clock stops");
  deserializeJson(d, ai_body("m", Route(), false, "", "", "x").c_str());
  assert(String(d["messages"][0]["content"] | "") == AI_PROMPT && String(d["messages"][1]["content"] | "").startsWith("Now: "));
  puts("report_test: all passed");
}
