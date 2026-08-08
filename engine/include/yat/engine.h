#pragma once
#include <ArduinoJson.h>

#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "canvas.h"

// YAT spec engine — v0.2 subset of Pack Spec 0.3-draft.
// Implemented: https+inline sources incl. source-level `when`; `for_each`+
// `collect` fan-out with `{{each}}`/`{{each.<prop>}}` (§5.1a); formats
// json/rss(+atom)/csv (§5.3–5.5); path expressions (member/index/one
// filter+projection/pipes [n] length first last round); placeholders
// {{params.*}} {{data.*}} {{now}} {{strings.*}} {{item…}} {{index}}
// {{each…}} {{sources.<id>.<field>}} with filters round/pad/time_hhmm/
// date_fmt/date_diff_days/upper/weekday/abs/sign_char/pick_by_day/enum_title;
// RFC 3986 percent-encoding of placeholder values substituted into https
// source URLs (§7.3.2); sequential source references (§5.7) — a later
// https/inline source's URL path/query and extract filter literals may
// substitute an earlier source's extracted field; backward-only (self/
// forward reference is a load error naming §5.7), null referent -> E_REF_NULL
// (source counts as failed); `compute` derived fields incl. §7.4(4) numeric
// typing; `when` (truthiness, == != < <= > >= has, exists/missing, any/all,
// item/index roots); widgets column/row/spacer/divider/text/bignum/list/qr/
// icon (incl. one nested list level, §9.8a: outer bind a for_each
// collect.field, inner bind item.<prop> over that source's collect.extract,
// outer x nested max_rows <= 40 [V]); qr (§9.11) is a real
// qrcodegen-backed encode (vendored Project Nayuki qrcodegen, MIT — see
// engine/third_party/qrcodegen/), ecc L/M/Q/H, black-on-white, >512-byte
// payload or encode failure falls back to the placeholder box; icon (§9.9)
// is the closed 32-name catalog as 24x24 1-bit bitmaps (engine/src/icons.cpp,
// authored as geometry by tools/icons/gen_icons.py and regenerated
// deterministically from it), blitted at integer scale x1/x2/x4 for size
// small/medium/large in the widget ink — no font dependency, crisp at every
// size; the catalog is closed, so an off-catalog name is unreachable from a
// schema-valid pack and keeps the bordered placeholder box + a warning;
// strings
// table + render.lang_param; standard chrome (+ optional footer battery
// glyph, Engine::setBatteryPercent() — G25/§11.4; default -1/unknown draws
// nothing at all, so every pre-existing render stays byte-identical; the
// catalog's "battery" icon is blitted at x1 with its interior body cavity
// repainted as a fill bar proportional to the percent, switching to red ink
// + a small bilingual low-battery hint at <=20%); FNV-1a data hash for
// hash-skip. (The §11.4 footer voice hint was removed in 0.4: with LLM
// routing accepting natural speech and the ←+→ help card listing every
// page's words, per-page keywords in the footer were clutter.)
// stale-serve (§11.3): an optional `StateStore` persists each source's last
// successfully extracted fields + timestamp, keyed "<packid>.<sourceid>"
// ({"t":epoch,"fields":{...}}; a for_each source's collected array under its
// collect.field). A null store (default, via setStateStore()) disables
// persistence entirely — v0.2 behavior, every failure yields fresh nulls,
// never stale. On fetch/parse failure (incl. E_REF_NULL, any for_each
// iteration failure) the engine loads the snapshot if one exists, serves its
// fields, and marks the source stale (Engine::anyStale()/oldestStaleSince());
// no snapshot -> nulls, not stale (never-succeeded, §11.3). Per-source
// `min_refresh_min` (§5.1): within the interval since the snapshot's
// timestamp, the engine skips the network entirely and serves the
// snapshot's fields as fresh (no stale flag, no glyph). Firmware wires a
// concrete StateStore (NVS/LittleFS); the native preview target provides a
// FileStateStore (tools/preview/main.cpp, --state <dir>).
// empty state (§11.3 "the engine never renders a blank page"): when EVERY
// source failed with no snapshot to fall back on — Engine::allSourcesEmpty()
// — render() draws a standard fallback card in the content band instead of
// the pack's widgets, which would otherwise substitute empty strings into
// every placeholder and leave a page-shaped ghost of itself. Distinct from
// stale-serve, which has content and renders normally with the footer badge.
// A `when`-skipped source is not a failure and does not count toward it, and
// a pack with no sources at all can never reach it.
// `image` widget (§9.10): the one construct fetched at *render* time, not
// during fetchAndExtract. fetchAndExtract() stashes a copy of its FetchFn
// (image.src is never known before render — it may reference {{now}} — so
// there is nothing to prefetch); render() reuses that stored copy for every
// `image` widget it draws, always calling it with sourceId "image" (fixture
// key for the native preview's --doc, since image widgets have no source
// id of their own). A pack rendered without ever calling fetchAndExtract()
// first has no stored FetchFn — image widgets draw the §9.10 failure box and
// warn accordingly. src substitution uses a *restricted* EvalCtx (params +
// now only — data/item/each/strings/pack left unset, §12.3(3)); form-(a)
// URLs (literal host, placeholders in path/query only) percent-encode those
// placeholder values same as a source url; form-(b) (src is exactly
// `{{params.<name>}}`, the whole-URL escape hatch) substitutes the raw param
// value unencoded, since it IS the URL, not a component of one. PNG decoding
// is pluggable (`ImageDecoder`, canvas.h) via `setImageDecoder()` so the
// portable engine core never links a decoder; render.cpp itself enforces the
// spec caps that are decoder-independent — PNG magic-byte check, ≤204800
// fetched bytes, decoded source pixels ≤800×480 — before ever calling the
// decoder. Scaling is nearest-neighbor into the widget's width×height box per
// `fit` (contain/cover/stretch); ink mapping is serpentine Floyd-Steinberg
// error diffusion against the 6-ink palette (`dither: "floyd_steinberg"`,
// default) or plain nearest-ink (`dither: "none"`). Any failure along this
// path (no FetchFn, fetch error, >204800 bytes, not a PNG, no decoder,
// decode failure, oversized source pixels) draws the §9.10 placeholder frame
// — the widget's own box, bordered, with 相片載入唔到 / Photo didn't load
// centered in it — and adds a render warning naming the cause. (The spec's
// letter was "the box renders empty"; on photo-frame, whose entire body is
// one image widget, an empty box is an empty panel indistinguishable from a
// dead device.) Load-time [V]: ≤2 `image` widgets per pack
// (extract.cpp, whole widget tree incl. list row templates).
// §9.1a-a semantic ink roles, amended: `warn` no longer resolves to a single
// ink. The panel's yellow measures ~1.1:1 against its white, so a yellow mark
// is invisible at household distance — `warn` marks in black and keeps yellow
// for the *field* the engine lays behind them (canvas.h's RolePaint). Two
// engine-owned areas use it: a `bar`'s fill, capped at its leading edge in the
// mark (the edge is the value, and against a white track it cannot be seen
// otherwise), and the §11.4a Action banner. An ink literal is untouched —
// `"color": "yellow"` still draws yellow, marks included.
// §11.4/§11.4a device notices — three levels, graded by what they ask of the
// reader (NoticeCode/NoticeLevel below). Info (this render carried an
// E_TYPE/E_BIND/E_ITER) draws §11.4's footer data-warning triangle, which is
// why standard chrome's footer is now painted AFTER the widget tree — the
// glyph is a fact about the render, not a flag set in advance; nothing
// overlaps, since the widget tree is clipped out of the chrome bands.
// Degraded (a source stale-serving 3h+, Engine::staleDegraded) draws a 24px
// line above the footer saying so in words. Both are derived by the engine
// with no host input. Action (Engine::setNotice) is the only level a host can
// raise, for the conditions only a device can know — a rejected key, storage
// that stopped answering — and draws a 34px banner at the top of the content
// band, under `"chrome": "none"` as well. Notices take space from the content
// band rather than covering it, and join dataHash() so a banner cannot be
// hash-skipped into invisibility (a page with no notice hashes as it always
// did).
// Device capability profiles (canvas.h's DeviceProfile, ROADMAP): render()'s
// optional `profile` argument (default kProfileE1002, unchanged behavior)
// swaps the §9.1a role table and the `image` dither palette for a panel with
// a different colour capability — kProfileE1001 today, a mono reTerminal
// E1001 at the same 800x480. See engine/src/render.cpp's inkForRole for the
// mono mapping and tools/preview's --profile flag for the CLI surface.
// Not yet: real icon glyphs, `{{sources.*}}` combined with a for_each
// source. Engine returns an error naming the construct when a pack uses one.

namespace yat {

// sourceId + substituted URL -> response body. Return false + err on failure.
using FetchFn = std::function<bool(const std::string& sourceId, const std::string& url,
                                   std::string& bodyOut, std::string& errOut)>;

// §11.3 stale-serve / §5.1 min_refresh_min persistence. The engine treats
// storage as a flat string->string map; keys are "<packid>.<sourceid>",
// values a JSON blob {"t": <epoch>, "fields": {...}}. Implementations own
// their own durability (NVS, LittleFS, a plain file — see tools/preview's
// FileStateStore); the engine never inspects the key/value bytes beyond
// that JSON shape. get() returns false for a missing key (not an error);
// put() returning false is treated as best-effort (the snapshot is simply
// not persisted this wake — no fatal effect on the render).
struct StateStore {
  virtual ~StateStore() = default;
  virtual bool get(const char* key, std::string& out) = 0;
  virtual bool put(const char* key, const std::string& val) = 0;
};

// §11.4a device notices — the three levels at which standard chrome tells a
// household that something is wrong, and the closed set of conditions that
// reach the panel at all.
//
// The levels differ in what they ask of the reader, which is the only thing a
// household display can meaningfully grade:
//
//   Info      Nothing to do. This render carried a data warning (E_TYPE /
//             E_BIND / E_ITER, §11.4) — one field of one page came out wrong.
//             A footer glyph, no words. Engine-derived; no host input.
//   Degraded  Nothing to do, but read the page differently: a source has been
//             stale-serving for hours, so the numbers on the panel are old.
//             One quiet line above the footer. Also engine-derived — the
//             engine is the only thing that knows how old its snapshots are.
//   Action    A person has to fix something, and nothing on the device can:
//             a provider rejected a key, storage stopped answering. A bordered
//             banner naming where the fix lives, every render until the
//             condition clears. Only the HOST can know these, so only these
//             have a NoticeCode.
//
// The engine owns the bilingual copy for every code (one voice, one place,
// and the preview renders exactly what the device will). A host names the
// condition; it never supplies the words.
enum class NoticeCode : uint8_t {
  None = 0,
  VoiceKeyRejected,  // the STT/LLM provider rejected the key — 401/403, not a timeout
  StorageFailed,     // writes to the device's storage are failing
  ConfigUnreadable,  // the device cannot read its own settings and is on defaults
};

enum class NoticeLevel : uint8_t { None = 0, Info, Degraded, Action };

NoticeLevel noticeLevel(NoticeCode code);

class Engine {
 public:
  // packJson: the .yat-pack.json text. paramsJson: user values object (may be
  // nullptr) — overlaid on top of the params-schema defaults.
  bool load(const char* packJson, const char* paramsJson, std::string& err);

  // Optional §11.3/§5.1 snapshot persistence. nullptr (the default) disables
  // it entirely, preserving v0.2 behavior exactly: every fetch/parse failure
  // yields fresh nulls and `min_refresh_min` has no effect. Does not take
  // ownership; the pointee must outlive the Engine (or subsequent calls that
  // touch it).
  void setStateStore(StateStore* store) { store_ = store; }

  // §9.10: PNG decoder for `image` widgets. Does not take ownership; the
  // pointee must outlive the Engine (or subsequent render() calls). nullptr
  // (the default) means every `image` widget draws the failure box and warns
  // "no decoder in this build" — the portable engine core links no decoder
  // of its own (see canvas.h's ImageDecoder).
  void setImageDecoder(ImageDecoder* decoder) { imageDecoder_ = decoder; }

  // G25/§11.4: standard-chrome footer battery glyph. -1 (the default) means
  // "unknown" and draws nothing at all — preserving every pre-existing
  // render byte-for-byte. Any other value is clamped to 0..100.
  void setBatteryPercent(int pct) {
    batteryPct_ = pct < 0 ? -1 : (pct > 100 ? 100 : pct);
  }

  // §11.4a: the host-known condition standard chrome should surface, or
  // NoticeCode::None (the default) for "nothing the device knows of" — which
  // draws nothing at all and leaves every pre-existing render byte-identical.
  // Set it every render while the condition holds and clear it the moment it
  // resolves: the engine has no memory between calls and no way to check.
  // Exactly one code at a time; a host holding several conditions passes the
  // one it most wants a person to act on (firmware/src/main.cpp orders them).
  void setNotice(NoticeCode code) { notice_ = code; }

  // §11.4a Degraded: this page has been serving a §11.3 snapshot for long
  // enough that the household should be told in words rather than by the
  // footer badge alone. Three hours — past every pack's refresh interval by an
  // order of magnitude, so it cannot fire on a hiccup, and short enough that a
  // page of morning bus times is never presented as current at lunch.
  bool staleDegraded(time_t now) const {
    return anyStale_ && oldestStale_ > 0 && now - oldestStale_ >= 3 * 3600;
  }

  // §11.4 footer voice hint. By default the words come straight out of the
  // loaded pack's §2 `aliases`, which is right for every host that matches
  // speech against those. A host whose user has overridden a page's alias
  // list (firmware config.json's per-page "aliases", docs/UX-NONTECH.md
  // Screen D) must pass those RAW override words here instead: printing the
  // pack's own words while the matcher listens for different ones is the one
  // way this footer can lie. Empty (the default) = use the pack's.

  // Runs every source in order: substitute URL, fetch, parse, extract into
  // data(); then evaluates `compute` entries into the same namespace (§7.4).
  // Resets/recomputes anyStale()/oldestStaleSince() for this call. Also
  // stashes a copy of `fetch` for render()'s `image` widgets (§9.10) — see
  // the file header comment above.
  bool fetchAndExtract(const FetchFn& fetch, time_t now, std::string& err);

  // True iff any source served a §11.3 stale snapshot during the most
  // recent fetchAndExtract() call (fetch/parse failure, E_REF_NULL, or a
  // failed for_each iteration, with a prior snapshot on record).
  bool anyStale() const { return anyStale_; }

  // Snapshot timestamp of the oldest currently-stale source — the "stale
  // since HH:MM" the standard chrome footer shows (§11.3/§11.4). 0 when
  // anyStale() is false.
  time_t oldestStaleSince() const { return oldestStale_; }

  // §11.3: true iff the most recent fetchAndExtract() left this page with
  // nothing at all to show — at least one source failed with no snapshot on
  // record ("never succeeded"), and NOT ONE source came back with content,
  // fresh or stale. This is what render() draws the empty-state card for.
  //
  // Deliberately not the same question as anyStale(): a stale-serving page
  // has real (old, badged) content and renders its widgets normally. Nor is
  // it "data() is all null" — a `when`-skipped source (§11.3: not a failure)
  // and an extraction miss (fresh truth, field absent) both leave nulls
  // behind without anything having gone wrong, and a page that legitimately
  // renders from `compute` alone would be slandered by that test.
  bool allSourcesEmpty() const { return srcEmpty_ > 0 && srcWithData_ == 0; }

  // Hash of extracted data + local date + params + pack id/version (spec §11.2).
  uint32_t dataHash(time_t now) const;

  // On success `err` carries any non-fatal render warnings (E_BIND, E_TYPE,
  // placeholder-widget notices — §11.3/§11.4); empty means a clean render.
  //
  // `profile` (default kProfileE1002, canvas.h): the device capability
  // profile to render against (ROADMAP "device capability profiles"). Every
  // call site written before this parameter existed — firmware, the WASM
  // target, every prior tools/preview invocation — omits it and renders
  // exactly as before: same table, same palette, byte-identical goldens.
  // Passing kProfileE1001 clamps every chromatic ink (role- or literal-named)
  // to black and dithers `image` widgets to black/white instead of 6 inks.
  bool render(Canvas& canvas, FontProvider& font, time_t now, std::string& err,
             const DeviceProfile& profile = kProfileE1002);

  JsonVariantConst pack() const { return pack_.as<JsonVariantConst>(); }
  JsonVariantConst params() const { return params_.as<JsonVariantConst>(); }
  JsonVariantConst data() const { return data_.as<JsonVariantConst>(); }

 private:
  JsonDocument pack_;
  JsonDocument params_;  // resolved values object
  JsonDocument data_;    // extracted fields object
  StateStore* store_ = nullptr;
  bool anyStale_ = false;
  time_t oldestStale_ = 0;
  int srcWithData_ = 0;  // sources that produced content this call (fresh/min_refresh/stale)
  int srcEmpty_ = 0;     // sources that failed with no snapshot to fall back on (§11.3)
  int batteryPct_ = -1;  // G25/§11.4: -1 = unknown = draw nothing (setBatteryPercent())
  NoticeCode notice_ = NoticeCode::None;  // §11.4a host-known condition (setNotice())
  bool iterWarn_ = false;  // §11.4: this fetch produced an E_ITER (partial for_each)
  FetchFn fetch_;                     // stashed by fetchAndExtract(), reused by render() (§9.10)
  ImageDecoder* imageDecoder_ = nullptr;
};

// ---- internals shared across engine translation units ----
namespace detail {

// Evaluate a document path expression (spec §6.1 subset) against doc.
// Placeholders in the expression must already be substituted.
// Result is written to out root; returns false only on grammar errors
// (misses yield null, per §6.2).
bool evalPath(JsonVariantConst doc, const std::string& expr, JsonDocument& out,
              std::string& err);

// Evaluation context threaded through substitution and `when` (§7.2 decides
// what each position may reference; a null/absent member means "illegal here",
// and the evaluator says so by name).
struct EvalCtx {
  JsonVariantConst params;   // resolved param values
  JsonVariantConst data;     // extracted fields
  JsonVariantConst pack;     // pack document — enum_titles (§8.13)
  JsonVariantConst strings;  // §9.13 table; set only for render strings
  JsonVariantConst item;     // current list element (§9.8); gated by inRow
  JsonVariantConst each;     // current for_each element (§5.1a); source scope only
  JsonVariantConst sources;  // §5.7: id -> {field: value} map of already-fetched
                             // earlier sources; null (default) = illegal position
  long index = 0;            // 1-based list row number (§7.1)
  bool inRow = false;        // inside a list row template
  const char* lang = nullptr;  // installed render.lang_param value, or null
  time_t now = 0;
  std::string* warn = nullptr;  // non-fatal warning sink (E_TYPE/E_BIND)
};

// Installed value of render.lang_param ("en"/"zh"/"both"), or null (§9.13).
const char* langValue(JsonVariantConst pack, JsonVariantConst params);

// Substitute {{...}} placeholders (§7) using `ctx`.
//
// urlEncode: when true, each placeholder's *substituted value* is RFC 3986
//   percent-encoded (unreserved chars `A-Za-z0-9-._~` pass through) before
//   being appended — used only for https source `url` substitution (§7.3.2).
//   The literal template text surrounding placeholders is never touched.
// hadTypeError: optional out-flag, set true iff any placeholder in `tpl` hit
//   a filter/conversion type-error outcome (§8 preamble / §7.3(4)) —
//   consulted by `evalCompute`'s §7.4(4) typing rule.
// hadNullSourceRef: optional out-flag, set true iff any `{{sources.<id>.
//   <field>}}` placeholder in `tpl` resolved to a null field value (§5.7
//   rule 3, E_REF_NULL). The caller must then treat the *whole referencing
//   source* as failed rather than fetching/using the partially-substituted
//   text — `substitute` itself still returns true with best-effort output.
bool substitute(const std::string& tpl, const EvalCtx& ctx, std::string& out, std::string& err,
                bool urlEncode = false, bool* hadTypeError = nullptr,
                bool* hadNullSourceRef = nullptr);

// Evaluate one `compute` template (§7.4). Result typing per §7.4(4): a
// template that is *exactly* one placeholder (no surrounding text) whose
// final value is numeric is stored as a JSON number; any other successful
// evaluation is stored as the §7.3(4) string; a type-error outcome anywhere
// in the template is stored as JSON null. Legal references: params/data/now.
// Returns false only on a grammar error.
bool evalCompute(const std::string& tpl, const EvalCtx& ctx, JsonDocument& out,
                 std::string& err);

// Parse an RSS 2.0 or Atom feed body into the canonical shape (spec §5.4):
//   {"feed": {"title": "..."}, "items": [{"title","link","description","pub_date"}]}
// Tolerant hand-written tag scanner — no external XML library. HTML tags are
// stripped, common entities decoded, `pub_date` normalized to ISO 8601 +08:00
// (RFC 822 and ISO inputs; offset-less = device-local). Items capped at 20,
// feed order preserved; missing fields "" except pub_date -> null. Returns
// false only when neither <channel> nor <feed> can be found.
bool parseRss(const std::string& body, JsonDocument& out, std::string& err);

// Parse an RFC 4180 CSV body into the canonical shape (spec §5.5): all cell
// values strings, never type-inferred.
//   hasHeader  -> {"rows": [ {"<col>": "<cell>", ...}, ... ]}
//   !hasHeader -> {"rows": [ ["<cell>", ...], ... ]}
// Quoting/""-escaping/CRLF handled; capped 100 rows x 20 cols (truncation,
// not error). Always returns true.
bool parseCsv(const std::string& body, bool hasHeader, JsonDocument& out, std::string& err);

// Widget/source `when` evaluation (§6.5 subset).
bool evalWhen(JsonVariantConst whenNode, const EvalCtx& ctx, bool& visible, std::string& err);

// Fixed device-local (UTC+8) civil time — spec §7.3(3); never host-TZ.
struct tm hkLocal(time_t t);

// Append a de-duplicated warning line to a warning sink.
void addWarn(std::string* sink, const std::string& msg);

uint32_t fnv1a(const void* buf, size_t len, uint32_t seed = 2166136261u);

}  // namespace detail
}  // namespace yat
