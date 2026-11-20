// Host check for heysticky/todo.h. Run from the repo root:
//   c++ -std=c++17 -I heysticky tools/todo_test.cpp -o /tmp/todo_test && /tmp/todo_test
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

// Just enough of Arduino's String for todo.h.
struct String : std::string {
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  unsigned length() const { return unsigned(size()); }
  String substring(size_t a, size_t b = npos) const { return a >= size() ? "" : String(substr(a, b == npos ? npos : b - a)); }
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
inline String operator+(const String &a, const char *b) { return String(static_cast<const std::string &>(a) + static_cast<const std::string &>(b)); }
inline String operator+(const char *a, const String &b) { return String(a + static_cast<const std::string &>(b)); }

#include "todo.h"
#include "wrap.h"

int main() {
  // round trip, Vietnamese included; non-task lines survive untouched
  const char *lines[] = {"- [ ] Call the dentist @due(2026-11-17 09:00)", "- [x] Flash HelloWorld",
                         "- [ ] Mua sữa cho mẹ @due(2026-11-20)", "# Groceries", ""};
  for (const char *l : lines) assert(todo_format(todo_parse(l)) == l);
  Task t = todo_parse("  - [X] Ship it  @due(2026-11-17 09:00)\r");
  assert(t.task && t.done && t.text == "Ship it" && t.due == "2026-11-17 09:00");
  assert(todo_parse("- [ ] bad date @due(2026-1-7)").text == "bad date @due(2026-1-7)");
  assert(http_date("Thu, 20 Nov 2026 09:05:07 GMT") == 1795165507LL);
  assert(http_date("Thu, 01 Jan 1970 00:00:00 GMT") == 0 && http_date("garbage") == 0 && http_date("Thu, 20 Foo 2026 09:05:07 GMT") == 0);
  // created date: either order with @due, round trip writes due first
  t = todo_parse("- [ ] Pay rent @due(2026-12-01) @created(2026-11-20 09:05)");
  assert(t.text == "Pay rent" && t.due == "2026-12-01" && t.created == "2026-11-20 09:05");
  assert(todo_format(t) == "- [ ] Pay rent @due(2026-12-01) @created(2026-11-20 09:05)");
  t = todo_parse("- [x] Call @created(2026-11-20 09:05) @due(2026-11-21 10:00)");
  assert(t.text == "Call" && t.due == "2026-11-21 10:00" && t.created == "2026-11-20 09:05");
  t = todo_parse("- [ ] Only made @created(2026-11-20 09:05)");
  assert(t.text == "Only made" && t.due == "" && todo_format(t) == "- [ ] Only made @created(2026-11-20 09:05)");
  assert(todo_parse("- [ ] mail me@due.vn").text == "mail me@due.vn");
  assert(!todo_parse("- [ ]").task);

  // dates
  assert(ymd(ymd_num("2026-11-17")) == "2026-11-17" && ymd(ymd_num("2024-02-28") + 1) == "2024-02-29");
  assert(weekday(ymd_num("2026-11-17")) == 2);  // Tuesday
  assert(ymd(ymd_num("2026-12-31") + 1) == "2027-01-01");

  // labels at Tue 2026-11-17 10:30
  const String now = "2026-11-17 10:30";
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-16)"), now) == "Overdue · 16/11");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-17 09:00)"), now) == "Overdue · 17/11 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-17 19:00)"), now) == "Today 19:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-17)"), now) == "Today");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-18 09:00)"), now) == "Tomorrow 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-20 09:00)"), now) == "Fri 20/11 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-12-24 09:00)"), now) == "Thu 24/12 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-12-24 09:00)"), "") == "24/12 9:00");  // no clock yet
  g_vi = true;
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-16)"), now) == "Quá hạn · 16/11");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-17 19:00)"), now) == "Hôm nay 19:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-18 09:00)"), now) == "Ngày mai 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-20 09:00)"), now) == "T6 20/11 9:00");
  g_vi = false;

  // wrap: 1 px a byte; spaces break, a too-long word breaks anywhere, "…" when out of lines
  auto px = [](const String &s) { return int(s.length()); };
  auto wrap = [&](const char *s, int w, int n) { std::string r; for (const String &l : wrap_lines(String(s), w, n, px)) r += l + "|"; return r; };
  assert(wrap("Email Sarah about the meeting", 12, 5) == "Email Sarah|about the|meeting|");
  assert(wrap("ejakfrkwejrhkqwe", 6, 5) == "ejakfr|kwejrh|kqwe|");
  assert(wrap("Email Sarah about the meeting", 11, 2) == "Email Sarah|about th…|" && wrap("Email Sarah about the meeting", 12, 2) == "Email Sarah|about the…|");  // "…" is 3 bytes here
  assert(wrap("  Buy milk  ", 20, 2) == "Buy milk|" && wrap("", 10, 2) == "");
  assert(wrap("Gửi email", 6, 3) == "Gửi|email|");  // never splits a character

  // order: overdue, today, tomorrow, undated (file order), done last
  std::vector<Task> ts;
  for (const char *l : {"- [x] done", "- [ ] buy milk", "- [ ] tomorrow @due(2026-11-18 09:00)", "# note",
                        "- [ ] overdue @due(2026-11-16 09:00)", "- [ ] today @due(2026-11-17 19:00)", "- [ ] second undated"})
    ts.push_back(todo_parse(l));
  std::string got;
  for (int i : todo_order(ts)) got += ts[i].text + "|";
  assert(got == "overdue|today|tomorrow|buy milk|second undated|done|");
  assert(todo_left(ts) == 5 && todo_due_today(ts, now) == 1);

  // 02h time entry ('<' = backspace); returns "HH:MM" plus any refused keys
  auto type = [](const char *keys) {
    TimeEntry te; te.f[0] = "09"; te.f[1] = "00";
    std::string bad;
    for (const char *k = keys; *k; k++) { if (*k == '<') time_back(te); else if (!time_digit(te, *k)) bad += *k; }
    return std::string(te.f[0]) + ":" + std::string(te.f[1]) + (bad.size() ? " !" + bad : "");
  };
  assert(type("1830") == "18:30");
  assert(type("930") == "09:30");       // 9 can only start 09
  assert(type("187") == "18:07");       // so can a minute of 7
  assert(type("25359") == "23:59 !5");  // 25 refused
  assert(type("18<9") == "19:00");      // backspace from a fresh minute goes back into the hour
  assert(type("1830<5") == "18:35");
  puts("todo_test OK");
}
