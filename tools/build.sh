#!/bin/sh
# Compile (and optionally upload) for the Sticky's 32 MB flash. The XIAO_ESP32S3 board menu stops at 8 MB and
# sketch.yaml can't set build properties, so the 32 MB table is chosen here (PLAN §2). OTA can't change it later.
#   tools/build.sh                     compile
#   tools/build.sh -u -p <port>        compile + upload
#   tools/build.sh -e                  compile + export .bin files to heysticky/build/
cd "$(dirname "$0")/../heysticky" || exit 1
exec arduino-cli compile --profile heysticky \
  --build-property build.flash_size=32MB \
  --build-property build.partitions=default_32MB \
  --build-property upload.maximum_size=13107200 \
  "$@" .
