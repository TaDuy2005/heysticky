// HeySticky addition: a small Markdown subset for .md books (CrossPoint shows .md as plain text).
// Headings (# .. ######) become <h1>..<h6> with an id ("md1", "md2", ...) that the TOC points at; paragraphs join
// their lines; list items get a bullet; **bold**, __bold__, *italic*, _italic_, `code`, [links](url) are rendered,
// pictures ![..](..) dropped; fenced code blocks are shown as plain lines. No std:: beyond <string>, so
// tools/reader_test.cpp checks it on the Mac.
#pragma once
#include <cctype>
#include <string>
#include <string_view>

namespace mdlite {

inline std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

inline void escape(std::string &out, std::string_view s) {
  for (const char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
}

// Emphasis, code and links inside one line.
inline void inline_html(std::string &out, std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    const char c = s[i];
    if (c == '\\' && i + 1 < s.size() && std::ispunct(static_cast<unsigned char>(s[i + 1]))) {  // \* = a literal *
      escape(out, s.substr(i + 1, 1));
      i += 2;
      continue;
    }
    if (c == '`') {
      const size_t e = s.find('`', i + 1);
      if (e != std::string_view::npos) {
        escape(out, s.substr(i + 1, e - i - 1));
        i = e + 1;
        continue;
      }
    }
    if ((c == '[' || (c == '!' && i + 1 < s.size() && s[i + 1] == '['))) {  // [text](url) -> text; ![alt](src) -> nothing
      const size_t open = c == '!' ? i + 1 : i, close = s.find("](", open);
      const size_t end = close == std::string_view::npos ? close : s.find(')', close + 2);
      if (end != std::string_view::npos) {
        if (c == '[') inline_html(out, s.substr(open + 1, close - open - 1));
        i = end + 1;
        continue;
      }
    }
    if (c == '*' || c == '_') {
      const size_t run = i + 1 < s.size() && s[i + 1] == c ? 2 : 1;
      const bool wordStart = c == '*' || i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1]));  // snake_case stays
      const std::string marker(run, c);
      const size_t e = wordStart && i + run < s.size() && s[i + run] != ' ' ? s.find(marker, i + run) : std::string_view::npos;
      if (e != std::string_view::npos && e > i + run) {
        const char *tag = run == 2 ? "b" : "i";
        out += '<';
        out += tag;
        out += '>';
        inline_html(out, s.substr(i + run, e - i - run));
        out += "</";
        out += tag;
        out += '>';
        i = e + run;
        continue;
      }
    }
    escape(out, s.substr(i, 1));
    i++;
  }
}

// Line classification shared by the HTML converter and the TOC pass, so heading anchors match.
struct Scanner {
  bool fence = false;  // inside ``` / ~~~
  int headings = 0;

  static bool is_fence(std::string_view t) { return t.substr(0, 3) == "```" || t.substr(0, 3) == "~~~"; }

  // true: `line` is a heading; level 1-6, text without the #s; headings counts it (its anchor is "md" + headings).
  bool heading(std::string_view line, int &level, std::string_view &text) {
    if (line.substr(0, 3) == "\xEF\xBB\xBF") line.remove_prefix(3);  // BOM: TxtToHtml skips it, so must the TOC pass
    const std::string_view t = trim(line);
    if (is_fence(t)) {
      fence = !fence;
      return false;
    }
    if (fence || t.empty() || t[0] != '#') return false;
    size_t n = 0;
    while (n < t.size() && t[n] == '#') n++;
    if (n > 6 || (n < t.size() && t[n] != ' ')) return false;
    std::string_view rest = trim(t.substr(n));
    while (!rest.empty() && rest.back() == '#') rest.remove_suffix(1);  // "## Title ##"
    level = int(n);
    text = trim(rest);
    headings++;
    return true;
  }
};

// Feed lines (without '\n'); read and clear `out` as you go; finish() at the end.
class Converter {
 public:
  std::string out;

  void line(std::string_view l) {
    int level;
    std::string_view text;
    const bool wasFence = scan.fence;
    if (scan.heading(l, level, text)) {
      flush();
      out += "<h" + std::to_string(level) + " id=\"md" + std::to_string(scan.headings) + "\">";
      inline_html(out, text);
      out += "</h" + std::to_string(level) + ">\n";
      return;
    }
    const std::string_view t = trim(l);
    if (wasFence != scan.fence) { flush(); return; }  // the ``` line itself
    if (scan.fence) {  // code: each line as it is
      out += "<p>";
      escape(out, t);
      out += "</p>\n";
      return;
    }
    if (t.empty() || rule(t)) { flush(); return; }
    std::string_view item = t;
    const char *bullet = list_item(item);
    if (bullet) {
      flush();
      out += "<p>";
      out += bullet;
      inline_html(out, item);
      out += "</p>\n";
      return;
    }
    if (t[0] == '>') item = trim(t.substr(1));  // quote: its text
    if (!para.empty()) para += ' ';
    para.append(item.data(), item.size());
  }
  void finish() { flush(); }

 private:
  Scanner scan;
  std::string para, number;

  void flush() {
    if (para.empty()) return;
    out += "<p>";
    inline_html(out, para);
    out += "</p>\n";
    para.clear();
  }
  static bool rule(std::string_view t) {  // --- *** ___
    if (t.size() < 3 || (t[0] != '-' && t[0] != '*' && t[0] != '_')) return false;
    for (const char c : t)
      if (c != t[0] && c != ' ') return false;
    return true;
  }
  // "- x", "* x", "+ x" -> "• "; "1. x", "1) x" -> "1. ". item becomes the text after the marker.
  const char *list_item(std::string_view &item) {
    if (item.size() > 2 && (item[0] == '-' || item[0] == '*' || item[0] == '+') && item[1] == ' ') {
      item = trim(item.substr(2));
      return "\xE2\x80\xA2 ";  // U+2022 bullet
    }
    size_t n = 0;
    while (n < item.size() && n < 4 && std::isdigit(static_cast<unsigned char>(item[n]))) n++;
    if (n && n + 1 < item.size() && (item[n] == '.' || item[n] == ')') && item[n + 1] == ' ') {
      number = std::string(item.substr(0, n)) + ". ";
      item = trim(item.substr(n + 2));
      return number.c_str();
    }
    return nullptr;
  }
};

}  // namespace mdlite
