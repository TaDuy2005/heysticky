// Reader model: the library list (/.reader/books.tsv), its order and pages, and where taps land on a book page.
// No Arduino I/O here, so tools/reader_test.cpp checks it on the Mac (with a String shim). The layout itself is
// CrossPoint's engine (reader_engine.h).
#pragma once
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>

struct Book {
  String path, title, author;  // path on the card, e.g. "/books/Frankenstein.epub"
  uint32_t size = 0;           // file size when last seen: a changed file is read again
  uint32_t seq = 0;            // order last opened (higher = more recent); 0 = never opened
  int chapter = 0;             // saved place: spine index + visible text offset (survives a text-size change)
  uint32_t offset = 0;
  int pct = 0;                 // % read, for the library
};

inline bool book_file(const String &name) {  // .epub, .txt, .md (any case); hidden files are not books
  if (!name.length() || name[0] == '.') return false;
  String n = name;
  for (unsigned i = 0; i < n.length(); i++) n[i] = char(tolower(uint8_t(n[i])));
  return n.endsWith(".epub") || n.endsWith(".txt") || n.endsWith(".md");
}

// One line per book: path, size, seq, chapter, offset, pct, title, author (tab-separated; tabs in names become spaces).
inline String tsv_clean(String s) {
  for (unsigned i = 0; i < s.length(); i++)
    if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r') s[i] = ' ';
  return s;
}

inline String book_format(const Book &b) {
  return tsv_clean(b.path) + "\t" + String(int(b.size)) + "\t" + String(int(b.seq)) + "\t" + String(b.chapter) + "\t" +
         String(int(b.offset)) + "\t" + String(b.pct) + "\t" + tsv_clean(b.title) + "\t" + tsv_clean(b.author);
}

inline bool book_parse(const String &line, Book &b) {
  String f[8];
  int n = 0, from = 0;
  while (n < 8) {
    const int tab = line.indexOf('\t', from);
    f[n++] = tab < 0 ? line.substring(from) : line.substring(from, tab);
    if (tab < 0) break;
    from = tab + 1;
  }
  if (n < 6 || !f[0].startsWith("/")) return false;
  b = Book();
  b.path = f[0];
  b.size = uint32_t(strtoul(f[1].c_str(), nullptr, 10));
  b.seq = uint32_t(strtoul(f[2].c_str(), nullptr, 10));
  b.chapter = int(f[3].toInt());
  b.offset = uint32_t(strtoul(f[4].c_str(), nullptr, 10));
  b.pct = std::min(100, std::max(0, int(f[5].toInt())));
  b.title = f[6];
  b.author = f[7];
  b.title.trim();
  b.author.trim();
  return true;
}

// A book's own title, without what Project Gutenberg adds after " / " ("Alice's Adventures in Wonderland / Illustrated
// by Arthur Rackham. With a Proem by ..."): the library has room for two lines.
inline String book_title_clean(String t) {
  const int cut = t.indexOf(" / ");
  if (cut > 0) t = t.substring(0, cut);
  t.trim();
  return t;
}

// File name without folder and extension: the title until the book's own title is read.
inline String book_name(const String &path) {
  const int slash = path.lastIndexOf("/"), dot = path.lastIndexOf(".");
  return path.substring(slash + 1, dot > slash ? dot : path.length());
}

inline bool title_less(const String &a, const String &b) {  // A-Z, ignoring ASCII case
  for (unsigned i = 0; i < a.length() && i < b.length(); i++) {
    const int ca = tolower(uint8_t(a[i])), cb = tolower(uint8_t(b[i]));
    if (ca != cb) return ca < cb;
  }
  return a.length() < b.length();
}

// 07 Library order: most recently opened first, then the books never opened, A-Z.
inline void library_order(std::vector<Book> &books) {
  std::stable_sort(books.begin(), books.end(), [](const Book &a, const Book &b) {
    if (a.seq != b.seq) return a.seq > b.seq;
    return title_less(a.title.length() ? a.title : book_name(a.path), b.title.length() ? b.title : book_name(b.path));
  });
}

inline uint32_t next_seq(const std::vector<Book> &books) {
  uint32_t m = 0;
  for (const Book &b : books) m = std::max(m, b.seq);
  return m + 1;
}

// 07 pages: page 1 shows the Continue card (the last book opened) and 4 rows; every other page 6 rows.
constexpr int LIB_ROWS = 6, LIB_ROWS_FIRST = 4;
inline bool lib_continue(const std::vector<Book> &books) { return !books.empty() && books[0].seq > 0; }
inline int lib_pages(const std::vector<Book> &books) {
  const int rows = int(books.size()) - (lib_continue(books) ? 1 : 0), first = lib_continue(books) ? LIB_ROWS_FIRST : LIB_ROWS;
  return rows <= first ? 1 : 1 + (rows - first + LIB_ROWS - 1) / LIB_ROWS;
}
// Index into books of row `r` on page `p`, or -1.
inline int lib_index(const std::vector<Book> &books, int p, int r) {
  const bool cont = lib_continue(books);
  const int first = cont ? LIB_ROWS_FIRST : LIB_ROWS;
  if (r < 0 || r >= (p == 0 ? first : LIB_ROWS)) return -1;
  const int i = (cont ? 1 : 0) + (p == 0 ? r : first + (p - 1) * LIB_ROWS + r);
  return i < int(books.size()) ? i : -1;
}

// 07d book page, three columns: left = previous page, middle = reader menu (07e), right = next page.
enum PageZone : uint8_t { ZONE_PREV, ZONE_MENU, ZONE_NEXT };
inline PageZone page_zone(int x) { return x < 160 ? ZONE_PREV : x < 320 ? ZONE_MENU : ZONE_NEXT; }

// 07f Contents gaps: a chapter file the book's own table of contents skips is listed under its opening, the text of
// its first one or two blocks (Alice: "CHAPTER I" + "Down the Rabbit-Hole"), up to TOC_FILL_MAX bytes. A file that
// opens with a longer block (the rest of a split chapter) or has no text (a cover) gets no entry: "".
// ponytail: a split file opening with a short line ("Yes.") still gets one; fine for Gutenberg-style books.
constexpr size_t TOC_FILL_MAX = 60;
inline std::string html_opening_title(const std::string &h) {
  static const char *const BLOCKS[] = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "td", "th", "tr", "br",
                                       "blockquote", "figure", "section", "table", "body"};
  const size_t body = h.find("<body");
  std::string out, block;
  int blocks = 0;
  auto add = [&](const std::string &t) { if (!t.empty() && block.size() <= TOC_FILL_MAX) block += t; };
  auto utf8 = [](unsigned c) {
    std::string u;
    if (c < 0x80) u += char(c);
    else if (c < 0x800) u += char(0xC0 | c >> 6), u += char(0x80 | (c & 63));
    else u += char(0xE0 | c >> 12), u += char(0x80 | (c >> 6 & 63)), u += char(0x80 | (c & 63));
    return u;
  };
  // false = stop: two blocks taken, or one too long
  auto end_block = [&]() {
    while (!block.empty() && block.back() == ' ') block.pop_back();
    if (block.empty()) return true;
    const std::string b = block;
    block.clear();
    if (b.size() > TOC_FILL_MAX || (!out.empty() && out.size() + 1 + b.size() > TOC_FILL_MAX)) return false;
    out += (out.empty() ? "" : " ") + b;
    return ++blocks < 2;
  };
  for (size_t i = body == std::string::npos ? 0 : h.find('>', body) + 1; i < h.size(); i++) {
    const char c = h[i];
    if (c == '<') {
      const size_t e = h.find('>', i);
      if (e == std::string::npos) break;
      size_t a = i + 1 + (h[i + 1] == '/');
      std::string tag;
      while (a < e && isalnum(uint8_t(h[a]))) tag += char(tolower(uint8_t(h[a++])));
      i = e;
      if (std::find_if(std::begin(BLOCKS), std::end(BLOCKS), [&](const char *b) { return tag == b; }) != std::end(BLOCKS) &&
          !end_block())
        break;
    } else if (c == '&') {
      const size_t e = h.find(';', i);
      if (e == std::string::npos || e - i > 8) { add("&"); continue; }
      const std::string n = h.substr(i + 1, e - i - 1);
      i = e;
      if (n == "amp") add("&");
      else if (n == "nbsp") add(block.empty() || block.back() == ' ' ? "" : " ");
      else if (n == "lt") add("<");
      else if (n == "gt") add(">");
      else if (n == "quot") add("\"");
      else if (n == "apos") add("'");
      else if (n.size() > 1 && n[0] == '#') add(utf8(unsigned(n[1] == 'x' ? strtoul(n.c_str() + 2, nullptr, 16) : strtoul(n.c_str() + 1, nullptr, 10))));
    } else if (isspace(uint8_t(c))) {
      add(block.empty() || block.back() == ' ' ? "" : " ");
    } else {
      add(std::string(1, c));
    }
  }
  if (blocks < 2) end_block();
  return out;
}
