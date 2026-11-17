#!/bin/sh
# Release package for the version in heysticky/settings.h (FW_VERSION), into release/vX.Y.Z/:
#   bootloader.bin 0x0, partition-table.bin 0x8000, boot_app0.bin 0xE000, heysticky.bin 0x10000  (first install)
#   manifest.json   sizes + SHA-256 of those four (Seeed Playground registry / esp-web-tools format)
#   heysticky-app.bin  the app alone, the GitHub Release asset that OTA downloads (= heysticky.bin)
# A merged image is not shipped: padded to the 32 MB flash it is too big to publish.
#   tools/release.sh            build + package
#   tools/release.sh v1.0.1     same, but stop if the tag does not match FW_VERSION
set -e
cd "$(dirname "$0")/.."
V=$(sed -n 's/.*FW_VERSION = "\(.*\)";.*/\1/p' heysticky/settings.h)
[ -n "$V" ] || { echo "FW_VERSION not found in heysticky/settings.h"; exit 1; }
[ -z "$1" ] || [ "$1" = "v$V" ] || { echo "tag $1 does not match FW_VERSION v$V"; exit 1; }
tools/build.sh -e | grep "Sketch uses"
B=heysticky/build/esp32.esp32.XIAO_ESP32S3
OUT=release/v$V
rm -rf "$OUT" && mkdir -p "$OUT"
cp "$B/heysticky.ino.bootloader.bin" "$OUT/bootloader.bin"
cp "$B/heysticky.ino.partitions.bin" "$OUT/partition-table.bin"
cp "$B/boot_app0.bin" "$OUT/boot_app0.bin"
cp "$B/heysticky.ino.bin" "$OUT/heysticky.bin"
cp "$B/heysticky.ino.bin" "$OUT/heysticky-app.bin"
python3 - "$OUT" "$V" <<'EOF'
import hashlib, json, os, sys
out, ver = sys.argv[1], sys.argv[2]
parts = [("bootloader.bin", 0x0), ("partition-table.bin", 0x8000), ("boot_app0.bin", 0xE000), ("heysticky.bin", 0x10000)]
def part(p, off):
    data = open(os.path.join(out, p), "rb").read()
    return {"path": p, "offset": off, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
m = {"name": "HeySticky", "version": ver, "flashSize": "32MB", "flashMode": "dio", "flashFreq": "80m",
     "baudRate": 460800, "new_install_prompt_erase": True,
     "builds": [{"chipFamily": "ESP32-S3", "parts": [part(p, o) for p, o in parts]}]}
json.dump(m, open(os.path.join(out, "manifest.json"), "w"), indent=2)
EOF
ls -l "$OUT"
