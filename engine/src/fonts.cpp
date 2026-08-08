// Font table lookup — the accessor over the generated data in
// fonts_data_<px>.cpp. See <yat/fonts.h> for the contract and
// tools/fonts/gen_fonts.py for how the tables are produced.
#include <yat/fonts.h>

#include "fonts_data.h"

namespace yat {

namespace {

using fontdata::Face;
using fontdata::Metrics;

struct SizeEntry {
  int px;
  const Face* regular;
  const Face* bold;
  int halfPx;  // size to fall back to at scale 2, 0 = none
};

// Ordered by size; `halfPx` wires up step 3 of the fallback chain.
const SizeEntry kSizes[] = {
    {16, &fontdata::face16r, &fontdata::face16b, 0},
    {24, &fontdata::face24r, &fontdata::face24b, 0},
    {32, &fontdata::face32r, &fontdata::face32b, 16},
    {48, &fontdata::face48r, &fontdata::face48b, 24},
    {96, &fontdata::face96r, nullptr, 0},
    {128, &fontdata::face128r, nullptr, 0},
};

const SizeEntry* sizeFor(int px) {
  for (const SizeEntry& e : kSizes)
    if (e.px == px) return &e;
  return nullptr;
}

// The codepoint arrays are sorted, so this is a plain binary search. Returns the
// glyph's index in the face's parallel arrays, or -1.
int findGlyph(const Face& f, uint16_t cp) {
  int lo = 0, hi = f.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t v = f.cps[mid];
    if (v == cp) return mid;
    if (v < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

bool fill(const Face& f, uint16_t cp, int scale, bool smear, GlyphInfo& out) {
  int i = findGlyph(f, cp);
  if (i < 0) return false;
  const Metrics& m = f.metrics[f.midx[i]];
  out.bitmap = f.blob + f.off[i];
  out.bw = m.bw;
  out.bh = m.bh;
  out.stride = m.stride;
  out.scale = (uint8_t)scale;
  out.advance = (uint8_t)(m.advance * scale + (smear ? 1 : 0));
  out.xOffset = (int16_t)(m.xo * scale);
  out.yOffset = (int16_t)(m.yo * scale);
  out.smear = smear;
  return true;
}

}  // namespace

bool fontGlyph(int px, uint32_t cp, bool bold, GlyphInfo& out) {
  if (cp > 0xFFFF) return false;
  uint16_t c = (uint16_t)cp;
  const SizeEntry* e = sizeFor(px);
  if (!e) return false;

  if (bold && e->bold && fill(*e->bold, c, 1, false, out)) return true;
  if (fill(*e->regular, c, 1, bold, out)) return true;

  if (e->halfPx) {
    const SizeEntry* h = sizeFor(e->halfPx);
    if (h) {
      if (bold && h->bold && fill(*h->bold, c, 2, false, out)) return true;
      if (fill(*h->regular, c, 2, bold, out)) return true;
    }
  }
  return false;
}

int fontLineHeight(int px) {
  const SizeEntry* e = sizeFor(px);
  return e ? e->regular->lineHeight : 0;
}

int fontAscent(int px) {
  const SizeEntry* e = sizeFor(px);
  return e ? e->regular->ascent : 0;
}

}  // namespace yat
