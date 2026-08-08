# tools/fonts — the engine's bitmap font generator

Rasterizes Noto Sans (Latin/symbols) and Noto Sans TC (CJK) into 1-bit glyph
bitmaps at the exact pixel sizes `docs/PACK-SPEC.md` §9.3/§9.7 mandate, and emits
them as C++ translation units the engine compiles directly.

**The generated output is checked in.** No build ever runs this script — the
engine, the native preview, the WASM target and the firmware all just compile
`engine/src/fonts_data_*.cpp`. Re-run it only when the charsets, the sizes, or the
source fonts change.

## What it produces

| Path | |
|---|---|
| `engine/include/yat/fonts.h` | public accessor: `GlyphInfo`, `fontGlyph()`, `fontAscent()`, `fontLineHeight()` — hand-written, not generated |
| `engine/src/fonts.cpp` | the lookup + fallback chain — hand-written, not generated |
| `engine/src/fonts_data.h` | generated: table layout + the `extern` face declarations |
| `engine/src/fonts_data_<px>.cpp` | generated: one TU per size, regular + bold |

## Setup

freetype-py needs a venv (the repo does not vendor it):

```sh
cd tools/fonts
python3 -m venv .venv
.venv/bin/pip install freetype-py
```

The source fonts live in `src/` (13MB — mostly the 12MB variable TC face). They
are there so a regeneration is reproducible without network access; if that is
too much weight for the repo, `src/*.ttf` can be gitignored and re-fetched with
exactly these commands:

```sh
cd tools/fonts/src
curl -sSLO https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf
curl -sSLO https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSans/hinted/ttf/NotoSans-Bold.ttf
curl -sSL -o 'NotoSansTC[wght].ttf' https://raw.githubusercontent.com/google/fonts/main/ofl/notosanstc/NotoSansTC%5Bwght%5D.ttf
curl -sSL -o OFL-NotoSans.txt   https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/OFL.txt
curl -sSL -o OFL-NotoSansTC.txt https://raw.githubusercontent.com/google/fonts/main/ofl/notosanstc/OFL.txt
```

Both are SIL Open Font License 1.1; keep the `OFL-*.txt` files next to the
fonts. See `THIRD_PARTY_NOTICES.md`.

## Run

```sh
tools/fonts/.venv/bin/python tools/fonts/gen_fonts.py    # ~80 s
```

It prints per-face glyph counts, bitmap bytes and the total flash cost, plus any
codepoint it could not map and any glyph that overflows its line box. Then:

```sh
cd tools/preview && make -s clean && make -s && ./run-tests.sh
```

**Every text pixel moves**, so every golden will mismatch. Inspect the new
renders before regenerating the goldens per the instructions inside
`run-tests.sh`, and re-run `tools/wasm/build.sh && node tools/wasm/test-identity.mjs`
— cross-target pixel identity is load-bearing (§12.2) and the font tables are
now part of what has to match.

## Why it is built this way

`gen_fonts.py`'s module docstring records the decisions and the evidence behind
them: mono hinting over threshold rasterization, em size == the spec's px, the
`ascent = px` baseline rule, where real Bold outlines earn their bytes versus
synthetic smear, and why the 48px face carries no CJK ideographs.

`compare_modes.py` is the throwaway that settled the first of those — it renders
the same CJK and Latin samples through all three candidate rasterization modes at
16/24/32px into `/tmp/fontcmp.png` at 4x, for eyeballing. Kept because the
docstring cites it.

## Budget

The 6MB app partition. Font data is ~1.38MB of it; `gen_fonts.py` prints the
breakdown per size, and the charset knobs that move it most are the Big5 Level 1
set (16/24px) and which sizes carry CJK ideographs at all.
