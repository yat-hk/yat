#pragma once
#include <cstdint>

namespace yat {

// Built-in bitmap fonts — spec §9.3/§9.7 real typography (v0.3).
//
// Noto Sans (Latin/symbols) + Noto Sans TC (CJK), rasterized 1-bit at the exact
// pixel sizes the spec mandates by tools/fonts/gen_fonts.py. The tables live in
// engine/src/fonts_data_<px>.cpp, so every target that links the engine — the
// native preview, the WASM build and the firmware — gets byte-identical
// typography for free. That is what makes the §12.2 pixel-identity promise
// survive the move off efont: there is exactly one font, in the engine.
//
// Faces: text 16/24/32/48 px (line heights 20/30/40/60) in regular + bold, and
// digits-only bignum faces at 96/128 px (line heights 112/148).

// One glyph, ready to blit. The bitmap is `bh` rows of `stride` bytes, MSB-first
// within each byte, top row first.
//
// Geometry is expressed against the pen: draw the bitmap's top-left corner at
// (penX + xOffset, baselineY + yOffset), scaling each source pixel to a
// `scale`x`scale` block. `yOffset` is normally negative (ink above the
// baseline). All of xOffset/yOffset/advance are final device pixels — `scale` is
// already folded in, so callers never multiply anything themselves.
struct GlyphInfo {
  const uint8_t* bitmap = nullptr;
  uint8_t bw = 0;       // bitmap width in source pixels
  uint8_t bh = 0;       // bitmap height in source pixels
  uint8_t stride = 0;   // bytes per bitmap row
  uint8_t scale = 1;    // 1, or 2 when a half-size face is standing in (see below)
  uint8_t advance = 0;  // pen advance, device px
  int16_t xOffset = 0;
  int16_t yOffset = 0;
  // Synthetic bold: OR the bitmap with itself shifted 1px right while blitting.
  // Set when bold was asked for at a (size, codepoint) that has no real Bold
  // outline — CJK ideographs above 32px, and anything outside the bold face's
  // charset. `advance` already includes the extra pixel.
  bool smear = false;
};

// Looks up one codepoint. `px` must be one of the face sizes above; `cp` beyond
// the BMP always misses (the engine's UTF-8 decoder folds those to U+FFFD
// anyway). Returns false when nothing in the fallback chain covers `cp`, which
// callers render as a tofu box — see engine/src/render.cpp.
//
// Fallback chain, in order:
//   1. the exact (px, weight) face;
//   2. the same size's regular face, with `smear` set (synthetic bold);
//   3. half that size at scale 2 — 48->24, 32->16 — which keeps proportional
//      advances and therefore correct layout, at the cost of doubled pixels.
//      This is the only integer-scale path left in the engine; it exists so a
//      pack that renders CJK at xlarge degrades visibly rather than vanishing.
bool fontGlyph(int px, uint32_t cp, bool bold, GlyphInfo& out);

// Line box height (§9.3) and the baseline's offset from the top of that box.
// Both are 0 for a size with no face.
int fontLineHeight(int px);
int fontAscent(int px);

}  // namespace yat
