# yat-wasm — WASM engine target (feasibility spike)

Proves the portable YAT engine (`engine/`) compiles to WebAssembly and renders
**pixel-identical** to the native target (`tools/preview`), for the
"one engine, three targets" architecture (firmware / native preview / browser).
This directory is a spike: a new host shim (`wasm_main.cpp`, analogous to
`tools/preview/main.cpp`) plus a build script and an automated identity test.
No `engine/` source was modified. One additive, non-behavior-changing change
was made to `tools/preview/main.cpp`: a `--raw <file>` flag that dumps the
canvas' raw RGB bytes alongside the PNG (PNG output is still the default;
existing golden tests are unaffected — `tools/preview/run-tests.sh` still
passes).

## Result: identity confirmed

`node test-identity.mjs` renders two packs on both targets with the same
pinned `--now 1785142800` and byte-compares the RGB pixels:

```
canvas: 800x480
OK   hko-now: 1152000 bytes identical
OK   family-board (CJK/qr/icon): 1152000 bytes identical

ALL PACKS IDENTICAL (native RGB == wasm RGB)
```

- **hko-now** — a real `https` source via a pinned fixture; exercises path
  expressions, placeholder substitution, `bignum`/`text`/`row`/`column`
  widgets, and standard chrome.
- **family-board** — `inline` source only (no fixtures/network at all); the
  CJK-heavy pack, and per `tools/preview/run-tests.sh` the first pack whose
  every widget is really drawn. This proves the vendored efont CJK bitmap
  font, the qrcodegen QR encoder, and the icon catalog all round-trip
  through `emcc` bit-for-bit identically to native.

Both packs matched on the **first attempt** — no divergence to analyze.
This is a meaningful result on its own: the engine has no lurking
64-bit-host assumptions (wasm32 uses 32-bit `long`/pointers vs. native's
64-bit) and no floating-point or memory-layout behavior that differs under
emscripten's libc vs. macOS's.

Also spot-checked visually in a real browser (Chromium, via Playwright) with
`demo.html`: renders correctly, ~13ms module instantiation + ~13ms render for
hko-now, CJK glyphs ("現時天氣", "沙田", "濕度") crisp and correct.

## Build

Requires `emcc` (built against 5.0.6-git here, at `/opt/homebrew/bin/emcc`).

```sh
cd tools/wasm
EMCC=/opt/homebrew/bin/emcc ./build.sh   # or just ./build.sh if emcc is on PATH
```

Produces `yat-engine.js` + `yat-engine.wasm` in this directory.

**Gotcha:** a single `emcc` invocation can't apply `-std=c++17` to
`engine/third_party/qrcodegen/qrcodegen.c` (a C file) the way
`tools/preview/Makefile`'s per-suffix implicit rules do for free. `build.sh`
compiles C++ sources and the one C source into separate `.o` files
(`std=c++17` vs `std=c11`) under `build/`, then links them in a final `emcc`
call — same source list as the Makefile, just split into two compile passes.

### Size / timing

| | |
|---|---|
| `yat-engine.wasm` | 1,001,501 bytes (~978 KiB) |
| `yat-engine.js` (glue) | 15,046 bytes |
| Cold build (first run, generates emscripten's cached sysroot libs) | ~12.7s |
| Warm rebuild | ~0.77s |

The vendored efont CJK bitmap data (`engine/third_party/efont/efontFontData.h`)
is ~710 KB of raw glyph bytes (7.5 MB as C source text, ~727k comma-separated
literals) — it accounts for the large majority of the `.wasm` binary. See
"Font-asset optimization" below.

## Test

```sh
cd tools/wasm
node test-identity.mjs
```

Self-contained: builds `tools/preview/yat-preview` if missing, builds nothing
for wasm (run `./build.sh` first — the test doesn't invoke `emcc` itself),
and shells out to the native binary with `--raw` to get its reference bytes
on the fly. No files outside `tools/preview` and `tools/wasm` are touched or
required. On mismatch it reports the exact first-diverging byte offset,
pixel (x, y), and channel (R/G/B) for triage — untested here since both
packs matched immediately, but load-bearing if a future engine change or a
different target/compiler combination ever does diverge.

## Demo

`demo.html` — human-openable, not part of the automated test. Renders
hko-now into a `<canvas>` in an actual browser.

```sh
cd /path/to/yat        # repo root
python3 -m http.server 8080
# open http://localhost:8080/tools/wasm/demo.html
```

Must be served over http — `fetch()` of local pack/fixture files and
streaming wasm compilation are blocked from `file://`.

## API surface (exported C functions)

```c
int          yat_render(const char* packJson, const char* paramsJson,
                         const char* docsJson, double nowEpoch);
uint8_t*     yat_get_buffer();   // 800*480*4 bytes, RGBA, alpha always 255
int          yat_get_width();    // 800
int          yat_get_height();   // 480
const char*  yat_get_error();    // last load/extract/render error or warning; "" if clean
```

`docsJson` is `{"<sourceid>": "<body>", ...}` — a flat map of pre-fetched
source bodies, mirroring `tools/preview/main.cpp`'s `--doc id=file` fixtures.
A value may also be a JSON array of strings, consumed in order across
repeated fetches of the same source id (for a `for_each` source, or an
`image` widget re-fetched across renders) — same shape as native's comma-
separated fixture lists. An `image` widget (§9.10) has no source id of its
own and is always fetched as id `"image"`, exactly as in the native preview.

Canvas is RGBA (not native's RGB) because browser `ImageData` wants RGBA;
`demo.html` and `test-identity.mjs` both show how to slice/convert it.

## Gotchas for the real site integration

1. **No network fetch inside wasm.** This shim never calls out to the
   network itself — it's handed pre-fetched bodies via `docsJson`. This is
   the *recommended* real-site shape, not just a spike shortcut: the site's
   JS does `fetch()` for each source (parallelizable, cacheable,
   CORS-controllable from JS), then calls `yat_render()` once,
   synchronously, with the collected bodies. Avoids needing Asyncify or a
   worker just to let a C++ call await a network response.
2. **Heap views are opt-in in this emcc version.** `Module.HEAPU8` is not
   attached by default in 5.0.6-git — it must be named in
   `-sEXPORTED_RUNTIME_METHODS` (`build.sh` does this). Older emscripten
   versions exported it unconditionally; don't assume older tutorials/code
   still work as-is.
3. **`ccall`'s `"string"` arg/return types are enough** — no manual
   `malloc`/`stringToUTF8`/`allocate` plumbing was needed for
   pack/params/docs JSON in or the error string out; `ccall` marshals
   strings on the wasm stack automatically. Simpler than the original spec
   note anticipated.
4. **MODULARIZE=1 + EXPORT_ES6=0 output works unmodified in both Node and
   the browser** — it's a UMD-style factory (`module.exports` when CommonJS
   is detected, a global `YatModule` otherwise). `test-identity.mjs` (Node)
   and `demo.html` (browser) load the exact same `yat-engine.js` with no
   target-specific build.
5. **`image` widget (PNG decode) is untested by this spike** — neither
   `hko-now` nor `family-board` uses one. Before shipping image support,
   wire an `ImageDecoder` (stb_image like native, or bridge to the browser's
   own `<canvas>` decode-then-hand-back-raw-RGB) and add it to the identity
   test.
6. **Determinism held under wasm32** — no divergence appeared despite
   wasm32's 32-bit `long`/pointer width vs. native's 64-bit host, meaning
   the engine's arithmetic (hashing, path eval, dithering, layout) has no
   hidden 64-bit-only assumption to fix later.

## Font-asset optimization (follow-up, not done here)

`efontFontData.h`'s ~710 KB of CJK glyph bytes is `#include`d directly into
this build's rodata — the biggest single contributor to the ~978 KB
`.wasm`. For the real flasher-site preview, consider fetching the font data
as a separate asset (lazily, and cacheable independently of engine-code
changes/updates) rather than baking it into the wasm binary: e.g. export a
`yat_load_font(const uint8_t* data, size_t len)` that copies fetched bytes
into a heap buffer before the first render, with `EfontProvider` reading
from that buffer instead of the compiled-in array. Out of scope for this
spike (which only needed to prove the font *renders* correctly through
`emcc`, which it does — see the family-board identity result above), but
worth doing before shipping, since most page loads won't need the full CJK
table's constant ~710 KB tax up front.
