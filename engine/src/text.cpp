// Placeholder substitution (§7) and when-evaluation (§6.5) — v0.2 subset.
#include <yat/engine.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../third_party/s2t_table.h"

namespace yat {
namespace detail {

// Device-local time is FIXED UTC+8 (spec §7.3(3)) — never the host timezone.
// gmtime over a shifted epoch keeps native/WASM/CI renders identical to the
// device regardless of where they run (HK has no DST).
struct tm hkLocal(time_t t) {
  time_t shifted = t + 8 * 3600;
  struct tm out;
#if defined(_WIN32)
  gmtime_s(&out, &shifted);
#else
  gmtime_r(&shifted, &out);
#endif
  return out;
}

namespace {

// ---- UTF-8 helpers (simplified->traditional Chinese) ----
// Encode a UTF-16 code point to UTF-8 bytes.
std::string utf16ToUtf8(uint16_t code) {
  std::string out;
  if (code < 0x80) {
    out += (char)code;
  } else if (code < 0x800) {
    out += (char)(0xC0 | (code >> 6));
    out += (char)(0x80 | (code & 0x3F));
  } else {
    out += (char)(0xE0 | (code >> 12));
    out += (char)(0x80 | ((code >> 6) & 0x3F));
    out += (char)(0x80 | (code & 0x3F));
  }
  return out;
}

// UTF-8 decode: convert UTF-8 bytes at position i to UTF-16 code point.
// Returns next position; code is set to the decoded value or 0xFFFD on error.
size_t utf8Next(const std::string& s, size_t i, uint16_t& code) {
  if (i >= s.size()) { code = 0xFFFD; return i; }
  unsigned char c = s[i];
  if (c < 0x80) { code = c; return i + 1; }
  if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
    code = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
    return i + 2;
  }
  if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
    code = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
    return i + 3;
  }
  if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) { code = 0xFFFD; return i + 4; }
  code = 0xFFFD;
  return i + 1;
}

// Binary search in S2T_FROM table to find mapping to S2T_TO.
// Returns the traditional character code, or the original if not found.
uint16_t s2tLookup(uint16_t simplified) {
  int lo = 0, hi = (int)S2T_N - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (S2T_FROM[mid] == simplified) return S2T_TO[mid];
    if (S2T_FROM[mid] < simplified) lo = mid + 1;
    else hi = mid - 1;
  }
  return simplified;  // not found, pass through unchanged
}

// ---- time helpers ----
bool parseIso(const std::string& s, struct tm& out, bool& hasTime) {
  memset(&out, 0, sizeof(out));
  hasTime = false;
  int y, mo, d, h = 0, mi = 0, se = 0;
  // YYYY-MM-DD[ T]HH:MM[:SS][.frac][offset|Z]  or bare date
  if (sscanf(s.c_str(), "%4d-%2d-%2d", &y, &mo, &d) != 3) return false;
  if (s.size() > 10 && (s[10] == 'T' || s[10] == ' ')) {
    if (sscanf(s.c_str() + 11, "%2d:%2d", &h, &mi) < 2) return false;
    if (s.size() >= 19 && s[16] == ':') sscanf(s.c_str() + 17, "%2d", &se);
    hasTime = true;
    // Offsets: we treat offset-less as device-local (spec §8.3). Timestamps
    // carrying an explicit offset are assumed already device-local for v0.1
    // (HK sources emit +08:00 and the device runs HKT). TODO(v0.2): honor
    // arbitrary offsets by converting through UTC.
  }
  out.tm_year = y - 1900;
  out.tm_mon = mo - 1;
  out.tm_mday = d;
  out.tm_hour = h;
  out.tm_min = mi;
  out.tm_sec = se;
  out.tm_isdst = -1;
  return true;
}

bool valueToTm(JsonVariantConst v, struct tm& out) {
  if (v.is<const char*>()) {
    bool ht;
    return parseIso(v.as<const char*>(), out, ht);
  }
  if (v.is<long>() || v.is<int>() || v.is<double>()) {
    out = hkLocal((time_t)v.as<double>());
    return true;
  }
  return false;
}

// §7.3(4) value-to-text
std::string valueToText(JsonVariantConst v, bool& typeErr) {
  typeErr = false;
  if (v.isNull()) return "";
  if (v.is<const char*>()) return v.as<const char*>();
  if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
  if (v.is<long>() || v.is<int>()) {
    char b[24];
    snprintf(b, sizeof(b), "%ld", (long)v.as<long>());
    return b;
  }
  if (v.is<double>() || v.is<float>()) {
    double d = v.as<double>();
    if (d == floor(d) && fabs(d) < 1e15) {
      char b[24];
      snprintf(b, sizeof(b), "%lld", (long long)d);
      return b;
    }
    char b[32];
    snprintf(b, sizeof(b), "%.6f", d);
    // strip trailing zeros
    std::string s(b);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
  }
  typeErr = true;  // array/object
  return "";
}

// §6.4 scalar equality, used by enum_title's enum lookup (§8.13).
bool scalarEq(JsonVariantConst a, JsonVariantConst b) {
  if (a.is<const char*>() && b.is<const char*>()) return strcmp(a.as<const char*>(), b.as<const char*>()) == 0;
  if (a.is<bool>() && b.is<bool>()) return a.as<bool>() == b.as<bool>();
  bool an = a.is<double>() || a.is<long>() || a.is<int>() || a.is<float>();
  bool bn = b.is<double>() || b.is<long>() || b.is<int>() || b.is<float>();
  if (an && bn) return a.as<double>() == b.as<double>();
  // string vs number: compare numerically when the string parses fully (§6.4)
  if (an && b.is<const char*>()) {
    char* e = nullptr;
    const char* s = b.as<const char*>();
    double d = strtod(s, &e);
    return e != s && *e == '\0' && a.as<double>() == d;
  }
  if (bn && a.is<const char*>()) return scalarEq(b, a);
  return false;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's days_from_civil) —
// the date arithmetic behind pick_by_day (§8.9) and weekday (§8.6).
long daysFromCivil(long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

long localDays(time_t t) {
  struct tm lt = hkLocal(t);
  return daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

// Language resolution shared by strings (§9.13.3), enum_title (§8.13) and
// weekday('auto') (§8.6): en / zh / both (zh-Hant + one space + en).
std::string byLang(const char* lang, const std::string& en, const std::string& zh) {
  if (!lang || strcmp(lang, "en") == 0) return en;
  if (strcmp(lang, "zh") == 0) return zh.empty() ? en : zh;
  if (strcmp(lang, "both") == 0) return zh.empty() ? en : (en.empty() ? zh : zh + " " + en);
  return en;
}

struct FilterCall {
  std::string name;
  std::vector<std::string> args;  // raw args: integers or 'strings' (quotes stripped)
};

// §8 preamble: a wrong-typed (or null) input yields the type-error outcome —
// warn glyph + empty render (typeErr lambda) AND the `typeError` out-flag that
// `evalCompute`'s §7.4(4) typing rule consults.
bool applyFilter(const FilterCall& f, JsonDocument& valDoc, const EvalCtx& ctx, std::string& err,
                 bool& typeError) {
  JsonVariant v = valDoc.as<JsonVariant>();
  auto fail = [&](const char* m) { err = std::string("filter ") + f.name + ": " + m; return false; };
  // §8: wrong input type -> placeholder renders empty, E_TYPE warning, render continues.
  auto typeErr = [&]() {
    addWarn(ctx.warn, "E_TYPE: filter " + f.name + " got a wrong-typed value");
    typeError = true;
    valDoc.set(nullptr);
    return true;
  };
  auto isNum = [](JsonVariantConst x) {
    return x.is<double>() || x.is<long>() || x.is<int>() || x.is<float>();
  };

  if (f.name == "round") {
    if (f.args.size() != 1) return fail("needs (n)");
    int n = atoi(f.args[0].c_str());
    if (!v.is<float>() && !v.is<double>() && !v.is<long>() && !v.is<int>()) return typeErr();
    double m = pow(10.0, n);
    double d = v.as<double>();
    double r = (d >= 0 ? floor(d * m + 0.5) : ceil(d * m - 0.5)) / m;
    char b[40];
    snprintf(b, sizeof(b), "%.*f", n, r);
    valDoc.set(std::string(b));
    return true;
  }
  if (f.name == "pad") {
    if (f.args.size() != 2 || f.args[1].size() < 1) return fail("needs (width,fill)");
    if (v.isNull()) return typeErr();
    bool te;
    std::string s = valueToText(v, te);
    if (te) return typeErr();
    size_t w = (size_t)atoi(f.args[0].c_str());
    // count code points
    size_t cp = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) cp++;
    std::string out;
    while (cp + (out.size() ? 0 : 0) < w && out.size() < 64 && cp < w) { out += f.args[1]; cp++; }
    valDoc.set(out + s);
    return true;
  }
  if (f.name == "time_hhmm") {
    if (v.is<const char*>()) {
      std::string s = v.as<const char*>();
      // bare HHMM
      if (s.size() == 4 && isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
          isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3])) {
        valDoc.set(s.substr(0, 2) + ":" + s.substr(2, 2));
        return true;
      }
    }
    struct tm t;
    if (!valueToTm(v, t)) return typeErr();
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
    valDoc.set(std::string(b));
    return true;
  }
  if (f.name == "date_fmt") {
    if (f.args.size() != 1) return fail("needs (fmt)");
    struct tm t;
    if (!valueToTm(v, t)) return typeErr();
    const std::string& fmt = f.args[0];
    std::string out;
    for (size_t i = 0; i < fmt.size();) {
      auto tok = [&](const char* k) { return fmt.compare(i, strlen(k), k) == 0; };
      char b[8];
      if (tok("YYYY")) { snprintf(b, 8, "%04d", t.tm_year + 1900); out += b; i += 4; }
      else if (tok("MM")) { snprintf(b, 8, "%02d", t.tm_mon + 1); out += b; i += 2; }
      else if (tok("DD")) { snprintf(b, 8, "%02d", t.tm_mday); out += b; i += 2; }
      else if (tok("HH")) { snprintf(b, 8, "%02d", t.tm_hour); out += b; i += 2; }
      else if (tok("mm")) { snprintf(b, 8, "%02d", t.tm_min); out += b; i += 2; }
      else if (tok("ss")) { snprintf(b, 8, "%02d", t.tm_sec); out += b; i += 2; }
      else if (tok("M")) { snprintf(b, 8, "%d", t.tm_mon + 1); out += b; i += 1; }
      else if (tok("D")) { snprintf(b, 8, "%d", t.tm_mday); out += b; i += 1; }
      else out += fmt[i++];
    }
    valDoc.set(out);
    return true;
  }
  if (f.name == "date_diff_days") {
    // §8.5: number of calendar-day boundaries from *today* (device-local,
    // i.e. the spec's fixed +08:00 offset) to the input date.
    struct tm t;
    if (!valueToTm(v, t)) return typeErr();
    long long localNowEpoch = (long long)ctx.now + 8LL * 3600;  // `now` is a UTC time_t
    long long todayDays = (localNowEpoch >= 0) ? (localNowEpoch / 86400) : ((localNowEpoch - 86399) / 86400);
    long long inputDays = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    valDoc.set((long)(inputDays - todayDays));
    return true;
  }
  if (f.name == "upper") {
    if (v.isNull()) return typeErr();
    bool te;
    std::string s = valueToText(v, te);
    if (te) return typeErr();
    for (auto& c : s)
      if (c >= 'a' && c <= 'z') c -= 32;
    valDoc.set(s);
    return true;
  }
  if (f.name == "weekday") {  // §8.6
    if (f.args.size() != 1) return fail("needs (locale)");
    struct tm t;
    if (!valueToTm(v, t)) return typeErr();
    static const char* kEn[7] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                 "Friday", "Saturday", "Sunday"};
    static const char* kShort[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const char* kZh[7] = {"星期一", "星期二", "星期三", "星期四",
                                 "星期五", "星期六", "星期日"};
    long days = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    int dow = (int)(((days + 3) % 7 + 7) % 7);  // 1970-01-01 = Thursday
    const std::string& loc = f.args[0];
    if (loc == "en") valDoc.set(std::string(kEn[dow]));
    else if (loc == "en-short") valDoc.set(std::string(kShort[dow]));
    else if (loc == "zh-Hant") valDoc.set(std::string(kZh[dow]));
    else if (loc == "auto") valDoc.set(byLang(ctx.lang, kEn[dow], kZh[dow]));
    else return fail("locale must be en|en-short|zh-Hant|auto");
    return true;
  }
  if (f.name == "abs") {  // §8.11
    if (!isNum(v)) return typeErr();
    double d = fabs(v.as<double>());
    if (d == floor(d) && d < 1e15) valDoc.set((long)d);
    else valDoc.set(d);
    return true;
  }
  if (f.name == "sign_char") {  // §8.10
    if (!isNum(v)) return typeErr();
    double d = v.as<double>();
    valDoc.set(std::string(d > 0 ? "+" : (d < 0 ? "-" : "")));
    return true;
  }
  if (f.name == "pick_by_day") {  // §8.9 — array -> scalar, daily rotation
    if (!v.is<JsonArrayConst>()) return typeErr();
    JsonArrayConst a = v.as<JsonArrayConst>();
    if (a.size() == 0) { valDoc.set(nullptr); return true; }  // empty -> null, not an error
    long idx = localDays(ctx.now) % (long)a.size();
    if (idx < 0) idx += (long)a.size();
    JsonDocument pick;
    pick.set(a[(size_t)idx]);
    valDoc = pick;
    return true;
  }
  if (f.name == "enum_title") {  // §8.13
    if (f.args.size() != 1) return fail("needs ('param')");
    JsonVariantConst prop = ctx.pack["params"]["properties"][f.args[0].c_str()];
    JsonArrayConst en = prop["enum"].as<JsonArrayConst>();
    JsonArrayConst titles = prop["enum_titles"].as<JsonArrayConst>();
    if (en.isNull() || titles.isNull()) {
      // Array-typed params (a for_each multi-select, §3.3(4)/(5)) declare
      // enum/enum_titles under `items`, not on the property itself — the
      // canonical enum_title use case (§8.13): "{{item.each|enum_title(...)}}"
      // labelling for_each rows by their source enum value.
      en = prop["items"]["enum"].as<JsonArrayConst>();
      titles = prop["items"]["enum_titles"].as<JsonArrayConst>();
    }
    if (en.isNull() || titles.isNull())
      return fail(("param '" + f.args[0] + "' declares no enum/enum_titles").c_str());
    for (size_t k = 0; k < en.size() && k < titles.size(); k++) {
      if (!scalarEq(en[k], v)) continue;
      JsonVariantConst tt = titles[k];
      valDoc.set(byLang(ctx.lang, tt["en"] | "", tt["zh-Hant"] | ""));
      return true;
    }
    return true;  // no match -> pass the input through unchanged (drift-safe)
  }
  if (f.name == "s2t") {
    // Simplified -> Traditional Chinese conversion (§8.7)
    if (v.isNull()) return typeErr();
    bool te;
    std::string s = valueToText(v, te);
    if (te) return typeErr();
    // Walk UTF-8 string, decode each code point, look it up, re-encode
    std::string out;
    for (size_t i = 0; i < s.size();) {
      uint16_t code;
      i = utf8Next(s, i, code);
      uint16_t mapped = s2tLookup(code);
      out += utf16ToUtf8(mapped);
    }
    valDoc.set(out);
    return true;
  }
  if (f.name == "count") {  // §8.12: array -> number (array->scalar class)
    if (!v.is<JsonArrayConst>()) return typeErr();
    valDoc.set((long)v.as<JsonArrayConst>().size());
    return true;
  }
  return fail("unknown filter");
}

// Parse "{{ref|f(a,'b')|g}}" starting after "{{"; returns position after "}}".
bool parsePlaceholder(const std::string& s, size_t start, size_t& endOut, std::string& ref,
                      std::vector<FilterCall>& filters, std::string& err) {
  size_t close = s.find("}}", start);
  if (close == std::string::npos) { err = "unterminated placeholder"; return false; }
  std::string inner = s.substr(start, close - start);
  endOut = close + 2;
  filters.clear();
  size_t p = 0;
  auto nextPipe = [&](size_t from) {
    bool q = false;
    for (size_t k = from; k < inner.size(); k++) {
      if (inner[k] == '\'') q = !q;
      else if (inner[k] == '|' && !q) return k;
    }
    return inner.size();
  };
  size_t bar = nextPipe(0);
  ref = inner.substr(0, bar);
  p = bar;
  while (p < inner.size()) {
    p++;  // skip |
    size_t e = nextPipe(p);
    std::string call = inner.substr(p, e - p);
    FilterCall fc;
    size_t par = call.find('(');
    if (par == std::string::npos) {
      fc.name = call;
    } else {
      fc.name = call.substr(0, par);
      if (call.back() != ')') { err = "bad filter call " + call; return false; }
      std::string args = call.substr(par + 1, call.size() - par - 2);
      // split on commas outside quotes
      std::string curArg;
      bool q = false;
      for (char c : args) {
        if (c == '\'') { q = !q; continue; }
        if (c == ',' && !q) { fc.args.push_back(curArg); curArg.clear(); continue; }
        curArg += c;
      }
      fc.args.push_back(curArg);
    }
    filters.push_back(fc);
    p = e;
  }
  return true;
}

// Walk ".name" / "[n]" steps (§6.5 context grammar, §7.1 render-string steps).
// Misses yield null; ok=false only on a grammar error.
JsonVariantConst walkSteps(JsonVariantConst cur, const std::string& path, size_t p, bool& ok) {
  ok = true;
  while (p < path.size()) {
    if (path[p] == '.') {
      p++;
      size_t e = p;
      while (e < path.size() && path[e] != '.' && path[e] != '[') e++;
      if (e == p) { ok = false; return JsonVariantConst(); }
      std::string name = path.substr(p, e - p);
      cur = cur.is<JsonObjectConst>() ? cur[name.c_str()] : JsonVariantConst();
      p = e;
    } else if (path[p] == '[') {
      size_t e = path.find(']', p);
      if (e == std::string::npos) { ok = false; return JsonVariantConst(); }
      long idx = strtol(path.c_str() + p + 1, nullptr, 10);
      if (cur.is<JsonArrayConst>()) {
        JsonArrayConst a = cur.as<JsonArrayConst>();
        long n = (long)a.size();
        if (idx < 0) idx += n;
        cur = (idx >= 0 && idx < n) ? a[(size_t)idx] : JsonVariantConst();
      } else {
        cur = JsonVariantConst();
      }
      p = e + 1;
    } else {
      ok = false;
      return JsonVariantConst();
    }
  }
  return cur;
}

// §7.1 references (also the §6.5 `when` roots): params/data/item/index/
// strings/now. Position legality comes from the context (§7.2): `item`/`index`
// need ctx.inRow, `strings` needs ctx.strings.
bool resolveRef(const std::string& ref, const EvalCtx& ctx, JsonDocument& out, std::string& err) {
  if (ref == "now") {
    struct tm lt = hkLocal(ctx.now);
    char b[40];
    snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02d+08:00", lt.tm_year + 1900,
             lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    out.set(std::string(b));
    return true;
  }
  size_t brk = ref.find_first_of(".[");
  std::string head = brk == std::string::npos ? ref : ref.substr(0, brk);
  size_t stepsAt = brk == std::string::npos ? ref.size() : brk;

  if (head == "index") {  // §7.1: 1-based row number, no steps
    if (!ctx.inRow) { err = "'{{index}}' is legal only inside a list row (§7.1/§9.8)"; return false; }
    if (stepsAt != ref.size()) { err = "'index' takes no steps (§6.5)"; return false; }
    out.set(ctx.index);
    return true;
  }
  if (head == "strings") {  // §9.13
    if (!ctx.strings.is<JsonObjectConst>() || stepsAt == ref.size() || ref[stepsAt] != '.') {
      err = "'{{" + ref +
            "}}': strings are legal only in render strings of a pack declaring a `strings` table "
            "(§7.2/§9.13)";
      return false;
    }
    std::string key = ref.substr(stepsAt + 1);
    JsonVariantConst e = ctx.strings[key.c_str()];
    if (!e.is<JsonObjectConst>()) { err = "strings: unknown key '" + key + "' (§9.13)"; return false; }
    out.set(byLang(ctx.lang, e["en"] | "", e["zh-Hant"] | ""));
    return true;
  }

  JsonVariantConst root;
  if (head == "params") root = ctx.params;
  else if (head == "data") root = ctx.data;
  else if (head == "item") {  // §9.8 row binding
    if (!ctx.inRow) { err = "'{{item…}}' is legal only inside a list row (§7.1/§9.8)"; return false; }
    root = ctx.item;
  } else if (head == "sources") {  // §5.7: source-only, always bare, id.field
    if (!ctx.sources.is<JsonObjectConst>()) {
      err =
          "'{{sources.<id>.<field>}}' is legal only in a later source's url path/query or "
          "extract filter literals (§5.7); illegal in this position";
      return false;
    }
    if (stepsAt == ref.size() || ref[stepsAt] != '.') {
      err = "'{{sources...}}' needs '<id>.<field>' (§5.7)";
      return false;
    }
    std::string rest = ref.substr(stepsAt + 1);
    if (rest.find('[') != std::string::npos) {
      err = "'{{sources.<id>.<field>}}' takes no index steps (§5.7)";
      return false;
    }
    size_t dot = rest.find('.');
    if (dot == std::string::npos || rest.find('.', dot + 1) != std::string::npos) {
      err = "'{{sources.<id>.<field>}}' takes exactly two steps, id and field — no more, no "
            "fewer (§5.7)";
      return false;
    }
    std::string id = rest.substr(0, dot);
    std::string field = rest.substr(dot + 1);
    if (id.empty() || field.empty()) {
      err = "'{{sources.<id>.<field>}}' needs a non-empty id and field (§5.7)";
      return false;
    }
    JsonVariantConst srcFields = ctx.sources[id.c_str()];
    if (!srcFields.is<JsonObjectConst>()) {
      err = "'{{sources." + id +
            "...}}': '" + id +
            "' is not an earlier source in this pack — self and forward references are illegal "
            "(§5.7)";
      return false;
    }
    out.set(srcFields[field.c_str()]);
    return true;
  } else if (head == "each") {  // §5.1a: for_each source scope only
    if (ctx.each.isNull()) {
      err = "'{{each…}}' is legal only inside a for_each source (§5.1a/§7.2)";
      return false;
    }
    if (stepsAt == ref.size()) { out.set(ctx.each); return true; }
    if (ref[stepsAt] != '.') { err = "each takes dot access only (§5.1a)"; return false; }
    {
      std::string prop = ref.substr(stepsAt + 1);
      if (prop.find('.') != std::string::npos || prop.find('[') != std::string::npos) {
        err = "each.<prop> is one level only (§5.1a)";
        return false;
      }
      out.set(ctx.each.is<JsonObjectConst>() ? ctx.each[prop.c_str()] : JsonVariantConst());
    }
    return true;
  } else if (head == "secrets") {
    err = "'" + head + ".*' is not legal in this position (§7.2)";
    return false;
  } else {
    err = "reference '" + head + "' not supported by this engine build";
    return false;
  }
  if (head != "item" && stepsAt == ref.size()) {
    err = "bare '" + head + "' is not a reference";
    return false;
  }
  bool ok = true;
  JsonVariantConst v = walkSteps(root, ref, stepsAt, ok);
  if (!ok) { err = "bad reference '" + ref + "' (§7.1)"; return false; }
  out.set(v);
  return true;
}

// §7.3(2): URL path/query placeholder values are RFC 3986 percent-encoded;
// unreserved chars (ALPHA / DIGIT / "-" / "." / "_" / "~") pass through.
// Applies only to the text produced by a *placeholder*, never to literal
// template text around it.
std::string percentEncodeValue(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
    if (unreserved) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

}  // namespace

void addWarn(std::string* sink, const std::string& msg) {
  if (!sink) return;
  if (sink->find(msg) != std::string::npos) return;  // de-dup: measure runs repeat
  if (!sink->empty()) *sink += "; ";
  *sink += msg;
}

const char* langValue(JsonVariantConst pack, JsonVariantConst params) {
  const char* lp = pack["render"]["lang_param"] | (const char*)nullptr;
  if (!lp) return nullptr;
  JsonVariantConst v = params[lp];
  return v.is<const char*>() ? v.as<const char*>() : nullptr;
}

bool substitute(const std::string& tpl, const EvalCtx& ctx, std::string& out, std::string& err,
                bool urlEncode, bool* hadTypeError, bool* hadNullSourceRef) {
  out.clear();
  bool anyTypeError = false;
  size_t i = 0;
  while (i < tpl.size()) {
    size_t open = tpl.find("{{", i);
    if (open == std::string::npos) { out += tpl.substr(i); break; }
    out += tpl.substr(i, open - i);
    std::string ref;
    std::vector<FilterCall> filters;
    size_t after;
    if (!parsePlaceholder(tpl, open + 2, after, ref, filters, err)) return false;
    JsonDocument val;
    if (!resolveRef(ref, ctx, val, err)) return false;
    // §5.7 rule 1: sources refs are always bare (no filters); rule 3: a null
    // referent means the *referencing source* counts as failed (E_REF_NULL) —
    // signaled to the caller, substitution itself still completes.
    if (ref.compare(0, 8, "sources.") == 0) {
      if (!filters.empty()) {
        err = "'{{sources.*}}' placeholders take no filters (§5.7)";
        return false;
      }
      if (val.as<JsonVariantConst>().isNull() && hadNullSourceRef) *hadNullSourceRef = true;
    }
    // §8 preamble: feeding null into a filter is itself a type-error outcome.
    if (!filters.empty() && val.as<JsonVariantConst>().isNull()) anyTypeError = true;
    for (auto& f : filters) {
      bool te = false;
      if (!applyFilter(f, val, ctx, err, te)) return false;
      if (te) anyTypeError = true;
    }
    bool typeErr = false;
    std::string piece = valueToText(val.as<JsonVariantConst>(), typeErr);
    if (typeErr) {
      anyTypeError = true;
      // §7.3(4)/§11.3: array/object in a render string -> empty + warning glyph
      addWarn(ctx.warn, "E_TYPE: '{{" + ref + "}}' is an array/object");
    }
    out += urlEncode ? percentEncodeValue(piece) : piece;
    i = after;
  }
  if (hadTypeError) *hadTypeError = anyTypeError;
  return true;
}

// §7.4 `compute`: see engine.h for the full contract.
bool evalCompute(const std::string& tpl, const EvalCtx& ctx, JsonDocument& out,
                 std::string& err) {
  // "exactly one placeholder, no surrounding text" (§7.4(4)): the template
  // must both start with "{{" and end with "}}", AND that opening span's
  // matching close must land exactly at the end of the string (rules out
  // "{{a}}{{b}}", which also starts/ends with the marker text).
  bool looksWhole = tpl.size() >= 4 && tpl.compare(0, 2, "{{") == 0 &&
                    tpl.compare(tpl.size() - 2, 2, "}}") == 0;
  if (looksWhole) {
    std::string ref;
    std::vector<FilterCall> filters;
    size_t after;
    std::string perr;
    if (parsePlaceholder(tpl, 2, after, ref, filters, perr) && after == tpl.size()) {
      JsonDocument val;
      if (!resolveRef(ref, ctx, val, err)) return false;
      bool anyTypeError = !filters.empty() && val.as<JsonVariantConst>().isNull();
      for (auto& f : filters) {
        bool te = false;
        if (!applyFilter(f, val, ctx, err, te)) return false;
        if (te) anyTypeError = true;
      }
      if (anyTypeError) { out.set(nullptr); return true; }
      JsonVariant v = val.as<JsonVariant>();
      if (v.is<double>() || v.is<float>() || v.is<long>() || v.is<int>()) {
        out.set(v);  // §7.4(4): single-placeholder numeric result -> number
        return true;
      }
      bool typeErr = false;
      std::string text = valueToText(v, typeErr);
      if (typeErr) { out.set(nullptr); return true; }
      out.set(text);
      return true;
    }
    // Didn't parse as one self-contained placeholder (e.g. a stray "}}"
    // earlier in the string) — fall through to the general template path,
    // which will surface the real grammar error via `substitute`.
  }
  bool hadTypeError = false;
  std::string text;
  if (!substitute(tpl, ctx, text, err, false, &hadTypeError)) return false;
  if (hadTypeError) { out.set(nullptr); return true; }
  out.set(text);
  return true;
}

// ---- when ----
namespace {

bool truthy(JsonVariantConst v) {
  if (v.isNull()) return false;
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<double>() || v.is<long>() || v.is<int>() || v.is<float>()) return v.as<double>() != 0;
  if (v.is<const char*>()) return v.as<const char*>()[0] != '\0';
  if (v.is<JsonArrayConst>()) return v.as<JsonArrayConst>().size() > 0;
  if (v.is<JsonObjectConst>()) return v.as<JsonObjectConst>().size() > 0;
  return false;
}

bool evalOneWhen(const std::string& expr, const EvalCtx& ctx, bool& visible, std::string& err) {
  size_t i = 0;
  while (i < expr.size() && expr[i] != ' ') i++;
  std::string path = expr.substr(0, i);
  // §6.5 roots: data | params | item | index (no now/strings/secrets here).
  std::string head = path.substr(0, path.find_first_of(".["));
  if (head != "data" && head != "params" && head != "item" && head != "index") {
    err = "when: unsupported root '" + head + "' in '" + expr + "' (§6.5)";
    return false;
  }
  JsonDocument valDoc;
  if (!resolveRef(path, ctx, valDoc, err)) { err = "when: " + err; return false; }
  JsonVariantConst v = valDoc.as<JsonVariantConst>();
  while (i < expr.size() && expr[i] == ' ') i++;
  if (i >= expr.size()) { visible = truthy(v); return true; }

  // unary keywords
  if (expr.compare(i, 6, "exists") == 0) { visible = !v.isNull(); return true; }
  if (expr.compare(i, 7, "missing") == 0) { visible = v.isNull(); return true; }

  // comparator
  std::string op;
  for (const char* c : {"==", "!=", "<=", ">=", "<", ">", "has"})
    if (expr.compare(i, strlen(c), c) == 0) { op = c; break; }
  if (op.empty()) { err = "when: bad comparator in '" + expr + "'"; return false; }
  i += op.size();
  while (i < expr.size() && expr[i] == ' ') i++;
  if (i >= expr.size() || expr[i] != '\'') { err = "when: literal must be quoted"; return false; }
  std::string lit = expr.substr(i + 1, expr.rfind('\'') - i - 1);

  if (op == "has") {  // §6.5 membership over an array context value
    visible = false;
    if (v.is<JsonArrayConst>()) {
      JsonDocument litDoc;
      litDoc.set(lit);
      for (JsonVariantConst el : v.as<JsonArrayConst>())
        if (scalarEq(el, litDoc.as<JsonVariantConst>())) { visible = true; break; }
    }
    return true;
  }
  if (op == "==" || op == "!=") {
    bool m = false;
    if (v.is<const char*>()) m = lit == v.as<const char*>();
    else if (v.is<bool>()) m = lit == (v.as<bool>() ? "true" : "false");
    else if (v.is<double>() || v.is<long>() || v.is<int>() || v.is<float>()) {
      char* end = nullptr;
      double d = strtod(lit.c_str(), &end);
      m = (end != lit.c_str() && *end == '\0' && v.as<double>() == d);
    }
    visible = (op == "==") ? m : !m;
    return true;
  }
  // ordering: numeric only; fully-numeric strings coerce (spec 0.3)
  double lv = 0, rv = 0;
  bool numeric = false;
  if (v.is<double>() || v.is<long>() || v.is<int>() || v.is<float>()) { lv = v.as<double>(); numeric = true; }
  else if (v.is<const char*>()) {
    char* end = nullptr;
    lv = strtod(v.as<const char*>(), &end);
    numeric = (end != v.as<const char*>() && *end == '\0');
  }
  {
    char* end = nullptr;
    rv = strtod(lit.c_str(), &end);
    if (end == lit.c_str() || *end != '\0') numeric = false;
  }
  if (!numeric) { visible = false; return true; }
  if (op == "<") visible = lv < rv;
  else if (op == "<=") visible = lv <= rv;
  else if (op == ">") visible = lv > rv;
  else visible = lv >= rv;
  return true;
}

}  // namespace

bool evalWhen(JsonVariantConst whenNode, const EvalCtx& ctx, bool& visible, std::string& err) {
  visible = true;
  if (whenNode.isNull()) return true;
  if (whenNode.is<const char*>()) {
    // Substitute {{params.*}} / {{data.<field>}} / {{now|…}} literals first (§6.5).
    std::string expr;
    if (!substitute(whenNode.as<const char*>(), ctx, expr, err)) return false;
    return evalOneWhen(expr, ctx, visible, err);
  }
  if (whenNode.is<JsonObjectConst>()) {
    JsonObjectConst o = whenNode.as<JsonObjectConst>();
    bool isAny = !o["any"].isNull();
    JsonArrayConst arr = (isAny ? o["any"] : o["all"]).as<JsonArrayConst>();
    if (arr.isNull()) { err = "when: object form needs any/all"; return false; }
    visible = !isAny;
    for (JsonVariantConst e : arr) {
      bool one;
      std::string expr;
      // A schema-valid pack's any/all entries are always strings, but the
      // schema is a load-time gate, not a guarantee this function can lean
      // on internally — e.as<const char*>() returns nullptr for a non-string
      // element, and substitute() would then construct a std::string from
      // that nullptr (UB, crashes) rather than surfacing a grammar error.
      if (!e.is<const char*>()) { err = "when: any/all entries must be strings"; return false; }
      if (!substitute(e.as<const char*>(), ctx, expr, err)) return false;
      if (!evalOneWhen(expr, ctx, one, err)) return false;
      if (isAny && one) { visible = true; return true; }
      if (!isAny && !one) { visible = false; return true; }
    }
    return true;
  }
  err = "when: bad node type";
  return false;
}

}  // namespace detail
}  // namespace yat
