// Reader screens, Figma 07-07h: Library (07 / 07a empty / 07b no card / 07c loading), book page (07d) with the
// reader menu sheet (07e), Contents (07f), Opening (07g), Can't open (07h). Books are /books/*.epub|txt|md;
// /.reader/books.tsv keeps titles and each book's place. Drawing, card I/O and the engine calls; screens.h navigates.
#pragma once
#include "ui.h"
#include "card.h"
#include "notes_ui.h"     // NROW_TOP, NROW_H: library rows match the notes list
#include "settings_ui.h"  // info_row
#include "reader.h"
#include "reader_engine.h"

constexpr const char *BOOKS_DIR = "/books", *READER_DIR = "/.reader", *BOOKS_TSV = "/.reader/books.tsv";

inline std::vector<Book> g_books;  // 07 order
inline int g_libPage = 0;
inline String g_bookPath;          // the open book
inline bool g_menu = false;        // 07e sheet over the page
inline int g_tocPage = 0;          // 07f page
constexpr int TOC_TOP = 112, TOC_H = 64, TOC_ROWS = 9;

inline bool reader_large() { return cfg_get("rd_size") == "M"; }  // 07e Text size: S = Noto Sans 12 pt, M = 14 pt

// ---------- card ----------
inline bool books_save() {
  if (!SD.exists(READER_DIR) && !SD.mkdir(READER_DIR)) return sd_fail("mkdir /.reader");
  File f = SD.open("/.reader/books.tmp", FILE_WRITE);
  if (!f) return sd_fail("open books.tmp");
  for (const Book &b : g_books) f.print(book_format(b) + "\n");
  f.close();
  SD.remove(BOOKS_TSV);
  return SD.rename("/.reader/books.tmp", BOOKS_TSV) || sd_fail("rename books.tsv");
}

// Read /books (the card may have been changed on a computer or the phone page): keep what books.tsv knows about
// files of the same size, read titles of new ones, drop the ones that are gone.
inline bool books_scan() {
  if (!g_sdOk && !sd_mount()) return false;
  std::vector<Book> known;
  if (File f = SD.open(BOOKS_TSV)) {
    while (f.available()) {
      Book b;
      if (book_parse(f.readStringUntil('\n'), b)) known.push_back(b);
    }
    f.close();
  }
  std::vector<Book> now;
  bool changed = false;
  if (File dir = SD.open(BOOKS_DIR)) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      const String name = e.name();
      if (e.isDirectory() || !book_file(name)) continue;
      Book b;
      b.path = String(BOOKS_DIR) + "/" + name;
      b.size = e.size();
      e.close();
      auto k = std::find_if(known.begin(), known.end(), [&](const Book &o) { return o.path == b.path; });
      if (k != known.end() && k->size == b.size) b = *k;
      else changed = true;  // new, or replaced with a different file: its place no longer fits
      if (!b.title.length()) {
        if (!rd::metadata(b.path, b.title, b.author) || !b.title.length()) b.title = book_name(b.path);
        changed = true;
      }
      now.push_back(b);
    }
    dir.close();
  } else if (!SD.mkdir(BOOKS_DIR)) {
    return sd_fail("mkdir /books");
  }
  changed |= now.size() != known.size();
  g_books = now;
  library_order(g_books);
  return !changed || books_save();
}

inline Book *open_book() {
  for (Book &b : g_books)
    if (b.path == g_bookPath) return &b;
  return nullptr;
}

// The place after every page turn, so a pulled card or an empty battery loses at most a page.
inline void book_remember() {
  Book *b = open_book();
  if (!b) return;
  b->chapter = rd::chapter();
  b->offset = rd::offset();
  b->pct = rd::percent();
  books_save();
}

// ---------- 07 Library / 07a empty ----------
inline void draw_library() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Library", "Thư viện"), String(g_books.size()));
  if (g_books.empty()) {
    ui_icon(ICON_APP_BOOK, 210, 303);
    ui_text(F_X30, TR("No books yet", "Chưa có sách"), W / 2, 381, 40, AL_CENTER);
    ui_wrap(F_R25, TR("Copy EPUB, TXT or MD files into /books/ on the card, or upload them on the phone page.",
                      "Chép tệp EPUB, TXT hoặc MD vào /books/ trên thẻ, hoặc tải lên trên trang điện thoại."),
            40, 437, 400, 33, 5, AL_CENTER);
    return;
  }
  g_libPage = constrain(g_libPage, 0, lib_pages(g_books) - 1);
  int y = NROW_TOP;
  if (g_libPage == 0 && lib_continue(g_books)) {  // Continue card: the last book opened
    const Book &b = g_books[0];
    ui_card(40, 128, 400, 172, 14, 3);
    ui_text(F_R20, TR("Continue", "Đọc tiếp"), 60, 144, 26);
    ui_text(F_X28, ui_fit(F_X28, b.title, 360), 60, 172, 37);
    ui_text(F_R25, ui_fit(F_R25, b.author, 360), 60, 212, 33);
    display.fillRoundRect(60, 260, 280, 16, 8, INK);
    display.fillRoundRect(62, 262, 276, 12, 6, PAPER);
    display.fillRect(62, 262, 276 * b.pct / 100, 12, INK);
    ui_text(F_R25, String(b.pct) + "%", 420, 251, 33, AL_RIGHT);
    y = 316;
  }
  for (int r = 0;; r++) {
    const int i = lib_index(g_books, g_libPage, r);
    if (i < 0) break;
    const Book &b = g_books[i];
    const int ry = y + r * NROW_H;
    const String right = b.seq ? String(b.pct) + "%" : TR("New", "Mới");
    const int rw = ui_text(F_R25, right, 440, ry + 13, 33, AL_RIGHT);
    ui_text(F_X25, ui_fit(F_X25, b.title, 400 - rw - 16), 40, ry + 13, 33);
    ui_text(F_R25, ui_fit(F_R25, b.author, 400), 40, ry + 50, 33);
    display.fillRect(40, ry + NROW_H - 1, 400, 1, INK);
  }
  if (lib_pages(g_books) > 1) ui_page_counter(String(g_libPage + 1) + "/" + lib_pages(g_books));
}

inline int library_hit(int x, int y) {  // index into g_books under (x, y), or -1
  if (x < 40 || x >= 440 || g_books.empty()) return -1;
  const bool card = g_libPage == 0 && lib_continue(g_books);
  if (card && y >= 128 && y < 300) return 0;
  const int top = card ? 316 : NROW_TOP;
  return y < top ? -1 : lib_index(g_books, g_libPage, (y - top) / NROW_H);
}

// ---------- 07b no card / 07c loading / 07g opening / 07h can't open ----------
inline void draw_reader_nosd() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Library", "Thư viện"), "");
  ui_icon(ICON_SD64, 208, 301);
  ui_text(F_X30, TR("No microSD card", "Không có thẻ SD"), W / 2, 381, 40, AL_CENTER);
  ui_wrap(F_R25, TR("Insert a FAT32 card, then tap Retry.", "Lắp thẻ FAT32, rồi chạm Thử lại."), 40, 437, 400, 33, 2, AL_CENTER);
  ui_button(40, 712, 400, 72, true, &ICON_RETRY30, TR("Retry", "Thử lại"), 10);
}

inline void draw_reader_wait(const String &title, const String &what) {  // 07c Loading… / 07g Opening…, shown at once
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(ui_fit(F_X32, title, 400), "");
  ui_icon(ICON_SPIN64, 208, 342);
  ui_text(F_X30, what, W / 2, 422, 40, AL_CENTER);
  ui_partial(0, 0, W, H);
}

inline void draw_book_error() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Library", "Thư viện"), "");
  ui_icon(ICON_APP_BOOK, 210, 303);
  ui_text(F_X30, TR("Can’t open this book", "Không mở được sách này"), W / 2, 381, 40, AL_CENTER);
  ui_wrap(F_R25, TR("The file may be damaged or protected (DRM).", "Tệp có thể bị hỏng hoặc được bảo vệ (DRM)."), 40, 437, 400, 33, 3, AL_CENTER);
  ui_button(40, 712, 400, 72, true, nullptr, TR("Library", "Thư viện"));
}

// ---------- 07d book page / 07e reader menu ----------
constexpr int MENU_Y = 430, MENU_ROW0 = MENU_Y + 100, SIZE_S_X = 296, SIZE_M_X = 376, SIZE_W = 64;

inline void draw_size_button(int x, int y, const char *label, bool on) {
  display.fillRoundRect(x, y, SIZE_W, 48, 10, INK);
  if (!on) display.fillRoundRect(x + 3, y + 3, SIZE_W - 6, 42, 7, PAPER);
  ui_text(F_X25, label, x + SIZE_W / 2, y + 7, 33, AL_CENTER, on ? PAPER : INK, on ? INK : PAPER);
}

inline void draw_book() {
  rd::draw();  // the whole screen: the page, white elsewhere
  ui_status_bar(ui_battery_pct());
  if (g_menu) {
    display.fillRect(0, MENU_Y, W, H - MENU_Y, PAPER);
    display.fillRect(0, MENU_Y, W, 3, INK);
    ui_text(F_X25, ui_fit(F_X25, rd::title(), 400), 40, MENU_Y + 22, 33);
    ui_text(F_R20, ui_fit(F_R20, rd::chapter_title() + " · " + (rd::page() + 1) + "/" + rd::pages() + " · " + rd::percent() + "%", 400),
            40, MENU_Y + 58, 26);
    const int toc = rd::toc_current();
    info_row(MENU_ROW0, 72, F_B28, TR("Contents", "Mục lục"), toc >= 0 ? String(toc + 1) + "/" + rd::toc_count() : "", true);
    ui_text(F_B28, TR("Text size", "Cỡ chữ"), 40, MENU_ROW0 + 72 + 17, 37);
    draw_size_button(SIZE_S_X, MENU_ROW0 + 72 + 12, "S", !reader_large());
    draw_size_button(SIZE_M_X, MENU_ROW0 + 72 + 12, "M", reader_large());
    display.fillRect(40, MENU_ROW0 + 143, 400, 1, INK);
    info_row(MENU_ROW0 + 144, 72, F_B28, TR("Library", "Thư viện"), "", true);
    return;
  }
  const int pw = ui_text(F_R20, String(rd::page() + 1) + "/" + rd::pages(), W / 2, 758, 26, AL_CENTER);
  const int pct = ui_text(F_R20, String(rd::percent()) + "%", 440, 758, 26, AL_RIGHT);
  (void)pct;
  ui_text(F_R20, ui_fit(F_R20, rd::chapter_title(), W / 2 - pw / 2 - 40 - 12), 40, 758, 26);
}

// ---------- 07f Contents ----------
inline int toc_pages() { return max(1, (rd::toc_count() + TOC_ROWS - 1) / TOC_ROWS); }

inline void draw_contents() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Contents", "Mục lục"), String(rd::toc_count()));
  g_tocPage = constrain(g_tocPage, 0, toc_pages() - 1);
  const int cur = rd::toc_current();
  for (int r = 0; r < TOC_ROWS; r++) {
    const int i = g_tocPage * TOC_ROWS + r, y = TOC_TOP + r * TOC_H;
    if (i >= rd::toc_count()) break;
    const bool on = i == cur;
    if (on) display.fillRect(40, y, 400, TOC_H, INK);
    else display.fillRect(40, y + TOC_H - 1, 400, 1, INK);
    ui_text(on ? F_X25 : F_R25, ui_fit(on ? F_X25 : F_R25, rd::toc_title(i), 376), 52, y + 15, 33, AL_LEFT, on ? PAPER : INK, on ? INK : PAPER);
  }
  if (toc_pages() > 1) ui_page_counter(String(g_tocPage + 1) + "/" + toc_pages());
}

inline int contents_hit(int x, int y) {  // TOC index under (x, y), or -1
  if (x < 40 || x >= 440 || y < TOC_TOP || y >= TOC_TOP + TOC_ROWS * TOC_H) return -1;
  const int i = g_tocPage * TOC_ROWS + (y - TOC_TOP) / TOC_H;
  return i < rd::toc_count() ? i : -1;
}
