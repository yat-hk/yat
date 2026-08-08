#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <yat/fonts.h>

namespace yat {

enum class Ink : uint8_t { Black = 0, White, Red, Yellow, Green, Blue };

// Semantic ink roles (spec §9.1a). A role names what a value MEANS — a wait
// that is fine, a warning in force, a line of supporting detail — and the
// engine picks the ink. Packs SHOULD spell colour this way; the six raw ink
// names stay valid forever (packs in the wild, and the cases where the colour
// IS the meaning: official warning colours, operator livery, a market's
// red-up convention).
enum class InkRole : uint8_t { Accent, Good, Warn, Danger, Info, Muted };

// Panel colour capability (ROADMAP "device capability profiles", phase 2 of
// the colour system). Spectra6 is the 6 fixed inks §9.1 specifies; Mono is
// black/white only — no ink request, literal or role, can mean anything else.
enum class PanelPalette : uint8_t { Spectra6, Mono };

// What one physical panel model can actually show: its colour capability plus
// its pixel dimensions. `inkForRole`/`inkFromName` and the `image` dither
// palette (engine/src/render.cpp) all consult the active profile, bound once
// at render(), so the same pack renders honestly on every model instead of
// every draw call hard-coding "800x480, 6 inks". Width/height are carried here
// (not just read off the Canvas) so a future differently-sized model is a new
// profile, not an engine change — today every shipped/planned model is
// 800x480 (PRD.md/ARCHITECTURE.md hardware sections), so add-a-profile is
// deliberately more work than resize-a-constant would have been.
struct DeviceProfile {
  const char* name;
  int width;
  int height;
  PanelPalette palette;
};

// SeeedStudio reTerminal E1002 — today's only shipped device and the engine's
// default, so every render() call site written before device profiles existed
// stays byte-identical with no argument change (PRD.md: 800x480 ED2208
// Spectra 6, 6 fixed inks).
inline constexpr DeviceProfile kProfileE1002{"e1002", 800, 480, PanelPalette::Spectra6};

// reTerminal E1001 — same panel size, mono instead of Spectra 6 (PRD.md's
// build-matrix note: "colour vs mono, refresh speed" is the documented
// difference between the two models; nothing else changes today).
inline constexpr DeviceProfile kProfileE1001{"e1001", 800, 480, PanelPalette::Mono};

// "e1002"/"e1001" -> the matching profile above. Unknown name -> false, `out`
// untouched; callers keep whatever default they already had (kProfileE1002).
bool deviceProfileFromName(const char* name, DeviceProfile& out);

// What a role paints with (spec §9.1a-a).
//
// `mark` is the ink every *mark* uses: type, icon bitmaps, rules, chart lines,
// a bar's fill — anything whose meaning is carried by a stroke a few pixels
// wide. `field` is an ink that may be laid as an *area* behind those marks, or
// Ink::White for "this role has no field of its own".
//
// Only `warn` carries both, and that is the entire point. Measured against
// this panel's white, yellow ink is about 1.1:1 — a yellow mark on white is
// not faint, it is absent at household viewing distance, and every attempt to
// darken it by weaving another ink through fails on its own terms (red woven
// into yellow reads as pink and collides with `danger`; black woven into
// yellow reads as dirty grey and stops being yellow at all — at 126 DPI a 4x4
// dither cell is 0.8 mm, well inside what an eye resolves at a metre, so the
// weave stays a texture and never blends into a colour). What does work is the
// pairing every hazard sign in the world already uses: a black mark on a
// yellow field. So `warn` marks in black and keeps yellow for the field the
// engine lays under them — and a pack that spells the ink `"yellow"` outright
// still gets yellow, mark and all, because there the colour is the identity of
// the thing (operator livery, an official amber signal) and not a judgement
// the reader has to be able to see.
struct RolePaint {
  Ink mark;
  Ink field;  // Ink::White = no field
};

// The role→paint table, clamped to what `profile` can actually show. This
// function (plus paintFromName/inkFromName below, which route through it) is
// the ONE place a role or a literal ink name becomes an Ink: nothing else in
// the engine (or in a host) may hard-code either mapping. Omitting `profile`
// renders against kProfileE1002/Spectra6. Keep the switch total: every role
// must return a paint.
RolePaint paintForRole(InkRole role, const DeviceProfile& profile = kProfileE1002);

// The mark half of paintForRole — what a caller that can only draw one ink
// (all type, every icon, every rule) is asking for.
Ink inkForRole(InkRole role, const DeviceProfile& profile = kProfileE1002);

// "danger" -> InkRole::Danger. Unknown name -> false, `out` untouched.
bool inkRoleFromName(const char* name, InkRole& out);

// Accepts either spelling: an ink literal ("red") or a role name ("danger",
// resolved through paintForRole above) — clamped to `profile` either way,
// since a literal ink name is exactly as unshowable on mono hardware as a role
// is. An ink literal answers with itself as the mark and no field: the pack
// asked for that ink, and got it.
bool paintFromName(const char* name, RolePaint& out,
                   const DeviceProfile& profile = kProfileE1002);

// The mark half of paintFromName, for the callers that draw one ink.
bool inkFromName(const char* name, Ink& out, const DeviceProfile& profile = kProfileE1002);

// Render target. Implementations: 4bpp EPaper sprite (firmware), RGB buffer (native/WASM).
struct Canvas {
  virtual ~Canvas() = default;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual void drawPixel(int x, int y, Ink ink) = 0;
  virtual void fillRect(int x, int y, int w, int h, Ink ink) {
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++) drawPixel(i, j, ink);
  }
};

// Typography source for the renderer (spec §9.3/§9.7).
//
// v0.3 moved the fonts *into* the engine: the real proportional faces are
// compiled from engine/src/fonts_data_<px>.cpp and reachable through
// <yat/fonts.h>, so the default implementations below are what every in-tree
// target uses. Instantiate `DefaultFontProvider` and be done — the type stays a
// virtual interface only so a host can still substitute its own faces.
//
// Overriding `glyph` obliges you to override `lineHeight`/`ascent` consistently:
// the renderer measures with one and draws with the others, and §12.2's
// pixel-identity promise across targets depends on them agreeing.
struct FontProvider {
  virtual ~FontProvider() = default;
  virtual bool glyph(int px, uint32_t cp, bool bold, GlyphInfo& out) {
    return fontGlyph(px, cp, bold, out);
  }
  virtual int lineHeight(int px) { return fontLineHeight(px); }
  virtual int ascent(int px) { return fontAscent(px); }

  // Deprecated v0.1 path: one 16x16 monochrome cell, 32 bytes, 2 bytes per row,
  // MSB first. The engine no longer calls this — it was the efont-era interface,
  // and it cannot express a proportional advance or a size other than 16. Kept
  // (no longer pure) so an out-of-tree provider written against v0.2 still
  // compiles; such a provider now silently supplies nothing and should move to
  // `glyph` above.
  virtual bool glyph16(uint16_t utf16, uint8_t out[32]) {
    (void)utf16;
    (void)out;
    return false;
  }
};

// The engine's built-in Noto Sans / Noto Sans TC faces. This is the provider all
// three targets pass to Engine::render().
struct DefaultFontProvider : FontProvider {};

// §9.10 `image` widget: PNG decode, pluggable so the portable engine core
// never links a decoder itself (firmware will wire pngle later; the native
// preview wires stb_image — see tools/preview/main.cpp). `png` is the raw
// fetched byte string (already size-capped and magic-byte PNG-checked by the
// caller, engine/src/render.cpp); implementations may assume it looks like a
// PNG but MUST still fail cleanly (return false) on anything they can't
// decode. On success `rgb` is exactly `w*h*3` bytes, row-major, 3 bytes/pixel
// (alpha, if any, composited away — the panel has no notion of
// transparency). No decoder installed (`Engine::setImageDecoder(nullptr)`,
// the default) -> every `image` widget renders the §9.10 failure box and
// warns "no decoder in this build".
struct ImageDecoder {
  virtual ~ImageDecoder() = default;
  virtual bool decode(const std::string& png, int& w, int& h, std::vector<uint8_t>& rgb) = 0;
};


}  // namespace yat
