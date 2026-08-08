#pragma once
#include <cstdint>

namespace yat {

// §9.9 icon catalog. Every icon in the closed 32-name catalog is a 24x24 1-bit
// bitmap that the engine draws at integer scale x1/x2/x4 for size small (24) /
// medium (48) / large (96) — crisp at every size, with no font dependency and
// no scaling arithmetic beyond a fillRect per set bit.
//
// The bitmaps are generated from geometry by tools/icons/gen_icons.py (the icon
// source of truth) into engine/src/icons.cpp; regeneration is deterministic.
constexpr int kIconBase = 24;      // bitmap edge, px
constexpr int kIconStride = 3;     // bytes per row (24 bits)
constexpr int kIconBytes = 72;     // 24 rows x 3 bytes

// Looks `name` up in the catalog. On success `out` points at kIconBytes bytes:
// 24 rows of 3, MSB first (bit 7 of byte 0 is column 0). Returns false for any
// name outside the catalog — the caller keeps drawing the honest placeholder
// box for those (a schema-valid pack can never reach that path).
bool iconBitmap(const char* name, const uint8_t*& out);

}  // namespace yat
