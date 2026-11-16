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

int main() {
  // round trip, Vietnamese included; non-task lines survive untouched
  const char *lines[] = {"- [ ] Call the dentist @due(2026-11-17 09:00)", "- [x] Flash HelloWorld",
                         "- [ ] Mua sữa cho mẹ @due(2026-11-20)", "# Groceries", ""};
  for (const char *l : lines) assert(todo_format(todo_parse(l)) == l);
  Task t = todo_parse("  - [X] Ship it  @due(2026-11-17 09:00)\r");
  assert(t.task && t.done && t.text == "Ship it" && t.due == "2026-11-17 09:00");
  assert(todo_parse("- [ ] bad date @due(2026-1-7)").text == "bad date @due(2026-1-7)");
  assert(!todo_parse("- [ ]").task);

  // dates
  assert(ymd(ymd_num("2026-11-17")) == "2026-11-17" && ymd(ymd_num("2024-02-28") + 1) == "2024-02-29");
  assert(weekday(ymd_num("2026-11-17")) == 2);  // Tuesday
  assert(ymd(ymd_num("2026-12-31") + 1) == "2027-01-01");

  // labels at Tue 2026-11-17 10:30
  const String now = "2026-11-17 10:30";
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-16 09:00)"), now) == "Overdue · Mon");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-17 09:00)"), now) == "Overdue · 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-17 19:00)"), now) == "Today 19:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-17)"), now) == "Today");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-18 09:00)"), now) == "Tomorrow 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-11-20 09:00)"), now) == "Fri 9:00");
  assert(todo_label(todo_parse("- [ ] a @due(2026-12-24 09:00)"), now) == "24 Dec 9:00");

  // order: overdue, today, tomorrow, undated (file order), done last
  std::vector<Task> ts;
  for (const char *l : {"- [x] done", "- [ ] buy milk", "- [ ] tomorrow @due(2026-11-18 09:00)", "# note",
                        "- [ ] overdue @due(2026-11-16 09:00)", "- [ ] today @due(2026-11-17 19:00)", "- [ ] second undated"})
    ts.push_back(todo_parse(l));
  std::string got;
  for (int i : todo_order(ts)) got += ts[i].text + "|";
  assert(got == "overdue|today|tomorrow|buy milk|second undated|done|");
  assert(todo_left(ts) == 5 && todo_due_today(ts, now) == 1);
  puts("todo_test OK");
}
