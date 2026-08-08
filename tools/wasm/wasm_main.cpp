// yat-wasm — WASM target of the YAT engine (feasibility spike).
//
// This is NOT a firmware/native peer target file (those live in engine/ and
// tools/preview/); it is a thin host shim, analogous to tools/preview/main.cpp,
// that exposes the portable engine core to JavaScript via three C functions:
//
//   int          yat_render(packJson, paramsJson, docsJson, nowEpoch)
//   uint8_t*     yat_get_buffer()   -- pointer to an 800x480 RGBA buffer
//   const char*  yat_get_error()    -- last load/extract/render error or warning
//
// docsJson is a flat JSON object mapping source id -> pre-fetched response
// body, e.g. {"current": "{...http body...}"}. A value may also be a JSON
// array of strings, consumed in order across repeated fetches of the same
// source id (mirrors tools/preview/main.cpp's --doc id=fileA,fileB fixture
// lists for `for_each` sources). An `image` widget (spec §9.10) has no
// source id of its own and is always fetched as id "image", exactly as in
// the native preview.
//
// This file deliberately has no network fetch capability of its own — the
// wasm module is handed already-fetched bodies, exactly as it will be in a
// browser: the site's JS does `fetch()` for each source, then calls
// yat_render() once with the collected bodies. See README.md for why this
// is the recommended real-site integration shape, not just a spike shortcut.
#include <yat/engine.h>
#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 480;

// RGBA canvas: browsers want RGBA (ImageData), so the wasm export carries an
// alpha channel; the identity proof (test-identity.mjs) compares only the RGB
// triples against the native target's RGB buffer. Ink -> color mapping is
// copied byte-for-byte from tools/preview/main.cpp's RgbCanvas palette —
// including its deliberately washed-out yellow, which that file explains. The
// two must stay identical or test-identity.mjs fails, which is the point: the
// gallery in a browser and the golden on disk have to be the same panel.
struct RgbaCanvas : yat::Canvas {
  int w, h;
  std::vector<uint8_t>& px;  // RGBA, external storage (the exported buffer)
  RgbaCanvas(int w_, int h_, std::vector<uint8_t>& buf) : w(w_), h(h_), px(buf) {}
  int width() const override { return w; }
  int height() const override { return h; }
  void drawPixel(int x, int y, yat::Ink ink) override {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    static const unsigned char pal[6][3] = {
        {0x10, 0x10, 0x10}, {0xFF, 0xFF, 0xFF}, {0xC0, 0x1E, 0x28},
        {0xFA, 0xE8, 0x98}, {0x15, 0x70, 0x3B}, {0x1D, 0x4F, 0x91}};
    size_t o = ((size_t)y * w + x) * 4;
    const unsigned char* c = pal[(int)ink];
    px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2]; px[o + 3] = 255;
  }
};

// Static storage so yat_get_buffer()/yat_get_error() can hand back stable
// pointers after yat_render() returns (single-threaded, one render in flight
// at a time -- fine for this spike and for a browser preview's usage pattern).
std::vector<uint8_t> g_buffer(static_cast<size_t>(kWidth) * kHeight * 4, 255);
std::string g_error;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
int yat_render(const char* packJson, const char* paramsJson, const char* docsJson,
                double nowEpoch) {
  g_error.clear();
  g_buffer.assign(static_cast<size_t>(kWidth) * kHeight * 4, 255);  // reset to white/opaque

  yat::Engine eng;
  std::string err;
  const char* pj = (paramsJson && paramsJson[0]) ? paramsJson : nullptr;
  if (!eng.load(packJson, pj, err)) {
    g_error = "load: " + err;
    return 1;
  }

  // Parse docsJson ({"<sourceid>": "<body>"} or {"<sourceid>": ["<body>",...]})
  // into the same per-id fixture-list shape tools/preview/main.cpp uses.
  std::map<std::string, std::vector<std::string>> docs;
  if (docsJson && docsJson[0]) {
    JsonDocument docsDoc;
    DeserializationError derr = deserializeJson(docsDoc, docsJson);
    if (derr) {
      g_error = std::string("docsJson parse: ") + derr.c_str();
      return 2;
    }
    for (JsonPairConst kv : docsDoc.as<JsonObjectConst>()) {
      std::string id = kv.key().c_str();
      JsonVariantConst v = kv.value();
      if (v.is<JsonArrayConst>()) {
        for (JsonVariantConst item : v.as<JsonArrayConst>()) {
          if (item.is<const char*>()) docs[id].push_back(item.as<const char*>());
        }
      } else if (v.is<const char*>()) {
        docs[id].push_back(v.as<const char*>());
      }
    }
  }
  std::map<std::string, size_t> docIdx;

  auto fetch = [&](const std::string& id, const std::string& /*url*/, std::string& body,
                    std::string& ferr) -> bool {
    auto it = docs.find(id);
    if (it == docs.end() || it->second.empty()) {
      ferr = "no fixture for source '" + id + "' (docsJson missing key)";
      return false;
    }
    size_t idx = docIdx[id]++;
    if (idx >= it->second.size()) {
      ferr = "fixture list exhausted for '" + id + "' (iteration " + std::to_string(idx) +
             ", only " + std::to_string(it->second.size()) + " given)";
      return false;
    }
    body = it->second[idx];
    return true;
  };

  time_t now = (time_t)nowEpoch;
  if (!eng.fetchAndExtract(fetch, now, err)) {
    g_error = "extract: " + err;
    return 3;
  }
  if (!err.empty()) g_error = "warn: " + err;

  RgbaCanvas cv(kWidth, kHeight, g_buffer);
  yat::DefaultFontProvider font;
  err.clear();
  if (!eng.render(cv, font, now, err)) {
    if (!g_error.empty()) g_error += " | ";
    g_error += "render: " + err;
    return 4;
  }
  if (!err.empty()) {
    if (!g_error.empty()) g_error += " | ";
    g_error += "render warn: " + err;
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t* yat_get_buffer() { return g_buffer.data(); }

EMSCRIPTEN_KEEPALIVE
int yat_get_width() { return kWidth; }

EMSCRIPTEN_KEEPALIVE
int yat_get_height() { return kHeight; }

EMSCRIPTEN_KEEPALIVE
const char* yat_get_error() { return g_error.c_str(); }

}  // extern "C"
