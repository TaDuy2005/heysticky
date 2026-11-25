// Reader engine glue: opens a book with CrossPoint's Epub (EPUB, TXT and MD), lays chapters out with its
// Section cache and draws pages with its GfxRenderer into a 1-bit buffer, then copies that to Seeed_GFX.
#include "reader_engine.h"

#include <CrossPoint.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <Epub/blocks/ImageBlock.h>
#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <Seeed_GFX.h>
#include <builtinFonts/notosans_12_bold.h>
#include <builtinFonts/notosans_12_bolditalic.h>
#include <builtinFonts/notosans_12_italic.h>
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_bolditalic.h>
#include <builtinFonts/notosans_14_italic.h>
#include <builtinFonts/notosans_14_regular.h>
#include <fontIds.h>

#include <memory>

extern Seeed_GFX display;

namespace rd {
namespace {

HalDisplay hal;
GfxRenderer renderer(hal);
FontDecompressor decompressor;
FontCacheManager fontCache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());

EpdFont n12r(&notosans_12_regular), n12b(&notosans_12_bold), n12i(&notosans_12_italic), n12bi(&notosans_12_bolditalic);
EpdFont n14r(&notosans_14_regular), n14b(&notosans_14_bold), n14i(&notosans_14_italic), n14bi(&notosans_14_bolditalic);
EpdFontFamily noto12(12, &n12r, &n12b, &n12i, &n12bi);
EpdFontFamily noto14(14, &n14r, &n14b, &n14i, &n14bi);

bool g_ready = false;
std::shared_ptr<Epub> g_epub;
std::unique_ptr<Section> g_section;
int g_spine = 0;
bool g_large = false;

void begin() {
  if (g_ready) return;
  hal.begin();
  renderer.begin();
  renderer.setOrientation(GfxRenderer::Portrait);
  decompressor.init();
  fontCache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&fontCache);
  renderer.insertFont(NOTOSANS_12_FONT_ID, noto12);
  renderer.insertFont(NOTOSANS_14_FONT_ID, noto14);
  g_ready = true;
}

int font_id() { return g_large ? NOTOSANS_14_FONT_ID : NOTOSANS_12_FONT_ID; }

ReaderRenderSpec spec() {
  ReaderRenderSpec s;
  s.fontId = font_id();
  s.lineCompression = 1.2f;  // CrossPoint's default line spacing (+20%)
  s.paragraphAlignment = 0;  // justified
  s.viewportWidth = PAGE_W;
  s.viewportHeight = PAGE_H;
  s.hyphenationEnabled = true;
  s.embeddedStyle = true;
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
  ImageBlock::setExtractor(g_epub.get(), [](void *ctx, const char *src, const char *dest) {
    return static_cast<Epub *>(ctx)->extractItemToFile(src, dest);
  });
  return true;
}

void close() {
  g_section.reset();
  g_epub.reset();
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
  if (!g_epub || toc < 0 || toc >= g_epub->getTocItemsCount()) return false;
  const auto t = g_epub->getTocItem(toc);
  if (t.spineIndex < 0 || !show(t.spineIndex, 0, large)) return false;
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

int toc_count() { return g_epub ? g_epub->getTocItemsCount() : 0; }
String toc_title(int i) { return g_epub ? String(g_epub->getTocItem(i).title.c_str()) : String(); }

int toc_current() {
  if (!g_section) return -1;
  const int t = g_section->getTocIndexForPage(g_section->currentPage);
  return t >= 0 ? t : g_epub->getTocIndexForSpineIndex(g_spine);
}

String chapter_title() {
  const int t = toc_current();
  return t >= 0 ? toc_title(t) : title();
}

void draw() {
  renderer.clearScreen();
  if (g_section) {
    if (auto page = g_section->loadPage(g_section->currentPage)) {
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

bool metadata(const String &path, String &t, String &a) {
  Epub e(std::string(path.c_str()), "/.reader");
  std::string st, sa;
  if (!e.loadMetadata(st, sa)) return false;
  t = st.c_str();
  a = sa.c_str();
  return true;
}

}  // namespace rd
