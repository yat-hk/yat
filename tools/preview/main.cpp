// yat-preview — native CLI target of the YAT engine.
// Renders a .yat-pack.json to PNG using fixture documents (no network) or live
// fetch via curl. Usage:
//   yat-preview pack.json [--doc <sourceid>=<file>]... [--params '<json>']
//               [--live] [--state <dir>] [--battery <pct>]
//               [--out out.png]
#include <yat/engine.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <sys/stat.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// §9.10 `image` widget PNG decode. The engine already enforces "PNG only"
// itself (a magic-byte check in engine/src/render.cpp runs before any decoder
// is invoked), so STBI_ONLY_PNG here is belt-and-suspenders: it also shrinks
// the compiled decoder to just the one format this build ever needs.
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

struct RgbCanvas : yat::Canvas {
  int w, h;
  std::vector<unsigned char> px;  // RGB
  RgbCanvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 3, 255) {}
  int width() const override { return w; }
  int height() const override { return h; }
  void drawPixel(int x, int y, yat::Ink ink) override {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    // What the panel LOOKS like, which is not the same table the engine
    // dithers against (kPalette in engine/src/render.cpp — device truth, and
    // deliberately untouched here). Yellow is the one entry where the two
    // disagree, on purpose: the ink measures ~1.1:1 against this panel's white
    // (§9.1a-a), so a preview that paints it #E8C000 is not previewing the
    // device — it is flattering it, and it flattered two design reviews into
    // shipping a `warn` surface the owner could not see.
    //
    // #FAE898 is 1.23:1 against white — faint enough that a field of it reads
    // as barely-tinted paper, which is the whole point — but it keeps the
    // ink's HUE and saturation rather than fading toward cream. That second
    // property is not cosmetic: the engine now weaves red through yellow to
    // build the §9.1a-a amber, and a desaturated stand-in makes every such mix
    // render pink instead of amber (measured: 1-in-4 red lands at hue 21° from
    // a cream #F4EAC4, 31° from this). Luminance is what the eye loses on the
    // panel; hue is what the weave depends on. Model both or the preview lies
    // in a new direction.
    static const unsigned char pal[6][3] = {
        {0x10, 0x10, 0x10}, {0xFF, 0xFF, 0xFF}, {0xC0, 0x1E, 0x28},
        {0xFA, 0xE8, 0x98}, {0x15, 0x70, 0x3B}, {0x1D, 0x4F, 0x91}};
    size_t o = ((size_t)y * w + x) * 3;
    const unsigned char* c = pal[(int)ink];
    px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2];
  }
  void fillRect(int x, int y, int ww, int hh, yat::Ink ink) override {
    for (int j = y; j < y + hh; j++)
      for (int i = x; i < x + ww; i++) drawPixel(i, j, ink);
  }
};

// §9.10 `image` widget: the native preview's yat::ImageDecoder, backed by
// stb_image (public domain / MIT — see THIRD_PARTY_NOTICES.md). The engine
// itself already PNG-magic-byte-checks and size-caps the input before ever
// calling decode() (engine/src/render.cpp); firmware will wire pngle instead
// (see engine/include/yat/canvas.h's ImageDecoder for the contract).
struct StbImageDecoder : yat::ImageDecoder {
  bool decode(const std::string& png, int& w, int& h, std::vector<uint8_t>& rgb) override {
    int channels = 0;
    unsigned char* px = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(png.data()), (int)png.size(), &w, &h, &channels, 3);
    if (!px) return false;
    rgb.assign(px, px + (size_t)w * h * 3);
    stbi_image_free(px);
    return true;
  }
};

bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// §11.3/§5.1 StateStore backed by one file per key under a directory (the
// native preview's stand-in for firmware NVS/LittleFS). Keys are already
// filesystem-safe ("<packid>.<sourceid>", both schema-constrained to
// lowercase/digits/hyphen/underscore — see docs/PACK-SPEC.md §5/§3).
// Persistence is opt-in via --state <dir>; omitting the flag leaves the
// engine's store pointer null, preserving exact v0.2 behavior (goldens
// unaffected).
struct FileStateStore : yat::StateStore {
  std::string dir;
  explicit FileStateStore(std::string d) : dir(std::move(d)) {}
  std::string pathFor(const char* key) const { return dir + "/" + key + ".json"; }
  bool get(const char* key, std::string& out) override { return readFile(pathFor(key), out); }
  bool put(const char* key, const std::string& val) override {
    std::ofstream f(pathFor(key), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << val;
    return (bool)f;
  }
};

void ensureDir(const std::string& path) {
  ::mkdir(path.c_str(), 0755);  // ignore errors — may already exist
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: yat-preview pack.json [--doc id=file[,file...]]... [--params json] "
            "[--now epoch] [--live] [--state <dir>] [--battery <pct>] [--profile e1002|e1001] "
            "[--out out.png]\n"
            "  --doc id=fileA,fileB,...  fixtures consumed in order per fetch of source "
            "'id' (a for_each source fetches once per array element); an `image` widget "
            "(§9.10) has no source id of its own and always fetches as id 'image' — use "
            "--doc image=some.png\n"
            "  --state <dir>  persist §11.3 stale-serve / §5.1 min_refresh_min snapshots "
            "as one file per source under <dir> (default: no persistence, matching v0.2)\n"
            "  --battery <pct>  G25/§11.4 standard-chrome footer battery glyph, 0..100 "
            "(default: omitted -> unknown -> no glyph at all, matching every prior render)\n"
            "  --profile e1002|e1001  device capability profile to render against (ROADMAP "
            "\"device capability profiles\"; default: e1002, byte-identical to every prior "
            "render). e1001 is the mono reTerminal, same 800x480 — every chromatic ink "
            "(role- or literal-named) draws black and `image` widgets dither to black/white\n"
            "  --notice voice|storage|config  §11.4a host-known ACTION condition — draws the "
            "top banner naming where the fix lives (default: none). The other two tiers have "
            "no flag because the engine derives them: the Info footer glyph from this render's "
            "own E_TYPE/E_BIND/E_ITER, and the Degraded strip from a snapshot 3h+ old (drive it "
            "with --state plus a --now past the interval)\n"
            "-> the pack's own `aliases`)\n");
    return 2;
  }
  std::string packPath = argv[1], out = "out.png", paramsJson;
  time_t pinnedNow = 0;
  std::map<std::string, std::vector<std::string>> docs;
  std::map<std::string, size_t> docIdx;
  bool live = false;
  std::string stateDir;
  int batteryPct = -1;  // -1 = flag omitted = unknown = no footer glyph (see engine.h)
  std::string profileName = "e1002";
  std::string noticeName;  // §11.4a: empty = no host-known condition = nothing drawn
  std::string rawOut;  // optional: raw RGB dump alongside the PNG (--raw), for
                        // byte-exact cross-target (e.g. WASM) identity checks.
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--doc" && i + 1 < argc) {
      std::string kv = argv[++i];
      size_t eq = kv.find('=');
      std::string id = kv.substr(0, eq);
      std::string rest = kv.substr(eq + 1);
      std::vector<std::string> files;
      size_t start = 0;
      while (true) {
        size_t comma = rest.find(',', start);
        files.push_back(rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
      docs[id] = files;
    } else if (a == "--params" && i + 1 < argc) paramsJson = argv[++i];
    else if (a == "--out" && i + 1 < argc) out = argv[++i];
    else if (a == "--now" && i + 1 < argc) pinnedNow = (time_t)atoll(argv[++i]);
    else if (a == "--live") live = true;
    else if (a == "--state" && i + 1 < argc) stateDir = argv[++i];
    else if (a == "--raw" && i + 1 < argc) rawOut = argv[++i];
    else if (a == "--battery" && i + 1 < argc) batteryPct = atoi(argv[++i]);
    else if (a == "--profile" && i + 1 < argc) profileName = argv[++i];
    else if (a == "--notice" && i + 1 < argc) noticeName = argv[++i];
  }

  yat::NoticeCode notice = yat::NoticeCode::None;
  if (!noticeName.empty()) {
    if (noticeName == "voice") notice = yat::NoticeCode::VoiceKeyRejected;
    else if (noticeName == "storage") notice = yat::NoticeCode::StorageFailed;
    else if (noticeName == "config") notice = yat::NoticeCode::ConfigUnreadable;
    else {
      fprintf(stderr, "unknown --notice '%s' (known: voice, storage, config)\n",
              noticeName.c_str());
      return 2;
    }
  }

  yat::DeviceProfile profile;
  if (!yat::deviceProfileFromName(profileName.c_str(), profile)) {
    fprintf(stderr, "unknown --profile '%s' (known: e1002, e1001)\n", profileName.c_str());
    return 2;
  }

  std::string packText, err;
  if (!readFile(packPath, packText)) { fprintf(stderr, "cannot read %s\n", packPath.c_str()); return 2; }

  yat::Engine eng;
  if (!eng.load(packText.c_str(), paramsJson.empty() ? nullptr : paramsJson.c_str(), err)) {
    fprintf(stderr, "load: %s\n", err.c_str());
    return 1;
  }

  std::unique_ptr<FileStateStore> stateStore;
  if (!stateDir.empty()) {
    ensureDir(stateDir);
    stateStore.reset(new FileStateStore(stateDir));
    eng.setStateStore(stateStore.get());
  }

  auto fetch = [&](const std::string& id, const std::string& url, std::string& body,
                   std::string& ferr) -> bool {
    // Printed unconditionally (including the substituted, percent-encoded
    // URL) so tests/authors can grep the actual fetch target — a for_each
    // source logs one line per iteration, in order.
    fprintf(stderr, "fetch %s <- %s\n", id.c_str(), url.c_str());
    auto it = docs.find(id);
    if (it != docs.end()) {
      size_t idx = docIdx[id]++;
      if (idx >= it->second.size()) {
        ferr = "fixture list exhausted for '" + id + "' (iteration " + std::to_string(idx) +
               ", only " + std::to_string(it->second.size()) + " file(s) given)";
        return false;
      }
      const std::string& file = it->second[idx];
      if (!readFile(file, body)) { ferr = "fixture missing: " + file; return false; }
      return true;
    }
    if (live) {
      std::string cmd = "curl -s --max-time 20 '" + url + "'";
      FILE* p = popen(cmd.c_str(), "r");
      if (!p) { ferr = "curl failed"; return false; }
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), p)) > 0) body.append(buf, n);
      pclose(p);
      if (body.empty()) { ferr = "empty response"; return false; }
      return true;
    }
    ferr = "no fixture for source '" + id + "' (use --doc " + id + "=file or --live)";
    return false;
  };

  time_t now = pinnedNow ? pinnedNow : time(nullptr);
  if (!eng.fetchAndExtract(fetch, now, err)) { fprintf(stderr, "extract: %s\n", err.c_str()); return 1; }
  if (!err.empty()) fprintf(stderr, "warn: %s\n", err.c_str());
  if (eng.anyStale()) {
    time_t oldest = eng.oldestStaleSince();
    struct tm* st = localtime(&oldest);
    fprintf(stderr, "stale: serving snapshot(s), oldest since %02d:%02d\n", st->tm_hour, st->tm_min);
  }
  // §11.3 empty state, on its own greppable line for the same reason `stale:`
  // has one: a fleet audit wants to sort renders by outcome without parsing
  // the free-form render-warning text.
  if (eng.allSourcesEmpty())
    fprintf(stderr, "empty: every source failed with no snapshot — render() will draw the fallback card\n");

  // §11.4a, set before the hash is printed because the hash folds it in (the
  // whole point: a notice appearing must not be hash-skipped away).
  eng.setNotice(notice);
  // One greppable line per tier that is actually up this run, for the same
  // reason `stale:`/`empty:` have theirs.
  if (notice != yat::NoticeCode::None) fprintf(stderr, "notice: action (%s)\n", noticeName.c_str());
  if (eng.staleDegraded(now))
    fprintf(stderr, "notice: degraded — a source has been stale-serving for 3h+\n");

  std::string dataStr;
  serializeJsonPretty(eng.data(), dataStr);
  fprintf(stderr, "data: %s\nhash: %08x\n", dataStr.c_str(), eng.dataHash(now));

  // Canvas dimensions come from the profile, not a literal 800x480 — the
  // plumbing a future differently-sized model needs; every model today
  // (e1002, e1001) happens to agree on 800x480 (PRD.md).
  RgbCanvas cv(profile.width, profile.height);
  yat::DefaultFontProvider font;
  StbImageDecoder imgDecoder;
  eng.setImageDecoder(&imgDecoder);
  eng.setBatteryPercent(batteryPct);
  err.clear();
  if (!eng.render(cv, font, now, err, profile)) { fprintf(stderr, "render: %s\n", err.c_str()); return 1; }
  if (!err.empty()) fprintf(stderr, "render warn: %s\n", err.c_str());

  if (!stbi_write_png(out.c_str(), cv.w, cv.h, 3, cv.px.data(), cv.w * 3)) {
    fprintf(stderr, "png write failed\n");
    return 1;
  }
  fprintf(stderr, "wrote %s\n", out.c_str());

  if (!rawOut.empty()) {
    std::ofstream rf(rawOut, std::ios::binary | std::ios::trunc);
    if (!rf) { fprintf(stderr, "raw write failed\n"); return 1; }
    rf.write(reinterpret_cast<const char*>(cv.px.data()), (std::streamsize)cv.px.size());
    if (!rf) { fprintf(stderr, "raw write failed\n"); return 1; }
    fprintf(stderr, "wrote %s (%dx%d RGB, %zu bytes)\n", rawOut.c_str(), cv.w, cv.h, cv.px.size());
  }
  return 0;
}
