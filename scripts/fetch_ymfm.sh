#!/bin/bash
# ymfm as MAME 0.289 carries it (3rdparty/ymfm), into extern/ymfm.
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -d "$ROOT/extern/ymfm/src" ]; then echo "extern/ymfm present"; exit 0; fi
TMP="$(mktemp -d)"
curl -L -o "$TMP/mame.tar.gz" https://github.com/mamedev/mame/archive/refs/tags/mame0289.tar.gz
tar -xzf "$TMP/mame.tar.gz" -C "$TMP" mame-mame0289/3rdparty/ymfm
mkdir -p "$ROOT/extern"
mv "$TMP/mame-mame0289/3rdparty/ymfm" "$ROOT/extern/ymfm"
rm -rf "$TMP"
