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

  // links
  assert(q_enc("Clock stops, 9:41 AM") == "Clock+stops%2C+9%3A41+AM" && q_enc("a~b_c-d.e") == "a~b_c-d.e");
  assert(q_enc("a b+c", true) == "a%20b%2Bc");
  Report r{"HS-7K2P", "Clock stops in sleep"};
  const String V = FW_VERSION;
  assert(report_gh_url(r, "It sleeps.", true) == "https://github.com/TaDuy2005/heysticky/issues/new?title=Clock+stops+in+sleep&body=It+sleeps.%0A%0AReport+HS-7K2P%2C+HeySticky+" + V);
  // blank: no title (GitHub asks), the guide in the Sticky's language, then the ID line
  assert(report_gh_url({"HS-7K2P", ""}, "", false) == "https://github.com/TaDuy2005/heysticky/issues/new?title=&body=" + q_enc(report_guide(false)) + "%0A%0AReport+HS-7K2P%2C+HeySticky+" + V);
  assert(report_guide(false).startsWith("What happened:") && report_guide(true).startsWith("Lỗi gì"));
  const String subj = "mailto:heystickyreport@gmail.com?subject=Confirmation%20Bug%20Report%20%23HS-7K2P%2C%20HeySticky%20" + V;
  assert(report_mail_url(r, "It sleeps.", false) == subj + "&body=Clock%20stops%20in%20sleep%0A%0AIt%20sleeps.");  // 04r: ready to send
  assert(report_mail_url({"HS-7K2P", ""}, "", true) == subj + "&body=" + q_enc(report_guide(true), true));
  assert(report_mail_url({"HS-7K2P", ""}, "", true).length() < 586 && report_gh_url({"HS-7K2P", ""}, "", true).length() < 586);
  // a full AI report must stay a QR the 260 px frame draws at 3 px a module (version 16, 586 bytes at ECC low)
  const String title40(std::string(40, 'w')), body300 = [] {
    std::string b;
    while (b.size() < 300) b += "words ";
    return String(b.substr(0, 300));
  }();
  assert(report_gh_url({"HS-7K2P", title40}, body300, false).length() < 586);
  assert(report_mail_url({"HS-7K2P", title40}, body300, false).length() < 586);

  // AI bug report: bug / unclear, the prompt switches and the transcript goes alone
  AiResult a = ai_parse("```json\n{\"type\":\"bug\",\"title\":\"Clock stops in sleep\",\"body\":\"After an hour asleep\\nthe clock stays.\"}\n```");
  assert(a.kind == AI_BUG && a.note.title == "Clock stops in sleep" && a.note.body == "After an hour asleep the clock stays.");
  assert(ai_parse("{\"type\":\"bug\",\"title\":\"\",\"body\":\"x\"}").kind == AI_BAD);
  assert(ai_parse("{\"type\":\"unclear\"}").kind == AI_UNCLEAR);
  assert(ai_parse(String("{\"type\":\"bug\",\"title\":\"t\",\"body\":\"") + String(std::string(900, 'b')) + "\"}").note.body.length() == BUG_BODY_MAX);
  JsonDocument d;
  deserializeJson(d, ai_body("m", Route(), true, "2026-11-25 10:00", "# Title", "the clock stops", true).c_str());
  assert(String(d["messages"][0]["content"] | "") == BUG_PROMPT && String(d["messages"][1]["content"] | "") == "Transcript: the clock stops");
  deserializeJson(d, ai_body("m", Route(), false, "", "", "x").c_str());
  assert(String(d["messages"][0]["content"] | "") == AI_PROMPT && String(d["messages"][1]["content"] | "").startsWith("Now: "));
  puts("report_test: all passed");
}
