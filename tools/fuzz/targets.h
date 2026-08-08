// Shared "feed these bytes to parser X" logic used by both the libFuzzer
// harnesses (fuzz_*.cpp) and the non-libFuzzer ASAN/UBSAN smoke driver
// (smoke_driver.cpp) — kept in one place so the two entry points can't
// drift apart. See PACK-SPEC.md §12.3(9): all fetched bytes are hostile
// input, these are the hand-rolled parsers that see them first.
#pragma once

#include <yat/engine.h>

#include <cstdint>
#include <string>

namespace yat_fuzz {

using yat::detail::EvalCtx;

// ---------------------------------------------------------------------
// rss / csv: raw fetched response bytes straight into the parser.
// ---------------------------------------------------------------------

inline void runRss(const std::string& body) {
  JsonDocument out;
  std::string err;
  yat::detail::parseRss(body, out, err);
}

// First byte selects has_header (odd = true), matching how a pack's
// `format: "csv"` source declares it; the rest is the CSV body.
inline void runCsv(const std::string& raw) {
  if (raw.empty()) return;
  bool hasHeader = (static_cast<unsigned char>(raw[0]) & 1) != 0;
  std::string body = raw.substr(1);
  JsonDocument out;
  std::string err;
  yat::detail::parseCsv(body, hasHeader, out, err);
}

// ---------------------------------------------------------------------
// path: expression string evaluated against a small fixed extracted-data
// document standing in for a source's parsed JSON/RSS/CSV output.
// ---------------------------------------------------------------------

inline JsonDocument& pathFixedDoc() {
  static JsonDocument doc;
  static bool init = false;
  if (!init) {
    deserializeJson(doc, R"JSON({
      "temperature": {"data": [
        {"place": "Sha Tin", "value": 27.5, "unit": "C"},
        {"place": "Central", "value": 29, "unit": "C"}
      ]},
      "tags": ["a", "b", "c"],
      "nested": {"x": 1, "y": [1, 2, 3]},
      "empty_arr": [],
      "flag": true,
      "maybe_null": null,
      "name": "Hello World",
      "mixed": [1, "two", 3.5, true, null, {"k": "v"}]
    })JSON");
    init = true;
  }
  return doc;
}

inline void runPath(const std::string& expr) {
  JsonDocument out;
  std::string err;
  yat::detail::evalPath(pathFixedDoc().as<JsonVariantConst>(), expr, out, err);
}

// ---------------------------------------------------------------------
// placeholder / when: fixed EvalCtx standing in for a pack's
// params/data/item/each/sources/strings — the templates and `when`
// expressions themselves are pack- or (via {{data.*}}) fetched-data-
// controlled text.
// ---------------------------------------------------------------------

struct FixedEvalDocs {
  JsonDocument pack, params, data, item, each, sources, strings;
};

inline FixedEvalDocs& fixedEvalDocs() {
  static FixedEvalDocs f;
  static bool init = false;
  if (!init) {
    deserializeJson(f.pack, R"JSON({
      "render": {"lang_param": "lang"},
      "params": {"properties": {
        "mode": {"enum": ["a", "b", "c"],
                 "enum_titles": [{"en": "A"}, {"en": "B", "zh-Hant": "乙"}, {"en": "C"}]},
        "items": {"items": {"enum": ["x", "y"],
                             "enum_titles": [{"en": "X"}, {"en": "Y", "zh-Hant": "歪"}]}}
      }}
    })JSON");
    deserializeJson(f.params, R"JSON({
      "district": "Sha Tin", "count": 5, "enabled": true,
      "lang": "both", "mode": "b", "items": [1, 2, 3]
    })JSON");
    deserializeJson(f.data, R"JSON({
      "temp": 27.5, "title": "Hello", "tags": ["a", "b", "c"],
      "nested": {"x": 1}, "empty_arr": [], "maybe_null": null,
      "days_1": "2", "days_2": "0", "aqhi": "5", "risk": "Moderate",
      "iso_date": "2026-07-27T10:00:00+08:00", "hhmm": "0930"
    })JSON");
    deserializeJson(f.item, R"JSON({"name": "Row1", "value": 42, "each_prop": "z"})JSON");
    deserializeJson(f.each, R"JSON({"prop": "val", "label": "x"})JSON");
    deserializeJson(f.sources, R"JSON({"first": {"field1": "abc", "field2": null}})JSON");
    deserializeJson(f.strings, R"JSON({"greeting": {"en": "Hello", "zh-Hant": "你好"}})JSON");
    init = true;
  }
  return f;
}

inline EvalCtx makeEvalCtx(std::string* warnSink) {
  FixedEvalDocs& f = fixedEvalDocs();
  EvalCtx ctx;
  ctx.params = f.params.as<JsonVariantConst>();
  ctx.data = f.data.as<JsonVariantConst>();
  ctx.pack = f.pack.as<JsonVariantConst>();
  ctx.strings = f.strings.as<JsonVariantConst>();
  ctx.item = f.item.as<JsonVariantConst>();
  ctx.each = f.each.as<JsonVariantConst>();
  ctx.sources = f.sources.as<JsonVariantConst>();
  ctx.index = 3;
  ctx.inRow = true;
  ctx.lang = "both";
  ctx.now = 1785300000;  // fixed epoch — 2026-07-27ish; determinism doesn't matter here
  ctx.warn = warnSink;
  return ctx;
}

// First byte selects the call shape (mode = byte0 % 4); the rest is the
// template/expression text. Seed corpus files use a literal ASCII '0'-'3'
// prefix, which happens to map directly to mode 0-3 (mod 4 of '0' == 48).
//   0: substitute(), urlEncode=false   (render-string / non-URL placeholder)
//   1: substitute(), urlEncode=true    (https source url substitution, §7.3.2)
//   2: evalWhen() with a string node   (a plain `when: "..."` expression)
//   3: evalWhen() with a JSON node     (a `when: {any:[...]}/{all:[...]}` object)
inline void runPlaceholder(const std::string& raw) {
  if (raw.empty()) return;
  uint8_t mode = static_cast<unsigned char>(raw[0]) % 4;
  std::string rest = raw.substr(1);
  std::string warn;
  EvalCtx ctx = makeEvalCtx(&warn);
  std::string err;

  if (mode == 0 || mode == 1) {
    std::string out;
    bool hadTypeError = false, hadNullSourceRef = false;
    yat::detail::substitute(rest, ctx, out, err, /*urlEncode=*/mode == 1, &hadTypeError,
                            &hadNullSourceRef);
  } else if (mode == 2) {
    JsonDocument whenDoc;
    whenDoc.set(rest);
    bool visible = false;
    yat::detail::evalWhen(whenDoc.as<JsonVariantConst>(), ctx, visible, err);
  } else {
    JsonDocument whenDoc;
    if (deserializeJson(whenDoc, rest)) return;  // not valid JSON — nothing to evaluate
    bool visible = false;
    yat::detail::evalWhen(whenDoc.as<JsonVariantConst>(), ctx, visible, err);
  }
}

}  // namespace yat_fuzz
