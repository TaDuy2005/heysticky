// Reader engine: CrossPoint Reader's EPUB / TXT / MD layout (third_party/crosspoint) behind a small API.
// Kept in its own .cpp so CrossPoint's names never meet HeySticky's single translation unit.
// A place in a book is (chapter = spine index, offset = visible text offset): it survives a text-size change.
#pragma once
#include <Arduino.h>

namespace rd {

constexpr int PAGE_X = 40, PAGE_Y = 56, PAGE_W = 400, PAGE_H = 690;  // Figma 07d page box

bool open(const String &path);  // .epub / .txt / .md; builds the book cache on first open. false = can't open (07h)
void close();
bool is_open();
String title();
String author();

// Lay out `chapter` (cached on the card per text size) and go to the page holding `offset`.
// large: Noto Sans 14 pt (M) instead of 12 pt (S). Slow the first time a chapter is laid out.
bool show(int chapter, uint32_t offset, bool large);
int start_chapter();  // where a book never opened starts: its first text (EPUB guide), not the cover
bool next();  // false at the end of the book
bool prev();  // false at the start
bool to_toc(int toc, bool large);  // Contents: jump to a chapter's start

int chapter();
int chapters();
int page();   // 0-based, in this chapter
int pages();  // in this chapter
uint32_t offset();  // the current page's first visible character
int percent();      // of the whole book
String chapter_title();  // current TOC entry, or the book title
int toc_count();
String toc_title(int i);
int toc_current();

void draw();  // render the current page into the whole 480 x 800 screen; HeySticky draws the status bar and footer on top
bool metadata(const String &path, String &title, String &author);  // for the library, without laying out
void forget(const String &path);  // drop the book's layout cache (/.reader/epub_<hash>): deleted or replaced

}  // namespace rd
