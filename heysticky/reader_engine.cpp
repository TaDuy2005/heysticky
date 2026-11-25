// Reader engine glue: opens a book with CrossPoint's Epub (EPUB, TXT and MD), lays chapters out with its
// Section cache and draws pages with its GfxRenderer into a 1-bit buffer, then copies that to Seeed_GFX.
#include "reader_engine.h"
#include "reader.h"  // html_opening_title

#include <CrossPoint.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <Epub/blocks/ImageBlock.h>
#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <SD.h>
#include <Seeed_GFX.h>
#include <Txt/Txt.h>
#include <builtinFonts/jetbrainsmono_12_bold.h>
#include <builtinFonts/jetbrainsmono_12_italic.h>
#include <builtinFonts/jetbrainsmono_12_regular.h>
#include <builtinFonts/jetbrainsmono_14_bold.h>
#include <builtinFonts/jetbrainsmono_14_italic.h>
#include <builtinFonts/jetbrainsmono_14_regular.h>

#include <memory>
#include <vector>

extern Seeed_GFX display;

namespace rd {
namespace {

HalDisplay hal;
GfxRenderer renderer(hal);
FontDecompressor decompressor;
FontCacheManager fontCache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());

// The UI's JetBrains Mono, made with CrossPoint's fontconvert.py (third_party/crosspoint/README.md). No bold italic in
// fonts/: bold stands in. New font IDs, so chapters cached with the old font are laid out again (Section checks the ID).
EpdFont j12r(&jetbrainsmono_12_regular), j12b(&jetbrainsmono_12_bold), j12i(&jetbrainsmono_12_italic);
EpdFont j14r(&jetbrainsmono_14_regular), j14b(&jetbrainsmono_14_bold), j14i(&jetbrainsmono_14_italic);
EpdFontFamily mono12(12, &j12r, &j12b, &j12i, &j12b);
EpdFontFamily mono14(14, &j14r, &j14b, &j14i, &j14b);
constexpr int MONO12_ID = 0x4A424D0C, MONO14_ID = 0x4A424D0E;  // "JBM" + pt

bool g_ready = false;
std::shared_ptr<Epub> g_epub;
std::unique_ptr<Section> g_section;
int g_spine = 0;
bool g_large = false;
bool g_image = false;  // the page last drawn has a picture

// 07f Contents: the book's own entries, plus one for each chapter file they skip (Gutenberg's Alice lists 6 entries
// and none of its 12 chapters), titled from the file's opening (reader.h html_opening_title).
struct TocRow {
  std::string title, anchor;
  int spine, own;  // own: index in the book's table of contents; -1 = filled in
};
std::vector<TocRow> g_toc;

// Opening of chapter file `href`: its first 8 KB is plenty for a title.
struct Head : Print {
  std::string s;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    const size_t k = std::min(n, size_t(8192) - std::min(s.size(), size_t(8192)));
    s.append(reinterpret_cast<const char *>(b), k);
    return k;  // short = stop reading
  }
};

// Titles for the skipped files, worked out once per book: <book cache>/toc_fill.tsv, "spine<TAB>title" per line.
std::vector<std::string> fill_titles(const std::vector<bool> &listed) {
  std::vector<std::string> fill(listed.size());
  if (Txt::isTxtOrMd(g_epub->getPath())) return fill;  // TXT / MD: Contents are the Markdown headings
  const String path = String(g_epub->getCachePath().c_str()) + "/toc_fill.tsv";
  if (File f = SD.open(path)) {
    while (f.available()) {
      const String line = f.readStringUntil('\n');
      const int tab = line.indexOf('\t'), i = line.toInt();
      if (tab > 0 && i >= 0 && i < int(fill.size())) fill[i] = line.substring(tab + 1).c_str();
    }
    f.close();
    return fill;
  }
  String out;
  for (size_t i = 0; i < listed.size(); i++) {
    if (listed[i]) continue;
    Head h;
    g_epub->readItemContentsToStream(g_epub->getSpineItem(i).href, h, 1024, true);
    fill[i] = html_opening_title(h.s);
    out += String(int(i)) + "\t" + fill[i].c_str() + "\n";
  }
  if (File f = SD.open(path, FILE_WRITE)) {
    f.print(out);
    f.close();
  }
  return fill;
}

void build_toc() {
  g_toc.clear();
  const int n = g_epub->getTocItemsCount(), spines = g_epub->getSpineItemsCount();
  std::vector<BookMetadataCache::TocEntry> own;
  std::vector<bool> listed(spines);
  for (int i = 0; i < n; i++) {
    own.push_back(g_epub->getTocItem(i));
    if (own[i].spineIndex >= 0 && own[i].spineIndex < spines) listed[own[i].spineIndex] = true;
  }
  const std::vector<std::string> fill = fill_titles(listed);
  int next = 0;  // the first file not yet passed: skipped files go in before the entry after them
  auto fill_to = [&](int s) {
    for (; next < s; next++)
      if (!listed[next] && !fill[next].empty()) g_toc.push_back({fill[next], "", next, -1});
  };
  for (int i = 0; i < n; i++) {
    if (own[i].spineIndex >= 0) {
      fill_to(own[i].spineIndex);
      next = std::max(next, own[i].spineIndex + 1);
    }
    g_toc.push_back({own[i].title, own[i].anchor, own[i].spineIndex, i});
  }
  fill_to(spines);
}

void begin() {
  if (g_ready) return;
  hal.begin();
  renderer.begin();
  renderer.setOrientation(GfxRenderer::Portrait);
  decompressor.init();
  fontCache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&fontCache);
  renderer.insertFont(MONO12_ID, mono12);
  renderer.insertFont(MONO14_ID, mono14);
  g_ready = true;
}

int font_id() { return g_large ? MONO14_ID : MONO12_ID; }

ReaderRenderSpec spec() {
  ReaderRenderSpec s;
  s.fontId = font_id();
  s.lineCompression = 1.2f;  // CrossPoint's default line spacing (+20%)
  s.paragraphAlignment = 0;  // justified
  s.viewportWidth = PAGE_W;
  s.viewportHeight = PAGE_H;
  s.hyphenationEnabled = true;
  s.embeddedStyle = false;  // one look for every book: the books' own CSS mixes left / justify / right / center
  return s;
}

// Lay out (or load the cached layout of) chapter `spine`.
bool load_chapter(int spine) {
  if (g_section && g_spine == spine) return true;
  g_section.reset(new Section(g_epub, spine, renderer));
  const ReaderRenderSpec s = spec();
  if (!g_section->loadSectionFile(s) || g_section->isPartial()) {
    if (!g_section->createSectionFile(s)) {
      g_section.reset();
      return false;
    }
  }
  g_spine = spine;
  return true;
}

}  // namespace

bool open(const String &path) {
  begin();
  close();
  auto e = std::make_shared<Epub>(std::string(path.c_str()), "/.reader");
  if (!e->load(true, false)) return false;
  e->setupCacheDir();
  g_epub = std::move(e);
  build_toc();
  ImageBlock::setExtractor(g_epub.get(), [](void *ctx, const char *src, const char *dest) {
    return static_cast<Epub *>(ctx)->extractItemToFile(src, dest);
  });
  return true;
}

void close() {
  g_section.reset();
  g_epub.reset();
  g_toc.clear();
  g_spine = 0;
}

bool is_open() { return bool(g_epub); }
String title() { return g_epub ? String(g_epub->getTitle().c_str()) : String(); }
String author() { return g_epub ? String(g_epub->getAuthor().c_str()) : String(); }

bool show(int chapter, uint32_t offset, bool large) {
  if (!g_epub) return false;
  if (large != g_large) {
    g_large = large;
    g_section.reset();  // laid out for the other size
  }
  chapter = constrain(chapter, 0, max(0, g_epub->getSpineItemsCount() - 1));
  if (!load_chapter(chapter)) return false;
  const auto p = g_section->getPageForVisibleTextOffset(offset);
  g_section->currentPage = p ? *p : 0;
  return true;
}

int start_chapter() { return g_epub ? max(0, g_epub->getSpineIndexForTextReference()) : 0; }

bool next() {
  if (!g_section) return false;
  if (g_section->currentPage + 1 < g_section->pageCount) {
    g_section->currentPage++;
    return true;
  }
  if (g_spine + 1 >= g_epub->getSpineItemsCount()) return false;
  return show(g_spine + 1, 0, g_large);
}

bool prev() {
  if (!g_section) return false;
  if (g_section->currentPage > 0) {
    g_section->currentPage--;
    return true;
  }
  if (g_spine == 0) return false;
  if (!show(g_spine - 1, 0, g_large)) return false;
  g_section->currentPage = max(0, int(g_section->pageCount) - 1);
  return true;
}

bool to_toc(int toc, bool large) {
  if (!g_epub || toc < 0 || toc >= int(g_toc.size())) return false;
  const TocRow &t = g_toc[toc];
  if (t.spine < 0 || !show(t.spine, 0, large)) return false;
  if (!t.anchor.empty())
    if (const auto p = g_section->getPageForAnchor(t.anchor)) g_section->currentPage = *p;
  return true;
}

int chapter() { return g_spine; }
int chapters() { return g_epub ? g_epub->getSpineItemsCount() : 0; }
int page() { return g_section ? g_section->currentPage : 0; }
int pages() { return g_section ? max<int>(1, g_section->pageCount) : 1; }

uint32_t offset() {
  if (!g_section) return 0;
  const auto o = g_section->getVisibleTextOffsetForPage(g_section->currentPage);
  return o ? *o : 0;
}

int percent() {
  if (!g_epub) return 0;
  return constrain(int(g_epub->calculateProgress(g_spine, float(page() + 1) / pages()) * 100.0f + 0.5f), 0, 100);
}

int toc_count() { return int(g_toc.size()); }
String toc_title(int i) { return i >= 0 && i < int(g_toc.size()) ? String(g_toc[i].title.c_str()) : String(); }

int toc_current() {
  if (!g_section) return -1;
  for (size_t r = 0; r < g_toc.size(); r++)  // a filled-in file: the book's own list has nothing for it
    if (g_toc[r].own < 0 && g_toc[r].spine == g_spine) return int(r);
  int t = g_section->getTocIndexForPage(g_section->currentPage);
  if (t < 0) t = g_epub->getTocIndexForSpineIndex(g_spine);
  for (size_t r = 0; t >= 0 && r < g_toc.size(); r++)
    if (g_toc[r].own == t) return int(r);
  return -1;
}

String chapter_title() {
  const int t = toc_current();
  return t >= 0 ? toc_title(t) : title();
}

void draw() {
  renderer.clearScreen();
  g_image = false;
  if (g_section) {
    if (auto page = g_section->loadPage(g_section->currentPage)) {
      g_image = page->hasImages();
      ImageBlock::clearRenderFailures();
      auto scope = fontCache.createPrewarmScope();  // first pass collects the glyphs, then decompresses them at once
      page->render(renderer, font_id(), PAGE_X, PAGE_Y);
      scope.endScanAndPrewarm();
      renderer.clearScreen();
      renderer.setDitherImagesInBw(true);
      page->render(renderer, font_id(), PAGE_X, PAGE_Y);
      renderer.setDitherImagesInBw(false);
      ImageBlock::releaseRenderCache();
    }
  }
  // Panel-native 800 x 480 -> the Sticky's 480 x 800 portrait: a clockwise quarter turn. 1 = paper.
  display.drawBitmapRotatedCW(0, 0, hal.getFrameBuffer(), HalDisplay::DISPLAY_WIDTH, HalDisplay::DISPLAY_HEIGHT,
                              TFT_WHITE, TFT_BLACK);
}

bool image_page() { return g_image; }

bool metadata(const String &path, String &t, String &a) {
  Epub e(std::string(path.c_str()), "/.reader");
  std::string st, sa;
  if (!e.loadMetadata(st, sa)) return false;
  t = st.c_str();
  a = sa.c_str();
  return true;
}

void forget(const String &path) {
  if (g_epub && g_epub->getPath() == path.c_str()) close();
  Epub(std::string(path.c_str()), "/.reader").clearCache();
}

}  // namespace rd
