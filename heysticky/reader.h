// Reader model: the library list (/.reader/books.tsv), its order and pages, and where taps land on a book page.
// No Arduino I/O here, so tools/reader_test.cpp checks it on the Mac (with a String shim). The layout itself is
// CrossPoint's engine (reader_engine.h).
#pragma once
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
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
