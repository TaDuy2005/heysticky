// To-do model: /todo.md lines <-> tasks, display order, due labels, date maths.
// No Arduino I/O here, so tools/todo_test.cpp can check it on the Mac (with a String shim).
// Dates are local-time strings "YYYY-MM-DD HH:MM" (or "YYYY-MM-DD"): they sort as text.
#pragma once
#include <stdio.h>
#include <vector>
#include <algorithm>

struct Task {
  bool task = true;   // false: a line that is not a task (heading, blank, ...), written back unchanged
  bool done = false;
  String text, due, raw;
};

// ---------- dates (Howard Hinnant's civil-from-days, no time zones involved) ----------
inline long day_num(int y, int m, int d) {  // days since 1970-01-01
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + long(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
}
inline void civil(long z, int &y, int &m, int &d) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = unsigned(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  d = int(doy - (153 * mp + 2) / 5 + 1);
  m = int(mp < 10 ? mp + 3 : mp - 9);
  y = int(yoe + era * 400 + (m <= 2));
}
inline long ymd_num(const String &s) { return day_num(s.substring(0, 4).toInt(), s.substring(5, 7).toInt(), s.substring(8, 10).toInt()); }
inline String ymd(long n) {
  int y, m, d;
  civil(n, y, m, d);
  char b[11];
  snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
  return b;
}
inline int weekday(long n) { return int(((n % 7) + 11) % 7); }  // 0 = Sunday (1970-01-01 was a Thursday)

inline const char *const DAY3[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
inline const char *const MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                                     "August", "September", "October", "November", "December"};
inline const char *const MON3[] ={"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// ---------- /todo.md lines ----------
inline bool valid_due(const String &d) {  // "YYYY-MM-DD" or "YYYY-MM-DD HH:MM"
  if (d.length() != 10 && d.length() != 16) return false;
  for (unsigned i = 0; i < d.length(); i++) {
    const char c = d[i], want = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 ? ':' : 0;
    if (want ? c != want : c < '0' || c > '9') return false;
  }
  return true;
}

inline Task todo_parse(const String &line) {
  Task t;
  String s = line;
  s.trim();
  if (!(s.startsWith("- [ ] ") || s.startsWith("- [x] ") || s.startsWith("- [X] "))) {
    t.task = false;
    t.raw = line;
    return t;
  }
  t.done = s[3] != ' ';
  s = s.substring(6);
  const int at = s.lastIndexOf("@due(");
  if (at >= 0 && s.endsWith(")")) {
    const String d = s.substring(at + 5, s.length() - 1);
    if (valid_due(d)) { t.due = d; s = s.substring(0, at); }
  }
  s.trim();
  t.text = s;
  return t;
}

inline String todo_format(const Task &t) {
  if (!t.task) return t.raw;
  String s = t.done ? "- [x] " : "- [ ] ";
  s += t.text;
  if (t.due.length()) s += " @due(" + t.due + ")";
  return s;
}

// ---------- order + labels ----------
// Open tasks first: dated by due (so overdue comes first), then undated; done tasks last. File order breaks ties.
inline std::vector<int> todo_order(const std::vector<Task> &ts) {
  std::vector<int> o;
  for (int i = 0; i < int(ts.size()); i++) if (ts[i].task) o.push_back(i);
  std::stable_sort(o.begin(), o.end(), [&](int a, int b) {
    const Task &x = ts[a], &y = ts[b];
    if (x.done != y.done) return !x.done;
    if (x.done) return false;
    if (!x.due.length() || !y.due.length()) return x.due.length() > y.due.length();
    return x.due < y.due;
  });
  return o;
}

inline int todo_left(const std::vector<Task> &ts) {
  int n = 0;
  for (const Task &t : ts) n += t.task && !t.done;
  return n;
}

inline int todo_due_today(const std::vector<Task> &ts, const String &now) {  // now = "YYYY-MM-DD HH:MM"
  int n = 0;
  for (const Task &t : ts) n += t.task && !t.done && now.length() && t.due.startsWith(now.substring(0, 10));
  return n;
}

inline String short_hm(const String &due) {  // "2026-11-17 09:00" -> "9:00"; "" for a date-only due
  if (due.length() != 16) return "";
  return due[11] == '0' ? due.substring(12) : due.substring(11);
}

inline String day_month(long n) {  // "24 Nov"
  int y, m, d;
  civil(n, y, m, d);
  return String(d) + " " + MON3[m - 1];
}

// "Overdue · Mon", "Today 19:00", "Tomorrow 9:00", "Fri 9:00", "24 Nov 9:00"
inline String todo_label(const Task &t, const String &now) {
  if (!t.due.length()) return "";
  const String hm = short_hm(t.due), sp = hm.length() ? " " : "";
  if (!now.length()) return day_month(ymd_num(t.due)) + sp + hm;
  const long day = ymd_num(t.due), today = ymd_num(now);
  const bool overdue = t.due.length() == 16 ? t.due < now : day < today;
  if (overdue) return String("Overdue · ") + (day == today ? hm : today - day < 7 ? String(DAY3[weekday(day)]) : day_month(day));
  if (day == today) return "Today" + sp + hm;
  if (day == today + 1) return "Tomorrow" + sp + hm;
  return (day - today < 7 ? String(DAY3[weekday(day)]) : day_month(day)) + sp + hm;
}
