# CrossPoint Reader engine (vendored)

The Reader app lays out EPUB, TXT and MD books with the engine of
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) (MIT, `LICENSE`),
copied from commit `3331d18967878ce208ad0cf321a856039db6748e` (2026-11-19).
Third-party notices for the code and data inside are in `licenses/` and `src/freeink/LICENSE`.

Built as one Arduino library (`library.properties`); `tools/build.sh` puts every folder under `src/`
on the include path, because CrossPoint includes its headers by bare name. HeySticky calls it only
from `heysticky/reader_engine.cpp`.

## What is here

`src/` holds these CrossPoint `lib/` folders: AsciiText, Epub, EpdFont, GfxRenderer, Txt, ZipFile,
miniz, uzlib, InflateReader, expat, XmlParserUtils, Utf8, Memory, Serialization, FsHelpers, MiniBidi,
OtShaper, JpegToBmpConverter, PngToBmpConverter, GifToBmpConverter, GifSupport.
`src/freeink/` holds files from CrossPoint's freeink-sdk: MemoryManager, FontAlloc, FontPsram,
GrayscaleCapabilities and the ContentProtection interface.

## What HeySticky changed

- `src/shim/` replaces CrossPoint's hardware layer: `HalStorage` on the Arduino `SD` library,
  `HalDisplay` as a plain 1-bit framebuffer (no grayscale), `BoardConfig`, `HalGPIO`, `Logging`
  (compiled out) and a ContentProtection stub (no DRM: protected books do not open). `HalMemory.*`
  and `fontIds.h` are CrossPoint's, moved here.
- Fonts: only Noto Sans 12 pt and 14 pt (regular, bold, italic, bold italic) are built in. Card fonts
  (`.cpfont` version 4, from CrossPoint's SD-card font builder) are loaded with `SdCardFontManager`
  (`heysticky/reader_engine.cpp`), one family at the reader size, with Noto Sans filling glyphs they lack.
- `EpdFont/VectorFontSupport.h`: TTF/OTF rendering always off (FreeType is not vendored).
- Hyphenation: Russian, Swedish and Ukrainian patterns (LPPL) removed from `LanguageRegistry.cpp`.
- Removed: `Epub/ContentProtection.cpp`, `Epub/BookKey.*`, DeviceSecret, TrustedTime (DRM).
- `miniz/third_party/miniz.c` renamed `miniz.c.inc` so Arduino does not compile it twice.
- Markdown: `Txt/MarkdownLite.h` (new) renders `.md` books (headings, emphasis, lists, links, code);
  `Txt/TxtToHtml.cpp` streams `.md` through it (`MD_CACHE_VERSION` 2) and `Txt/Txt.cpp` lists its headings
  as the table of contents.
- `Epub/Epub/blocks/TextBlock.cpp` includes `<fontIds.h>`; the image converters read the panel size
  from `HalDisplay` constants instead of a global `display`.
- `Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`: each row of a table nested in a table cell starts a new
  line instead of running into one paragraph; `Epub/Epub/Section.cpp` `SECTION_FILE_VERSION` 77 so cached
  chapters are laid out again.
