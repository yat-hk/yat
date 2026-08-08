#!/usr/bin/env python3
"""Bitmap font generator for the YAT engine — spec §9.3/§9.7 real typography.

Rasterizes Noto Sans (Latin/symbols) and Noto Sans TC (CJK + CJK punctuation)
to 1-bit glyph bitmaps at the exact pixel sizes docs/PACK-SPEC.md mandates, and
emits them as C++ translation units under engine/src/ plus the public accessor
declarations in engine/include/yat/fonts.h.

Run:  tools/fonts/.venv/bin/python tools/fonts/gen_fonts.py
      (see README.md for venv setup; needs freetype-py)

The output is checked in — the engine build never runs this script. Re-run it
only when the charsets, sizes, or source fonts change, then re-verify goldens
(tools/preview/run-tests.sh) because every text pixel moves.

--- Design decisions, and why ---

RASTERIZATION.  FT_LOAD_TARGET_MONO with each font's own hinting, i.e. FreeType's
black-and-white rasterizer, NOT an antialiased render thresholded at 50%.
Compared side by side at 16/24/32px (tools/fonts/compare_modes.py) mono is the
clear winner: on dense ideographs (觀, 鐵, 礮) thresholding thins or breaks
strokes, and forcing the autohinter blobs them together, while the hinted mono
raster keeps every stroke separated. Latin stems also come out even instead of
alternating 1px/2px.

SIZE == EM.  The spec's "16px glyphs / 20px line, 24/30, 32/40, 48/60" is read as
em size = the stated px, which is both the universal meaning of "16px text" and
the only self-consistent reading: all four pairs are exactly 1.25 line-height,
the classic body-text ratio. (Cap height would put em at 22.4px inside a 20px
line — glyphs would collide.)

BASELINE.  ascent = px for every text face, i.e. the baseline sits `px` below the
top of the line box. This falls out of the ideographic em box: Noto Sans TC
ideographs span about [-0.12em, +0.88em], so centering that 1.0em block in a
1.25em line puts the baseline at 0.125em + 0.88em = 1.005em ≈ px. Verified below
(REPORT prints any glyph that overflows its line box): accented capitals and
Latin descenders both stay inside all four boxes.

BIGNUM.  §9.3 calls 96/128 "glyph heights", and for a digits-only face the glyph
height IS the digit height — so instead of em=96 (which would leave 68px digits
rattling around a 112px box) the em is solved by search so that the digits
measure exactly 96/128px tall. Baseline centers that digit block in the spec
line height.

BOLD.  Real Bold outlines wherever they earn their bytes:
  * Latin/symbols: Noto Sans Bold at every size (~100 glyphs, negligible).
  * CJK ideographs: Noto Sans TC wght=700 at 16/24/32 for the used-CJK set only.
    A cross-tab of every pack widget (size × weight × script) shows bold CJK is
    concentrated exactly there — large+bold+CJK is the single most common bold
    case (the typhoon/rainstorm warning packs), then medium+bold+CJK.
  * Everything else falls back to the regular glyph with a 1px horizontal smear
    (advance +1 so measure and draw stay consistent). The two xlarge bold
    widgets in the corpus are both Latin, and a 1px smear is 1/48 of the glyph
    at that size rather than 1/16.

COVERAGE / BUDGET.  The 48px face carries no CJK ideographs: no pack renders CJK
at xlarge (the only xlarge text widgets are a minibus route code and "Today!"),
and carrying the used-CJK set at 48px costs ~400KB. A hypothetical future
xlarge ideograph is served by the fallback chain in engine/src/fonts.cpp — the
24px glyph at integer 2x, which is blockier than a true 48px raster but keeps
proportional advances and therefore correct layout. Same for 32px misses (16px
at 2x). This is the ONLY remaining integer-scale path in the engine.
"""

import os
import sys
import collections

import freetype

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(HERE, "src")
OUT_SRC = os.path.join(ROOT, "engine", "src")
OUT_INC = os.path.join(ROOT, "engine", "include", "yat")

LATIN_REG = os.path.join(SRC, "NotoSans-Regular.ttf")
LATIN_BOLD = os.path.join(SRC, "NotoSans-Bold.ttf")
TC_VAR = os.path.join(SRC, "NotoSansTC[wght].ttf")

# ---------------------------------------------------------------- charsets

# Everything the Latin face owns: printable ASCII plus the accented letters and
# typographic marks a Hong Kong panel plausibly shows (° for temperatures is
# load-bearing; … is the engine's own wrap ellipsis).
ASCII = [chr(c) for c in range(0x20, 0x7F)]
LATIN_EXTRA = list(
    "£¥¢€©®™°±·×÷–—‘’“”„…‹›«»•‰≤≥≠≈→←↑↓↔§¶†‡"
    "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÑÒÓÔÕÖØÙÚÛÜÝß"
    "àáâãäåæçèéêëìíîïñòóôõöøùúûüýÿ"
    "ΣΔΩμ"
)

# CJK-side punctuation, fullwidth forms and the handful of geometric/symbol
# codepoints the repo actually uses. Rendered from Noto Sans TC so they carry
# CJK proportions and sit on the CJK baseline.
CJK_PUNCT = list("　、。〃〈〉《》「」『』【】〔〕〖〗〜・〇々")
FULLWIDTH = [chr(c) for c in range(0xFF01, 0xFF5F)] + [chr(0xFFE5)]
ENCLOSED = [chr(c) for c in range(0x2460, 0x2474)]  # ①..⑳
# CJK Compatibility Forms (U+FE30 ︰ vertical/presentation punctuation). Cheap,
# and real feeds do emit them where an author would have typed the plain form.
COMPAT_FORMS = [chr(c) for c in range(0xFE30, 0xFE50)]
MISC_SYM = list("●○■□▲▼◆★☆✓⚠※℃℉─│┌┐└┘├┤┬┴┼")

# Deliberately no thousands comma: a comma's tail descends ~0.13em, which at the
# bignum em (132/176px) is 17/23px below the baseline — more than the 8/10px the
# spec's line height leaves under a 96/128px digit block, and the engine has no
# clip rect, so it would bleed into the widget below. '/' sits on the baseline
# and fits. A pack that needs a grouped number wants `text`, not `bignum`.
BIGNUM_CHARS = list("0123456789.-+%°:/ ")

# Big5 Level 1 (0xA440..0xC67E) is, by construction, the common zh-Hant set —
# 5401 ideographs, frequency/stroke ordered by the standard itself. Decoded
# through Python's own big5hkscs codec so the set is reproducible with no
# network and no vendored list. HKSCS adds the Cantonese-only characters a HK
# device needs (嘅/啲/喺/嘢/冇...) which Big5 proper lacks; the ones the corpus
# uses arrive via CJK_USED below, and a hand list covers the rest of the
# everyday particles.
CANTO_PARTICLES = list("嘅啲喺咗嗰咁嘢冇睇攞諗冚嚟乜嘥啩嘞嗮咩喎唞揦嗌搵嘈掂啱靚")


def big5_level1():
    out = []
    for lead in range(0xA4, 0xC7):
        for trail in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
            try:
                ch = bytes([lead, trail]).decode("big5hkscs")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1 and 0x4E00 <= ord(ch) <= 0x9FFF:
                out.append(ch)
    return out


# Codepoints the repo actually renders: pack documents, the preview fixtures
# (whose text lands in the goldens), firmware UI strings, engine-emitted strings
# and the non-technical UX doc. A superset is harmless; a miss is a hole in a
# headline.
#
# The 17 real-world packs moved to the yat-hk/yat-packs community repo; this
# repo only keeps packs/internal (firmware-embedded) and
# packs/examples/render-test.yat-pack.json (engine-conformance). Scanning only
# those two would silently drop every ideograph the real pack corpus uses
# (district names, descriptions, ...) from the bold-16/24px and all-32px CJK
# faces — a missing-glyph regression on real devices, not just a doc gap — so
# pull in a sibling yat-packs checkout's official/ and community/ when present.
SCAN_ROOTS = [
    "packs/examples",
    "packs/internal",
    "tools/preview/fixtures",   # includes fixtures/packs — these land in the goldens
    "firmware/src",
    "engine/src",
    "docs/UX-NONTECH.md",
]
_YAT_PACKS = os.path.normpath(os.path.join(ROOT, "..", "yat-packs"))
if os.path.isdir(_YAT_PACKS):
    SCAN_ROOTS += [
        os.path.join(_YAT_PACKS, "official"),
        os.path.join(_YAT_PACKS, "community"),
    ]
else:
    print(f"  (warning) sibling checkout {_YAT_PACKS!r} not found — the CJK "
          "scan will miss every ideograph the 17 real-world packs use "
          "(district names, descriptions, ...), so this run's charset will "
          "be incomplete for real content. Clone github.com/yat-hk/yat-packs "
          "next to this repo before regenerating fonts for real.",
          file=sys.stderr)

# The website (yat-site, a separate private sibling repo) ships pack copies
# and mtr-stations.json under its own assets/ — station names and gallery
# pack text need the same CJK coverage as the packs above. This used to be
# "site/assets" back when the website lived inside this repo.
_YAT_SITE = os.path.normpath(os.path.join(ROOT, "..", "yat-site"))
if os.path.isdir(_YAT_SITE):
    SCAN_ROOTS.append(os.path.join(_YAT_SITE, "assets"))
else:
    print(f"  (warning) sibling checkout {_YAT_SITE!r} not found — the CJK "
          "scan will miss station names and gallery pack copies served by "
          "the website. Check out yat-site next to this repo before "
          "regenerating fonts for real.",
          file=sys.stderr)


def scan_used_cjk():
    seen = set()
    for rel in SCAN_ROOTS:
        p = os.path.join(ROOT, rel)
        paths = [p] if os.path.isfile(p) else [
            os.path.join(d, f) for d, _, fs in os.walk(p) for f in fs
        ]
        for path in paths:
            try:
                text = open(path, encoding="utf-8").read()
            except (UnicodeDecodeError, OSError):
                continue
            for ch in text:
                o = ord(ch)
                # CJK Extension A, the main block, and the Compatibility
                # Ideographs (U+FA08 行 and friends turn up in real HK feeds
                # where the unified form was meant).
                if 0x3400 <= o <= 0x4DBF or 0x4E00 <= o <= 0x9FFF or 0xF900 <= o <= 0xFAFF:
                    seen.add(ch)
    return seen


# ---------------------------------------------------------------- faces

# px -> (line height per §9.3, kind)
TEXT_SIZES = [(16, 20), (24, 30), (32, 40), (48, 60)]
BIGNUM_SIZES = [(96, 112), (128, 148)]

LATIN_SET = ASCII + LATIN_EXTRA
PUNCT_SET = CJK_PUNCT + FULLWIDTH + ENCLOSED + COMPAT_FORMS + MISC_SYM


def charsets(used_cjk):
    """(face key, px, lineH, bold, [(char, source)...]) for every emitted face."""
    common_cjk = sorted(set(big5_level1()) | set(CANTO_PARTICLES) | used_cjk)
    used_only = sorted(used_cjk | set(CANTO_PARTICLES))
    plans = []
    for px, lineH in TEXT_SIZES:
        for bold in (False, True):
            if px in (16, 24):
                cjk = common_cjk if not bold else used_only
            elif px == 32:
                cjk = used_only
            else:  # 48px carries no ideographs — see the module docstring
                cjk = []
            chars = [(c, "latin") for c in LATIN_SET]
            chars += [(c, "cjk") for c in PUNCT_SET]
            chars += [(c, "cjk") for c in cjk]
            plans.append((f"{px}{'b' if bold else 'r'}", px, lineH, bold, chars))
    for px, lineH in BIGNUM_SIZES:
        plans.append((f"{px}r", px, lineH, False, [(c, "latin") for c in BIGNUM_CHARS]))
    return plans


class Rasterizer:
    """One (weight, pixel size) pair of faces: Latin outlines + TC outlines."""

    def __init__(self, bold, px_latin, px_cjk):
        self.latin = freetype.Face(LATIN_BOLD if bold else LATIN_REG)
        self.latin.set_pixel_sizes(0, px_latin)
        self.cjk = freetype.Face(TC_VAR)
        self.cjk.set_var_design_coords([700 if bold else 400])
        self.cjk.set_pixel_sizes(0, px_cjk)

    def face_for(self, ch, source):
        primary, secondary = (
            (self.latin, self.cjk) if source == "latin" else (self.cjk, self.latin)
        )
        if primary.get_char_index(ch):
            return primary
        if secondary.get_char_index(ch):
            return secondary
        return None

    def glyph(self, ch, source):
        """-> (rows_of_bits, bw, bh, xoff, top, advance) or None when unmapped."""
        face = self.face_for(ch, source)
        if face is None:
            return None
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        g = face.glyph
        b = g.bitmap
        rows = []
        for r in range(b.rows):
            row = 0
            for c in range(b.width):
                byte = b.buffer[r * b.pitch + (c >> 3)]
                if byte & (0x80 >> (c & 7)):
                    row |= 1 << (b.width - 1 - c)
            rows.append(row)
        return rows, b.width, b.rows, g.bitmap_left, g.bitmap_top, g.advance.x >> 6


def solve_bignum_em(target_h):
    """Pixel size whose digit ink height is exactly `target_h` (see docstring)."""
    face = freetype.Face(LATIN_REG)

    def digit_height(px):
        face.set_pixel_sizes(0, px)
        h = 0
        for ch in "0123456789":
            face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
            h = max(h, face.glyph.bitmap.rows)
        return h

    lo, hi = target_h, target_h * 3
    best = target_h
    while lo <= hi:
        mid = (lo + hi) // 2
        h = digit_height(mid)
        if h <= target_h:
            best = mid
            lo = mid + 1
        else:
            hi = mid - 1
    return best, digit_height(best)


# ---------------------------------------------------------------- emit

Metrics = collections.namedtuple("Metrics", "bw bh stride advance xo yo")


def build_face(key, px, lineH, bold, chars):
    if px >= 96:  # bignum: em solved so the digits measure `px` tall
        em, digit_h = solve_bignum_em(px)
        ascent = (lineH + digit_h) // 2
        rast = Rasterizer(bold, em, em)
        note = f"em={em} (digits {digit_h}px)"
    else:
        em = px
        ascent = px  # see docstring: ideographic em box centered in a 1.25 line
        rast = Rasterizer(bold, px, px)
        note = f"em={em}"

    glyphs = {}
    missing = []
    for ch, source in chars:
        cp = ord(ch)
        if cp > 0xFFFF or cp in glyphs:
            continue
        g = rast.glyph(ch, source)
        if g is None:
            missing.append(ch)
            continue
        glyphs[cp] = g

    blob = bytearray()
    palette = {}
    metrics_list = []
    entries = []
    over_top = over_bot = 0
    for cp in sorted(glyphs):
        rows, bw, bh, xo, top, adv = glyphs[cp]
        stride = (bw + 7) // 8
        off = len(blob)
        for row in rows:
            packed = row << (stride * 8 - bw) if bw else 0
            blob += packed.to_bytes(stride, "big") if stride else b""
        if top > ascent:
            over_top = max(over_top, top - ascent)
        if bh - top > lineH - ascent:
            over_bot = max(over_bot, (bh - top) - (lineH - ascent))
        m = Metrics(bw, bh, stride, adv, xo, -top)
        idx = palette.get(m)
        if idx is None:
            idx = len(metrics_list)
            palette[m] = idx
            metrics_list.append(m)
        entries.append((cp, off, idx))

    return dict(
        key=key, px=px, em=em, lineH=lineH, ascent=ascent, bold=bold, note=note,
        entries=entries, metrics=metrics_list, blob=bytes(blob), missing=missing,
        over_top=over_top, over_bot=over_bot,
    )


def c_array(name, ctype, values, per_line=16, fmt=str):
    out = [f"const {ctype} {name}[] = {{"]
    for i in range(0, len(values), per_line):
        out.append("    " + ", ".join(fmt(v) for v in values[i:i + per_line]) + ",")
    out.append("};")
    return "\n".join(out)


HEADER_NOTE = """// GENERATED by tools/fonts/gen_fonts.py — do not edit by hand.
// Noto Sans (Latin/symbols) + Noto Sans TC (CJK), SIL Open Font License 1.1;
// license texts in tools/fonts/src/OFL-*.txt, attribution in
// THIRD_PARTY_NOTICES.md. Rasterized 1-bit via FreeType FT_LOAD_TARGET_MONO.
"""


def emit_face_tu(faces, path):
    parts = [HEADER_NOTE, '#include "fonts_data.h"', "", "namespace yat {",
             "namespace fontdata {", ""]
    for f in faces:
        k = f["key"]
        parts.append(f"// --- {f['px']}px {'bold' if f['bold'] else 'regular'}: "
                     f"{len(f['entries'])} glyphs, {len(f['blob'])} bitmap bytes, "
                     f"{f['note']}, ascent {f['ascent']}/{f['lineH']}")
        parts.append("namespace {")
        parts.append(c_array(f"kCp{k}", "uint16_t", [e[0] for e in f["entries"]],
                             16, lambda v: f"0x{v:04X}"))
        parts.append(c_array(f"kOff{k}", "uint32_t", [e[1] for e in f["entries"]], 12))
        parts.append(c_array(f"kMidx{k}", "uint16_t", [e[2] for e in f["entries"]], 16))
        parts.append(c_array(
            f"kMet{k}", "Metrics", f["metrics"], 4,
            lambda m: "{%d,%d,%d,%d,%d,%d}" % (m.bw, m.bh, m.stride, m.advance, m.xo, m.yo)))
        parts.append(c_array(f"kBlob{k}", "uint8_t", list(f["blob"]), 24,
                             lambda v: f"0x{v:02X}"))
        parts.append("}  // namespace")
        parts.append(
            f"const Face face{k} = {{{f['px']}, {f['ascent']}, {f['lineH']}, "
            f"{len(f['entries'])}, kCp{k}, kOff{k}, kMidx{k}, kMet{k}, kBlob{k}}};")
        parts.append("")
    parts += ["}  // namespace fontdata", "}  // namespace yat", ""]
    open(path, "w").write("\n".join(parts))


def emit_data_header(all_faces, path):
    decls = "\n".join(f"extern const Face face{f['key']};" for f in all_faces)
    open(path, "w").write(HEADER_NOTE + f"""//
// Internal layout of the generated tables. Consumers use the public accessor in
// <yat/fonts.h>; this header only exists so fonts.cpp can name the faces.
#pragma once
#include <cstdint>

namespace yat {{
namespace fontdata {{

// Per-glyph geometry, deduplicated: every ideograph at a given size tends to
// share one tuple, so glyphs index a small palette instead of each carrying a
// copy. `xo`/`yo` place the ink box relative to (pen x, baseline y); `yo` is
// negative for the usual case of ink above the baseline.
struct Metrics {{
  uint8_t bw, bh, stride, advance;
  int16_t xo, yo;
}};

// One rasterized (size, weight) pair. `cps` is sorted for binary search; the
// three per-glyph arrays are parallel to it. `blob` holds the packed bitmaps:
// `bh` rows of `stride` bytes, MSB-first, top row first.
struct Face {{
  int px, ascent, lineHeight, count;
  const uint16_t* cps;
  const uint32_t* off;
  const uint16_t* midx;
  const Metrics* metrics;
  const uint8_t* blob;
}};

{decls}

}}  // namespace fontdata
}}  // namespace yat
""")


def main():
    used = scan_used_cjk()
    plans = charsets(used)
    print(f"scan: {len(used)} CJK ideographs used in-repo")

    all_faces = []
    by_px = collections.defaultdict(list)
    for key, px, lineH, bold, chars in plans:
        f = build_face(key, px, lineH, bold, chars)
        all_faces.append(f)
        by_px[px].append(f)
        print(f"  face {key:>5}: {len(f['entries']):5d} glyphs  "
              f"{len(f['blob']):8d} bitmap B  {len(f['metrics']):4d} metrics  {f['note']}"
              + (f"  OVERFLOW top+{f['over_top']} bot+{f['over_bot']}"
                 if f["over_top"] or f["over_bot"] else ""))
        if f["missing"]:
            print(f"           unmapped in both faces: {''.join(f['missing'])}")

    for px, faces in sorted(by_px.items()):
        emit_face_tu(faces, os.path.join(OUT_SRC, f"fonts_data_{px}.cpp"))
    emit_data_header(all_faces, os.path.join(OUT_SRC, "fonts_data.h"))

    total = 0
    print("\nflash cost per size:")
    for px, faces in sorted(by_px.items()):
        b = sum(len(f["blob"]) for f in faces)
        t = sum(len(f["entries"]) * 8 + len(f["metrics"]) * 8 for f in faces)
        n = sum(len(f["entries"]) for f in faces)
        total += b + t
        print(f"  {px:>4}px: {n:6d} glyphs  bitmaps {b/1024:8.1f} KB  "
              f"tables {t/1024:6.1f} KB  = {(b+t)/1024:8.1f} KB")
    print(f"  TOTAL: {total/1024:.1f} KB ({total/1048576:.2f} MB)")


if __name__ == "__main__":
    sys.exit(main())
