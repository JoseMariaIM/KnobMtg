#!/bin/bash
# Font generation script for Knobby MTG Life Counter
# Requires lv_font_conv on PATH (install via: npm install -g lv_font_conv)
#
# Usage:  ./generate_fonts.sh
# Re-run after changing sizes, weights, or character ranges below.

set -euo pipefail
cd "$(dirname "$0")"

CONV=lv_font_conv
BPP=4
FORMAT=lvgl
LV_INCLUDE="lvgl.h"

# Toggle compression: "true" = smaller flash, more CPU per glyph render
# "false" = larger flash, zero decompression overhead
COMPRESS=false

# Font source files
BOLD=Montserrat-Bold.ttf
REGULAR=Montserrat-Regular.ttf

# LVGL's icon glyphs (LV_SYMBOL_*: backspace, checkmark, arrows, eye,
# battery, etc.) live in this FontAwesome subset, merged into LVGL's own
# bundled fonts the same way at build time. It ships with the LVGL
# library itself; point this at your install if it's not in the same
# place (e.g. a different Arduino sketchbook location).
FA_FONT="${FA_FONT:-$HOME/Arduino/libraries/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff}"

# ---------- Character ranges ----------
# Digits + signs for life total / dice (space, +, -, 0-9, =)
RANGE_DIGITS="0x20,0x2B,0x2D,0x30-0x39,0x3D"

# Full printable ASCII (for general UI text)
RANGE_ASCII="0x20-0x7F"

# ASCII + Latin-1 Supplement (adds the accented letters and punctuation
# Spanish needs: á é í ó ú ñ Á É Í Ó Ú Ñ ¿ ¡). Used for the general UI
# text fonts below so translated strings render correctly - LVGL's own
# bundled lv_font_montserrat_* only cover plain ASCII.
RANGE_LATIN1="0x20-0x7E,0xA1,0xBF,0xC0-0xFF"

# The exact FontAwesome codepoints LVGL's own bundled montserrat fonts
# include (copied from the "Opts:" comment at the top of e.g.
# lv_font_montserrat_14.c) - covers every LV_SYMBOL_* this app or LVGL's
# widgets (lv_keyboard's backspace/enter/mode keys, etc.) reference.
RANGE_SYMBOLS="61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

# ---------- Compression flag ----------
COMPRESS_FLAG=""
if [ "$COMPRESS" = "true" ]; then
    COMPRESS_FLAG="--no-compress false"
else
    COMPRESS_FLAG="--no-compress"
fi

# ---------- Font definitions ----------
# Each entry: output_name font_file weight size range
generate_font() {
    local name="$1"
    local font="$2"
    local size="$3"
    local range="$4"
    local outfile="${name}.c"

    echo "Generating ${outfile} (size=${size}, bpp=${BPP})..."
    $CONV \
        --font "$font" \
        --bpp "$BPP" \
        --size "$size" \
        --range "$range" \
        --format "$FORMAT" \
        $COMPRESS_FLAG \
        --lv-include "$LV_INCLUDE" \
        -o "$outfile"
}

# Same as generate_font(), but also merges in LVGL's FontAwesome icon
# glyphs (RANGE_SYMBOLS) from FA_FONT - needed for any font used as
# LV_FONT_DEFAULT or set on lv_keyboard, since those render LV_SYMBOL_*
# icons (backspace, enter, arrows...) using whatever font is active.
generate_font_with_symbols() {
    local name="$1"
    local font="$2"
    local size="$3"
    local range="$4"
    local outfile="${name}.c"

    echo "Generating ${outfile} (size=${size}, bpp=${BPP}, +symbols)..."
    $CONV \
        --font "$font" \
        --range "$range" \
        --font "$FA_FONT" \
        --range "$RANGE_SYMBOLS" \
        --bpp "$BPP" \
        --size "$size" \
        --format "$FORMAT" \
        $COMPRESS_FLAG \
        --lv-include "$LV_INCLUDE" \
        -o "$outfile"
}

# ---------- Generate fonts ----------

# Large bold font for life total and dice result
generate_font "lv_font_montserrat_bold_116" "$BOLD" 116 "$RANGE_DIGITS"

# Regular font for life preview total ("= xxx")
generate_font "lv_font_montserrat_regular_48" "$REGULAR" 48 "$RANGE_DIGITS"

# Bold fonts for multiplayer life totals (56 for absolute/tabletop, 44 for centric)
generate_font "lv_font_montserrat_bold_56" "$BOLD" 56 "$RANGE_DIGITS"
generate_font "lv_font_montserrat_bold_44" "$BOLD" 44 "$RANGE_DIGITS"

# General UI text fonts (regular weight, matching LVGL's own bundled
# montserrat sizes visually) but with accented-letter coverage. Named
# lv_font_es_* rather than lv_font_montserrat_* to avoid colliding with
# the LV_FONT_MONTSERRAT_* macros LVGL's own bundled fonts use - see
# LV_FONT_CUSTOM_DECLARE in lv_conf.h for where these get declared, and
# grep the app sources for lv_font_es_ to see where they're used.
generate_font_with_symbols "lv_font_es_14" "$REGULAR" 14 "$RANGE_LATIN1"
generate_font_with_symbols "lv_font_es_16" "$REGULAR" 16 "$RANGE_LATIN1"
generate_font_with_symbols "lv_font_es_22" "$REGULAR" 22 "$RANGE_LATIN1"
generate_font_with_symbols "lv_font_es_32" "$REGULAR" 32 "$RANGE_LATIN1"

echo ""
echo "Done! Generated fonts:"
ls -lh *.c 2>/dev/null || echo "(no .c files found)"
