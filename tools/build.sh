#!/bin/sh
# Compile (and optionally upload) for the Sticky's 32 MB flash. The XIAO_ESP32S3 board menu stops at 8 MB and
# sketch.yaml can't set build properties, so the 32 MB flash is set here. The partition table is heysticky/partitions.csv
# (two 8 MB app slots below 16 MB); OTA can't change it later.
# -ffile-prefix-map keeps the builder's home folder out of the .bin (source paths in asserts and logs).
#   tools/build.sh                     compile
#   tools/build.sh -u -p <port>        compile + upload
#   tools/build.sh -e                  compile + export .bin files to heysticky/build/
cd "$(dirname "$0")/../heysticky" || exit 1
# The Reader's CrossPoint engine (third_party/crosspoint) includes its headers by bare name: every folder is on the path.
CP_INC=$(find ../third_party/crosspoint/src -name '*.h' -exec dirname {} \; | sort -u | sed "s|^\.\.|-I$(cd .. && pwd)|" | tr '\n' ' ')
CP_DEF="-DXML_GE=0 -DXML_CONTEXT_BYTES=0 -DMAX_WIDTH=3072 -DPNG_MAX_BUFFERED_PIXELS=16416"
exec arduino-cli compile --profile heysticky \
  --build-property build.flash_size=32MB \
  --build-property upload.maximum_size=8323072 \
  --build-property "compiler.c.extra_flags=-ffile-prefix-map=$HOME=~ $CP_DEF $CP_INC" \
  --build-property "compiler.cpp.extra_flags=-ffile-prefix-map=$HOME=~ $CP_DEF $CP_INC" \
  "$@" .
