// Notes model: /notes/NNN.md <-> Note, file naming, word wrap. No Arduino I/O here, so
// tools/notes_test.cpp checks it on the Mac (with a String shim).
// File: "# Title\n" then the body, kept exactly as written.
#pragma once
#include <stdio.h>
#include <vector>

constexpr unsigned NOTE_BYTES = 4096;   // bytes per note file, read and typed (RAM bound)
constexpr unsigned TITLE_MAX = 120;

struct Note { int num = 0; String title, body; };       // num 0 = not saved yet
struct NoteHead { int num; String title, preview; };     // what the list shows

inline Note note_parse(const String &s) {
  Note n;
  if (!s.startsWith("# ")) { n.body = s; return n; }  // no heading: the whole file is the body
  int nl = s.indexOf('\n');
  if (nl < 0) nl = s.length();
  n.title = s.substring(2, nl);
  if (n.title.endsWith("\r")) n.title = n.title.substring(0, n.title.length() - 1);
  n.body = s.substring(nl + 1);
  return n;
}

inline String note_format(const Note &n) { return "# " + n.title + "\n" + n.body; }

inline String note_preview(const String &body) {  // first line with text
  for (int i = 0; i <= int(body.length());) {
    int j = body.indexOf('\n', i);
    if (j < 0) j = body.length();
    String l = body.substring(i, j);
    l.trim();
    if (l.length()) return l;
    i = j + 1;
  }
  return "";
}

// "012.md" -> 12; anything else -> -1 (macOS "._012.md" junk included).
inline int note_file_num(const String &name, const char *ext = ".md") {
  const String e(ext);
  if (!name.endsWith(e.c_str()) || name.length() == e.length()) return -1;
  for (unsigned i = 0; i < name.length() - e.length(); i++)
    if (name[i] < '0' || name[i] > '9') return -1;
  return name.toInt();
}

inline String note_path(int num, const char *ext = ".md") {
  char b[24];
  snprintf(b, sizeof b, "/notes/%03d%s", num, ext);
  return b;
}

inline String note_tag(int num) {  // "#09"
  char b[12];
  snprintf(b, sizeof b, "#%02d", num);
  return b;
}

// Keyboard auto-capital: at the start of a field or line, and after ". ", "! ", "? ".
inline bool cap_next(const String &s) {
  int i = s.length();
  while (i > 0 && s[i - 1] == ' ') i--;
  if (i == 0 || s[i - 1] == '\n') return true;
  return i < int(s.length()) && (s[i - 1] == '.' || s[i - 1] == '!' || s[i - 1] == '?');
}

// Word wrap to lines no wider than w (width() measures a String). Keeps the body's own line
// breaks; a word wider than the line is split by character (UTF-8 safe).
template <class Width>
std::vector<String> wrap_lines(const String &s, int w, Width width) {
  std::vector<String> out;
  for (int i = 0; i <= int(s.length());) {
    int nl = s.indexOf('\n', i);
    if (nl < 0) nl = s.length();
    String line;
    for (int k = i; k <= nl;) {
      int sp = s.indexOf(' ', k);
      if (sp < 0 || sp > nl) sp = nl;
      String word = s.substring(k, sp);
      if (word.endsWith("\r")) word = word.substring(0, word.length() - 1);
      const String next = line.length() ? line + " " + word : word;
      if (width(next) <= w) line = next;
      else {
        if (line.length()) out.push_back(line);
        line = "";
        for (unsigned c = 0; c < word.length();) {  // split an over-long word
          unsigned e = c + 1;
          while (e < word.length() && (uint8_t(word[e]) & 0xC0) == 0x80) e++;
          const String ch = word.substring(c, e);
          if (line.length() && width(line + ch) > w) { out.push_back(line); line = ""; }
          line = line + ch;
          c = e;
        }
      }
      k = sp + 1;
    }
    out.push_back(line);
    i = nl + 1;
  }
  return out;
}
