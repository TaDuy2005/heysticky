// Reader screens, Figma 07-07j: Library (07 / 07a empty / 07b no card / 07c loading), book page (07d) with the
// reader menu sheet (07e), Contents (07f), Opening (07g), Can't open (07h), Reader settings (07i), Font (07j). Books are /books/*.epub|txt|md;
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

inline bool reader_large() { return cfg_get("rd_size") == "M"; }  // 07i Text size: S = 12 pt, M = 14 pt
inline String reader_font() { return cfg_get("rd_font"); }       // 07j: a card font family, "" = Noto Sans (built-in)

// Reader fonts on the card: /fonts/<Family>/<Family>_<size>.cpfont (and /.fonts/, which CrossPoint also reads).
struct ReaderFont {
  String name;
  std::vector<int> sizes;
};
inline std::vector<ReaderFont> rfont_scan() {
  std::vector<ReaderFont> v;
  if (!g_sdOk) return v;
  for (const char *root : {"/fonts", "/.fonts"}) {
    File d = SD.open(root);
    if (!d) continue;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      const String fam = f.name();
      if (f.isDirectory() && cpfont_family_ok(fam)) {
        ReaderFont rf{fam, {}};
        for (File e = f.openNextFile(); e; e = f.openNextFile()) {
          String ef;
          int sz;
          if (!e.isDirectory() && cpfont_name(e.name(), ef, sz) && ef == fam) rf.sizes.push_back(sz);
          e.close();
        }
        auto k = std::find_if(v.begin(), v.end(), [&](const ReaderFont &o) { return o.name == fam; });
        if (!rf.sizes.empty() && k == v.end()) v.push_back(rf);
      }
      f.close();
    }
    d.close();
  }
  for (ReaderFont &f : v) std::sort(f.sizes.begin(), f.sizes.end());
  std::sort(v.begin(), v.end(), [](const ReaderFont &a, const ReaderFont &b) { return a.name.compareTo(b.name) < 0; });
  return v;
}

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
      else {  // new, or replaced with a different file: its place and its old layout no longer fit
        if (k != known.end()) rd::forget(b.path);
        changed = true;
      }
      if (!b.title.length()) {
        if (!rd::metadata(b.path, b.title, b.author) || !b.title.length()) b.title = book_name(b.path);
        changed = true;
      }
      b.title = book_title_clean(b.title);  // also for titles saved before it was cut
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

// S / M (07i) and Aa (07 / 07a: opens Reader settings): 64 x 48 buttons, filled when on.
constexpr int SIZE_S_X = 296, SIZE_M_X = 376, SIZE_W = 64, AA_X = 376, AA_Y = 52;
inline void draw_size_button(int x, int y, const char *label, bool on) {
  display.fillRoundRect(x, y, SIZE_W, 48, 10, INK);
  if (!on) display.fillRoundRect(x + 3, y + 3, SIZE_W - 6, 42, 7, PAPER);
  ui_text(F_X25, label, x + SIZE_W / 2, y + 7, 33, AL_CENTER, on ? PAPER : INK, on ? INK : PAPER);
}
inline bool aa_hit(int x, int y) { return x >= AA_X - 16 && y >= 40 && y < 112; }

// ---------- 07 Library / 07a empty ----------
// Titles take up to two lines: the Continue card grows by a line, a row drops its author line.
inline std::vector<String> card_title(const Book &b) { return ui_lines(F_X30, b.title, 360, 2); }
inline int lib_rows_top() {  // first row's y on the current page
  if (g_libPage != 0 || !lib_continue(g_books)) return NROW_TOP;
  return 316 + (int(card_title(g_books[0]).size()) - 1) * 40;
}

inline void draw_library() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Library", "Thư viện"), "");
  draw_size_button(AA_X, AA_Y, "Aa", false);
  if (g_books.empty()) {
    ui_icon(ICON_APP_BOOK, 210, 303);
    ui_text(F_X30, TR("No books yet", "Chưa có sách"), W / 2, 381, 40, AL_CENTER);
    ui_wrap(F_R25, TR("Copy EPUB, TXT or MD files into /books/ on the card, or upload them on the phone page.",
                      "Chép tệp EPUB, TXT hoặc MD vào /books/ trên thẻ, hoặc tải lên trên trang điện thoại."),
            40, 437, 400, 33, 5, AL_CENTER);
    return;
  }
  g_libPage = constrain(g_libPage, 0, lib_pages(g_books) - 1);
  const int y = lib_rows_top();
  if (g_libPage == 0 && lib_continue(g_books)) {  // Continue card: the last book opened
    const Book &b = g_books[0];
    const std::vector<String> tl = card_title(b);
    const int dy = (int(tl.size()) - 1) * 40;
    ui_card(40, 128, 400, 172 + dy, 14, 3);
    ui_text(F_R20, TR("Continue", "Đọc tiếp"), 60, 144, 26);
    for (size_t k = 0; k < tl.size(); k++) ui_text(F_X30, tl[k], 60, 170 + k * 40, 40);
    ui_text(F_R25, ui_fit(F_R25, b.author, 360), 60, 212 + dy, 33);
    display.fillRoundRect(60, 260 + dy, 280, 16, 8, INK);
    display.fillRoundRect(62, 262 + dy, 276, 12, 6, PAPER);
    display.fillRect(62, 262 + dy, 276 * b.pct / 100, 12, INK);
    ui_text(F_R25, String(b.pct) + "%", 420, 251 + dy, 33, AL_RIGHT);
  }
  for (int r = 0;; r++) {
    const int i = lib_index(g_books, g_libPage, r);
    if (i < 0) break;
    const Book &b = g_books[i];
    const int ry = y + r * NROW_H;
    const String right = b.seq ? String(b.pct) + "%" : TR("New", "Mới");
    const int rw = ui_text(F_R25, right, 440, ry + 13, 33, AL_RIGHT);
    const std::vector<String> tl = ui_lines(F_X25, b.title, 400 - rw - 16, 2);
    for (size_t k = 0; k < tl.size(); k++) ui_text(F_X25, tl[k], 40, ry + 13 + k * 37, 33);
    if (tl.size() < 2) ui_text(F_R25, ui_fit(F_R25, b.author, 400), 40, ry + 50, 33);
    display.fillRect(40, ry + NROW_H - 1, 400, 1, INK);
  }
  if (lib_pages(g_books) > 1) ui_page_counter(String(g_libPage + 1) + "/" + lib_pages(g_books));
}

inline int library_hit(int x, int y) {  // index into g_books under (x, y), or -1
  if (x < 40 || x >= 440 || g_books.empty()) return -1;
  const bool card = g_libPage == 0 && lib_continue(g_books);
  const int top = lib_rows_top();
  if (card && y >= 128 && y < top - 16) return 0;
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
constexpr int MENU_Y = 430, MENU_ROW0 = MENU_Y + 100;

inline void draw_book() {
  rd::draw();  // the whole screen: the page, white elsewhere
  ui_status_bar(ui_battery_pct());
  if (g_menu) {
    display.fillRect(0, MENU_Y, W, H - MENU_Y, PAPER);
    display.fillRect(0, MENU_Y, W, 3, INK);
    const Book *b = open_book();
    ui_text(F_X25, ui_fit(F_X25, b ? b->title : rd::title(), 400), 40, MENU_Y + 22, 33);
    ui_text(F_R20, ui_fit(F_R20, rd::chapter_title() + " · " + (rd::page() + 1) + "/" + rd::pages() + " · " + rd::percent() + "%", 400),
            40, MENU_Y + 58, 26);
    const int toc = rd::toc_current();
    info_row(MENU_ROW0, 72, F_B28, TR("Contents", "Mục lục"), toc >= 0 ? String(toc + 1) + "/" + rd::toc_count() : "", true);
    info_row(MENU_ROW0 + 72, 72, F_B28, TR("Reader settings", "Cài đặt đọc"), "", true);
    info_row(MENU_ROW0 + 144, 72, F_B28, TR("Library", "Thư viện"), "", true);
    return;
  }
  // Pages count within the chapter (laying out the whole book for a book-wide count is too slow).
  // Chapter title and % are in the reader menu (07e).
  ui_text(F_R20, String(rd::page() + 1) + "/" + rd::pages(), W / 2, 758, 26, AL_CENTER);
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

// ---------- 07i Reader settings / 07j Font ----------
inline std::vector<ReaderFont> g_rdFonts;  // 07j rows after Noto Sans, read when 07j opens
inline int g_rdFontPage = 0;
constexpr int RDFONT_ROWS = 8;
inline int rdfont_pages() { return max(1, (int(g_rdFonts.size()) + 1 + RDFONT_ROWS - 1) / RDFONT_ROWS); }

inline void draw_reader_settings() {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Reader settings", "Cài đặt đọc"), "");
  info_row(SROW_TOP, SROW_H, F_B28, TR("Font", "Phông chữ"), reader_font().length() ? reader_font() : "Noto Sans", true);
  const int y = SROW_TOP + SROW_H;
  ui_text(F_B28, TR("Text size", "Cỡ chữ"), 40, y + 17, 37);
  draw_size_button(SIZE_S_X, y + 12, "S", !reader_large());
  draw_size_button(SIZE_M_X, y + 12, "M", reader_large());
  display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
}

// sel: the row just tapped (ticked at once). Row 0 = Noto Sans (built-in).
inline void draw_reader_fonts(int sel = -1) {
  display.fillScreen(PAPER);
  ui_status_bar(ui_battery_pct());
  ui_topbar(TR("Font", "Phông chữ"), "");
  if (sel < 0) {
    sel = 0;
    for (size_t i = 0; i < g_rdFonts.size(); i++)
      if (g_rdFonts[i].name == reader_font()) sel = int(i) + 1;
  }
  g_rdFontPage = constrain(g_rdFontPage, 0, rdfont_pages() - 1);
  for (int r = 0; r < RDFONT_ROWS; r++) {
    const int i = g_rdFontPage * RDFONT_ROWS + r, y = SROW_TOP + r * SROW_H;
    if (i > int(g_rdFonts.size())) break;
    const String name = i ? g_rdFonts[i - 1].name : TR("Noto Sans (built-in)", "Noto Sans (có sẵn)");
    ui_text(F_B28, ui_fit(F_B28, name, 352), 40, y + 17, 37);
    checkbox(404, y + 18, i == sel);
    display.fillRect(40, y + SROW_H - 1, 400, 1, INK);
  }
  if (rdfont_pages() > 1) ui_page_counter(String(g_rdFontPage + 1) + "/" + rdfont_pages());
  else ui_wrap(F_R20, TR("Add fonts on the phone page: Reader › Fonts.", "Thêm phông trên trang điện thoại: Đọc sách › Phông chữ."),
               40, 716, 400, 26, 2, AL_CENTER);
}

inline int rdfont_hit(int x, int y) {  // 07j row under (x, y): 0 = Noto Sans, n = g_rdFonts[n - 1]; -1 = none
  if (x < 40 || x >= 440 || y < SROW_TOP || y >= SROW_TOP + RDFONT_ROWS * SROW_H) return -1;
  const int i = g_rdFontPage * RDFONT_ROWS + (y - SROW_TOP) / SROW_H;
  return i <= int(g_rdFonts.size()) ? i : -1;
}
