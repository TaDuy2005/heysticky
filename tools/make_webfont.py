#!/usr/bin/env python3
"""JetBrains Mono Regular + Bold -> small WOFF files for the phone setup page, as a C header.

Run from the repo root:  python3 tools/make_webfont.py   (needs fontTools)
Writes heysticky/src/webfont.h. The hotspot has no internet, so net.h serves these at /font/r.woff and /font/b.woff.
Subset: ASCII, Latin-1, Vietnamese, and the punctuation / symbols the page uses. WOFF (zlib), since WOFF2 needs brotli.
"""
import io
from pathlib import Path
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "heysticky" / "src" / "webfont.h"
RANGES = [(0x20, 0x7E), (0xA0, 0xFF), (0x102, 0x103), (0x110, 0x111), (0x128, 0x129), (0x168, 0x169), (0x1A0, 0x1A1),
          (0x1AF, 0x1B0), (0x300, 0x303), (0x309, 0x309), (0x323, 0x323), (0x1EA0, 0x1EF9), (0x2010, 0x2027),
          (0x2039, 0x203A), (0x2190, 0x2193), (0x2212, 0x2212), (0x2713, 0x2715)]
UNICODES = [c for a, b in RANGES for c in range(a, b + 1)]


def woff(ttf):
    font = TTFont(ROOT / "fonts" / ttf)
    opts = subset.Options()
    opts.flavor = "woff"
    opts.layout_features = ["*"]
    opts.name_IDs = ["*"]  # keeps the OFL notice in the file
    s = subset.Subsetter(opts)
    s.populate(unicodes=UNICODES)
    s.subset(font)
    buf = io.BytesIO()
    font.flavor = "woff"
    font.save(buf)
    return buf.getvalue()


def array(name, data):
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    return f"inline const uint8_t {name}[] PROGMEM = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def main():
    r, b = woff("JetBrainsMono-Regular.ttf"), woff("JetBrainsMono-Bold.ttf")
    OUT.write_text("// Made by tools/make_webfont.py from fonts/ (JetBrains Mono, SIL OFL 1.1). Do not edit.\n"
                   "#pragma once\n#include <Arduino.h>\n\n" + array("WEBFONT_R", r) + array("WEBFONT_B", b))
    print(f"{OUT.relative_to(ROOT)}: Regular {len(r)} B, Bold {len(b)} B")


if __name__ == "__main__":
    main()
