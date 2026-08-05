#!/usr/bin/env bash
# Regenerate the JetBrains Mono bitmap fonts in src/ui/fonts/.
#
# Needs: brew install freetype
# Uses Adafruit's fontconvert, fetched and built into a temp dir.
#
# The 32..255 codepoint range is not optional. fontconvert defaults to 32..126,
# which would render "Björk" as "Bj?rk" — the exact bug the Unicode fonts were
# adopted to fix.
#
# fontconvert rasterises at 141 DPI, so pixel em size is roughly pt * 1.96.

set -euo pipefail

FONT_DIR="${FONT_DIR:-$HOME/Library/Fonts}"
OUT="$(cd "$(dirname "$0")/.." && pwd)/src/ui/fonts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

FT="$(brew --prefix freetype)"

curl -fsSL -o "$TMP/fontconvert.c" \
  https://raw.githubusercontent.com/adafruit/Adafruit-GFX-Library/master/fontconvert/fontconvert.c
curl -fsSL -o "$TMP/gfxfont.h" \
  https://raw.githubusercontent.com/adafruit/Adafruit-GFX-Library/master/gfxfont.h
mkdir -p "$TMP/fontconvert"
mv "$TMP/fontconvert.c" "$TMP/fontconvert/"

cc "$TMP/fontconvert/fontconvert.c" -o "$TMP/fc" \
  -I"$FT/include/freetype2" -L"$FT/lib" -lfreetype

gen() { # face pt outfile
  "$TMP/fc" "$FONT_DIR/JetBrainsMonoNerdFont-$1.ttf" "$2" 32 255 > "$OUT/$3"
  echo "$3: $(wc -c < "$OUT/$3") bytes"
}

mkdir -p "$OUT"
gen Bold    8 JBMonoTitle.h
gen Regular 7 JBMonoArtist.h
gen Regular 6 JBMonoSmall.h

echo
echo "Symbols now defined (update src/ui/fonts/JBMono.h and Theme.cpp if these changed):"
grep -hoE 'const GFXfont [A-Za-z0-9_]+' "$OUT"/JBMono{Title,Artist,Small}.h
