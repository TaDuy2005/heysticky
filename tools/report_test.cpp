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
  const String t = reports_text(v);
  assert(t == "HS-AAAA\tf\tClock stops in sleep\nHS-BBBB\to\t\n");
  auto w = reports_parse(t + "garbage\nHS-CCCC\tq\tbad state\n");
  assert(w.size() == 2 && w[0].title == "Clock stops in sleep" && w[1].title == "" && w[1].st == 'o');
  for (int i = 0; i < 20; i++) reports_add(v, {report_id(i), "x", 'w'});
  assert(int(v.size()) == REPORTS_MAX && int(reports_parse(reports_text(v)).size()) == REPORTS_MAX);

  // links
  assert(q_enc("Clock stops, 9:41 AM") == "Clock+stops%2C+9%3A41+AM" && q_enc("a~b_c-d.e") == "a~b_c-d.e");
  Report r{"HS-7K2P", "Clock stops in sleep", 'w'};
  const String V = FW_VERSION;
  assert(report_url(r, "It sleeps.", false, true) == "https://sticky.davidtalab.cc/r?id=HS-7K2P&v=" + V + "&via=gh&t=Clock+stops+in+sleep&b=It+sleeps.");
  assert(report_url(r, "", true, false) == "https://sticky.davidtalab.cc/r?id=HS-7K2P&v=" + V + "&l=vi&t=Clock+stops+in+sleep");
  assert(report_url({"HS-7K2P", "", 'w'}, "", false, false) == "https://sticky.davidtalab.cc/r?id=HS-7K2P&v=" + V);
  // a full AI report must stay a QR the 260 px frame draws at 3 px a module (version 16, 586 bytes at ECC low)
  const String longBody(std::string(300, 'w'));
  assert(report_url({"HS-7K2P", String(std::string(40, 'w')), 'w'}, longBody, true, true).length() < 586);

  // states from the server: one request; left out = cancelled / unfinished / never opened -> dropped
  std::vector<Report> s = {{"HS-AAAA", "A", 'w'}, {"HS-BBBB", "", 'w'}, {"HS-CCCC", "C", 'o'}, {"HS-DDDD", "D", 'w'}};
  assert(report_status_url(s) == "https://sticky.davidtalab.cc/s?ids=HS-AAAA,HS-BBBB,HS-CCCC,HS-DDDD");
  assert(report_status_url({}) == "https://sticky.davidtalab.cc/s?ids=");
  JsonDocument d;
  deserializeJson(d, R"({"HS-AAAA":{"st":"f","t":"Clock stops"},"HS-BBBB":{"st":"o","t":"Weather shows --"},"HS-DDDD":{"st":"w","t":""}})");
  assert(reports_apply(s, d.as<JsonObjectConst>()));
  assert(s.size() == 3 && s[0].id == "HS-AAAA" && s[0].st == 'f' && s[0].title == "A");  // the Sticky's own title stays
  assert(s[1].st == 'o' && s[1].title == "Weather shows --" && s[2].id == "HS-DDDD" && s[2].st == 'w');
  assert(!reports_apply(s, d.as<JsonObjectConst>()));  // nothing new
  deserializeJson(d, R"({"HS-AAAA":{"st":"x"},"HS-BBBB":{"st":"q"},"HS-DDDD":7})");  // a made-up state or shape: dropped
  assert(reports_apply(s, d.as<JsonObjectConst>()) && s.size() == 1 && s[0].st == 'x');
  deserializeJson(d, "{}");
  assert(reports_apply(s, d.as<JsonObjectConst>()) && s.empty());

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
