// Host check for heysticky/notes.h. Run from the repo root:
//   c++ -std=c++17 -I heysticky tools/notes_test.cpp -o /tmp/notes_test && /tmp/notes_test
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

// Just enough of Arduino's String for notes.h.
struct String : std::string {
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  unsigned length() const { return unsigned(size()); }
  String substring(size_t a, size_t b = npos) const { return a >= size() ? "" : String(substr(a, b == npos ? npos : b - a)); }
  int indexOf(char c, size_t from = 0) const { auto p = find(c, from); return p == npos ? -1 : int(p); }
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

#include "notes.h"

static int chars(const String &s) {  // width = UTF-8 characters
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

int main() {
  // round trip, Vietnamese and blank lines included
  const char *files[] = {"# Idea: sticky habit grid\nShow a 7×5 grid.\n\nKeep it 1-bit.\n", "# Mua sữa\nCho mẹ", "# \n", "# Empty body\n"};
  for (const char *f : files) assert(note_format(note_parse(f)) == f);
  Note n = note_parse("# Win title\r\nbody\r\n");
  assert(n.title == "Win title" && n.body == "body\r\n");
  n = note_parse("no heading\nsecond");
  assert(n.title == "" && n.body == "no heading\nsecond");
  assert(note_parse("# Only title").title == "Only title" && note_parse("# Only title").body == "");

  // properties block: created date, other lines kept, Windows line ends, unclosed block = plain body
  const char *withProps = "---\ncreated: 2026-11-20 09:05\ntags: idea\n---\n# ESP humidity sensor idea\nSHT40 on the bus.\n";
  n = note_parse(withProps);
  assert(n.created == "2026-11-20 09:05" && n.props == "tags: idea\n" && n.title == "ESP humidity sensor idea" && n.body == "SHT40 on the bus.\n");
  assert(note_format(n) == withProps);
  n = note_parse("---\r\ncreated: 2026-11-20 09:05\r\n---\r\n# T\r\nb");
  assert(n.created == "2026-11-20 09:05" && n.title == "T" && n.body == "b");
  n = note_parse("---\nnot closed\n# T\n");
  assert(n.created == "" && n.title == "" && n.body == "---\nnot closed\n# T\n");
  n = note_parse("# Old note\nbody");
  n.created = "2026-11-20 09:05";
  assert(note_format(n) == "---\ncreated: 2026-11-20 09:05\n---\n# Old note\nbody");

  assert(note_preview("\n  \n  Eggs, rice \nmore") == "Eggs, rice");
  assert(note_preview("") == "");

  assert(note_file_num("012.md") == 12 && note_file_num("7.md") == 7);
  assert(note_file_num("._012.md") == -1 && note_file_num(".md") == -1 && note_file_num("012.txt") == -1);
  assert(note_file_num("012.tmp", ".tmp") == 12);
  assert(note_path(12) == "/notes/012.md" && note_path(1234, ".tmp") == "/notes/1234.tmp");
  assert(note_tag(9) == "#09" && note_tag(123) == "#123");

  // wrap at 10 characters: words, own line breaks, blank lines, long words, UTF-8
  auto w = wrap_lines("Show a 7×5 grid on the\n\nhome", 10, chars);
  assert(w.size() == 5 && w[0] == "Show a 7×5" && w[1] == "grid on" && w[2] == "the" && w[3] == "" && w[4] == "home");
  w = wrap_lines("abcdefghijklmnoếp", 10, chars);
  assert(w.size() == 2 && w[0] == "abcdefghij" && w[1] == "klmnoếp");
  w = wrap_lines("", 10, chars);
  assert(w.size() == 1 && w[0] == "");
  w = wrap_lines("end\n", 10, chars);
  assert(w.size() == 2 && w[1] == "");

  puts("notes_test OK");
}
