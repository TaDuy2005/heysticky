// Host check for heysticky/reader.h. Run from the repo root:
//   c++ -std=c++17 -I heysticky tools/reader_test.cpp -o /tmp/reader_test && /tmp/reader_test
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

// Just enough of Arduino's String for reader.h.
struct String : std::string {
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  unsigned length() const { return unsigned(size()); }
  String substring(size_t a, size_t b = npos) const { return a >= size() ? "" : String(substr(a, b == npos ? npos : b - a)); }
  int indexOf(char c, size_t from = 0) const { auto p = find(c, from); return p == npos ? -1 : int(p); }
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

#include "reader.h"

static Book mk(const char *path, const char *title, uint32_t seq) {
  Book b;
  b.path = path;
  b.title = title;
  b.seq = seq;
  return b;
}

int main() {
  // books.tsv round trip, Vietnamese included; tabs and newlines in names can't break a line
  Book b = mk("/books/Truyện Kiều.txt", "Truyện Kiều", 7);
  b.author = "Nguyễn\tDu";
  b.size = 140000; b.chapter = 3; b.offset = 4096000; b.pct = 15;
  Book r;
  assert(book_parse(book_format(b), r));
  assert(r.path == b.path && r.title == "Truyện Kiều" && r.author == "Nguyễn Du" && r.size == 140000 && r.seq == 7 &&
         r.chapter == 3 && r.offset == 4096000 && r.pct == 15);
  assert(book_parse("/books/a.epub\t10\t0\t0\t0\t0", r) && r.title == "" && r.author == "");  // title not read yet
  assert(!book_parse("not a path\t1\t2\t3\t4\t5", r));
  assert(!book_parse("/books/short\t1", r));
  assert(book_parse("/books/x.md\t1\t0\t0\t0\t250\tX\tY", r) && r.pct == 100);  // clamped

  // which files are books
  assert(book_file("Moby-Dick.EPUB") && book_file("notes.md") && book_file("a.txt"));
  assert(!book_file("._Moby-Dick.epub") && !book_file("cover.jpg") && !book_file("book.pdf") && !book_file(""));
  assert(book_name("/books/The Time Machine.epub") == "The Time Machine" && book_name("/books/README") == "README");

  // order: last opened first, then never opened A-Z (case-insensitive)
  std::vector<Book> lib = {mk("/books/m.epub", "moby-Dick", 0), mk("/books/t.epub", "The Time Machine", 9),
                           mk("/books/f.epub", "Frankenstein", 4), mk("/books/a.epub", "", 0),
                           mk("/books/p.epub", "Pride and Prejudice", 0)};
  library_order(lib);
  assert(lib[0].title == "The Time Machine" && lib[1].title == "Frankenstein");
  assert(lib[2].path == "/books/a.epub" && lib[3].title == "moby-Dick" && lib[4].title == "Pride and Prejudice");
  assert(next_seq(lib) == 10);

  // pages: Continue card + 4 rows on page 1, then 6 per page
  assert(lib_continue(lib) && lib_pages(lib) == 1);
  assert(lib_index(lib, 0, 0) == 1 && lib_index(lib, 0, 3) == 4 && lib_index(lib, 0, 4) == -1);
  for (int i = 0; i < 6; i++) lib.push_back(mk("/books/z.txt", "Z", 0));  // 11 books: card + 4, then 6
  assert(lib_pages(lib) == 2 && lib_index(lib, 1, 0) == 5 && lib_index(lib, 1, 5) == 10 && lib_index(lib, 1, 6) == -1);
  lib.push_back(mk("/books/z.txt", "Z", 0));
  assert(lib_pages(lib) == 3 && lib_index(lib, 2, 0) == 11 && lib_index(lib, 2, 1) == -1);
  std::vector<Book> fresh = {mk("/books/a.txt", "A", 0), mk("/books/b.txt", "B", 0)};  // nothing opened: no card
  assert(!lib_continue(fresh) && lib_pages(fresh) == 1 && lib_index(fresh, 0, 0) == 0 && lib_index(fresh, 0, 2) == -1);
  std::vector<Book> none;
  assert(lib_pages(none) == 1 && lib_index(none, 0, 0) == -1);

  // book page taps: left third back, middle menu, right third forward
  assert(page_zone(0) == ZONE_PREV && page_zone(159) == ZONE_PREV && page_zone(160) == ZONE_MENU);
  assert(page_zone(319) == ZONE_MENU && page_zone(320) == ZONE_NEXT && page_zone(479) == ZONE_NEXT);

  puts("reader_test: all passed");
}
