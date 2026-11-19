// Word wrap without drawing, so tools/todo_test.cpp checks it on the Mac (ui.h passes the font's width).
#pragma once
#include <vector>

inline bool utf8_cont(char c) { return (uint8_t(c) & 0xC0) == 0x80; }

// s as lines no wider than w (width(String) in px), breaking at spaces; a word wider than a line breaks
// anywhere. At most maxLines: text left over ends the last line with "…".
template <class Width> std::vector<String> wrap_lines(const String &s, int w, int maxLines, Width width) {
  std::vector<String> out;
  const int n = s.length();
  int i = 0;
  while (i < n && s[i] == ' ') i++;
  while (i < n && int(out.size()) < maxLines) {
    int end = i, space = -1;
    while (end < n) {  // the longest run from i that fits
      int k = end + 1;
      while (k < n && utf8_cont(s[k])) k++;
      if (width(s.substring(i, k)) > w) break;
      if (s[end] == ' ') space = end;
      end = k;
    }
    if (end < n && s[end] != ' ' && space > i) end = space;  // whole words when they fit
    if (end == i) do end++; while (end < n && utf8_cont(s[end]));  // not even one character fits
    String line = s.substring(i, end);
    i = end;
    while (i < n && s[i] == ' ') i++;
    if (i < n && int(out.size()) == maxLines - 1) {  // more text than lines
      while (line.length() && width(line + "…") > w) {
        int k = line.length() - 1;
        while (k > 0 && utf8_cont(line[k])) k--;
        line = line.substring(0, k);
      }
      line.trim();
      line = line + "…";
    }
    line.trim();
    out.push_back(line);
  }
  return out;
}
