// Path expression evaluator — Pack Spec §6.1/§6.2/§6.3/§6.4 (v0.1 subset).
#include <yat/engine.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace yat {
namespace detail {

namespace {

struct Parser {
  const std::string& s;
  size_t i = 0;
  explicit Parser(const std::string& str) : s(str) {}
  bool eof() const { return i >= s.size(); }
  char peek() const { return eof() ? '\0' : s[i]; }
  void skipSpaces() {
    while (!eof() && s[i] == ' ') i++;
  }
};

bool isIdentStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool isIdentChar(char c) { return isIdentStart(c) || (c >= '0' && c <= '9') || c == '-'; }

bool parseIdent(Parser& p, std::string& out) {
  if (!isIdentStart(p.peek())) return false;
  size_t start = p.i;
  while (!p.eof() && isIdentChar(p.s[p.i])) p.i++;
  out = p.s.substr(start, p.i - start);
  return true;
}

bool parseQuotedMember(Parser& p, std::string& out) {  // "name"
  if (p.peek() != '"') return false;
  p.i++;
  out.clear();
  while (!p.eof() && p.s[p.i] != '"') {
    if (p.s[p.i] == '\\' && p.i + 1 < p.s.size()) p.i++;
    out += p.s[p.i++];
  }
  if (p.eof()) return false;
  p.i++;  // closing quote
  return true;
}

bool parseInt(Parser& p, long& out) {
  size_t start = p.i;
  if (p.peek() == '-') p.i++;
  if (!isdigit((unsigned char)p.peek())) { p.i = start; return false; }
  while (isdigit((unsigned char)p.peek())) p.i++;
  out = strtol(p.s.c_str() + start, nullptr, 10);
  return true;
}

// §6.4 comparison of a member value against a single-quoted literal.
bool filterMatch(JsonVariantConst v, const std::string& lit) {
  if (v.is<const char*>()) return lit == v.as<const char*>();
  if (v.is<bool>()) return lit == (v.as<bool>() ? "true" : "false");
  if (v.is<float>() || v.is<double>() || v.is<long>() || v.is<int>()) {
    char* end = nullptr;
    double d = strtod(lit.c_str(), &end);
    if (end == lit.c_str() || *end != '\0') return false;
    return v.as<double>() == d;
  }
  return false;  // null/array/object never match
}

JsonVariantConst indexArray(JsonVariantConst v, long idx) {
  if (!v.is<JsonArrayConst>()) return JsonVariantConst();
  JsonArrayConst a = v.as<JsonArrayConst>();
  long n = (long)a.size();
  if (idx < 0) idx += n;
  if (idx < 0 || idx >= n) return JsonVariantConst();
  return a[(size_t)idx];
}

}  // namespace

bool evalPath(JsonVariantConst doc, const std::string& expr, JsonDocument& out,
              std::string& err) {
  Parser p(expr);
  JsonVariantConst cur = doc;
  bool inProjection = false;
  std::vector<JsonVariantConst> proj;  // elements after a filter

  bool first = true;
  // ---- selector ----
  while (!p.eof()) {
    p.skipSpaces();
    if (p.peek() == '|') break;
    if (!first) {
      if (p.peek() == '.') {
        p.i++;
      } else if (p.peek() != '[') {
        err = "path: expected '.', '[' or '|' at pos " + std::to_string(p.i) + " in '" + expr + "'";
        return false;
      }
    }
    first = false;

    if (p.peek() == '[') {
      if (p.i + 1 < p.s.size() && p.s[p.i + 1] == '?') {
        // filter [?ident=='literal']
        if (inProjection) { err = "path: only one filter per expression"; return false; }
        p.i += 2;
        std::string field;
        if (!parseIdent(p, field)) { err = "path: bad filter field"; return false; }
        p.skipSpaces();
        if (p.s.compare(p.i, 2, "==") != 0) { err = "path: filter needs =="; return false; }
        p.i += 2;
        p.skipSpaces();
        if (p.peek() != '\'') { err = "path: filter literal must be quoted"; return false; }
        p.i++;
        std::string lit;
        while (!p.eof() && p.s[p.i] != '\'') lit += p.s[p.i++];
        if (p.eof()) { err = "path: unterminated literal"; return false; }
        p.i++;
        if (p.peek() != ']') { err = "path: filter missing ]"; return false; }
        p.i++;
        proj.clear();
        if (cur.is<JsonArrayConst>()) {
          for (JsonVariantConst el : cur.as<JsonArrayConst>()) {
            if (el.is<JsonObjectConst>() && filterMatch(el[field.c_str()], lit)) proj.push_back(el);
          }
          inProjection = true;
        } else {
          cur = JsonVariantConst();  // filter on non-array -> null
          inProjection = true;      // (projection over nothing)
          proj.clear();
        }
      } else {
        // index [n]
        p.i++;
        long idx;
        if (!parseInt(p, idx)) { err = "path: bad index"; return false; }
        if (p.peek() != ']') { err = "path: index missing ]"; return false; }
        p.i++;
        if (inProjection) { err = "path: only member steps allowed after a filter"; return false; }
        cur = indexArray(cur, idx);
      }
    } else {
      // member (identifier or quoted)
      std::string name;
      if (p.peek() == '"') {
        if (!parseQuotedMember(p, name)) { err = "path: bad quoted member"; return false; }
      } else if (!parseIdent(p, name)) {
        err = "path: expected member at pos " + std::to_string(p.i) + " in '" + expr + "'";
        return false;
      }
      if (inProjection) {
        std::vector<JsonVariantConst> next;
        for (JsonVariantConst el : proj) {
          JsonVariantConst v =
              el.is<JsonObjectConst>() ? el[name.c_str()] : JsonVariantConst();
          if (!v.isNull()) next.push_back(v);  // drop nulls (§6.3)
        }
        proj = next;
      } else {
        cur = cur.is<JsonObjectConst>() ? cur[name.c_str()] : JsonVariantConst();
      }
    }
  }

  // Collect projection into a working array (owned by out via later copy).
  // We keep everything as JsonVariantConst until pipes need materialization.
  auto materialize = [&](JsonVariant dst) {
    if (inProjection) {
      JsonArray arr = dst.to<JsonArray>();
      for (JsonVariantConst el : proj) arr.add(el);
    } else {
      dst.set(cur);
    }
  };

  // ---- pipes ----
  JsonDocument work;  // holds intermediate array/scalar between stages
  JsonVariant stage = work.to<JsonVariant>();
  materialize(stage);

  int pipeCount = 0;
  while (!p.eof()) {
    p.skipSpaces();
    if (p.peek() != '|') { err = "path: trailing junk in '" + expr + "'"; return false; }
    p.i++;
    p.skipSpaces();
    if (++pipeCount > 2) { err = "path: more than 2 pipe stages"; return false; }

    JsonVariantConst in = stage;
    JsonDocument nextDoc;
    JsonVariant nxt = nextDoc.to<JsonVariant>();

    if (p.peek() == '[') {
      p.i++;
      long idx;
      if (!parseInt(p, idx) || p.peek() != ']') { err = "path: bad pipe index"; return false; }
      p.i++;
      nxt.set(indexArray(in, idx));
    } else {
      std::string fn;
      if (!parseIdent(p, fn)) { err = "path: bad pipe stage"; return false; }
      if (fn == "length") {
        if (in.is<JsonArrayConst>()) nxt.set((long)in.as<JsonArrayConst>().size());
        else if (in.is<const char*>()) {
          // Unicode code points
          const char* s = in.as<const char*>();
          long n = 0;
          for (const unsigned char* q = (const unsigned char*)s; *q; q++)
            if ((*q & 0xC0) != 0x80) n++;
          nxt.set(n);
        } else if (in.is<JsonObjectConst>()) nxt.set((long)in.as<JsonObjectConst>().size());
        // else null
      } else if (fn == "first" || fn == "last") {
        if (p.peek() != '(') { err = "path: " + fn + " needs (n)"; return false; }
        p.i++;
        long n;
        if (!parseInt(p, n) || p.peek() != ')') { err = "path: bad " + fn + " arg"; return false; }
        p.i++;
        if (n < 1 || n > 20) { err = "path: " + fn + " n out of range"; return false; }
        if (in.is<JsonArrayConst>()) {
          JsonArrayConst a = in.as<JsonArrayConst>();
          long len = (long)a.size(), take = n < len ? n : len;
          JsonArray dst = nxt.to<JsonArray>();
          long start = (fn == "first") ? 0 : len - take;
          for (long k = 0; k < take; k++) dst.add(a[(size_t)(start + k)]);
        }
      } else if (fn == "round") {
        if (p.peek() != '(') { err = "path: round needs (n)"; return false; }
        p.i++;
        long n;
        if (!parseInt(p, n) || p.peek() != ')') { err = "path: bad round arg"; return false; }
        p.i++;
        if (n < 0 || n > 6) { err = "path: round n out of range"; return false; }
        if (in.is<float>() || in.is<double>() || in.is<long>() || in.is<int>()) {
          double m = pow(10.0, (double)n);
          double v = in.as<double>();
          double r = (v >= 0 ? floor(v * m + 0.5) : ceil(v * m - 0.5)) / m;
          if (n == 0) nxt.set((long)r);
          else nxt.set(r);
        }
      } else {
        err = "path: unknown pipe stage '" + fn + "'";
        return false;
      }
    }
    work = nextDoc;  // move result into work (deep copy semantics of JsonDocument)
    stage = work.as<JsonVariant>();
  }

  out.set(stage);
  return true;
}

uint32_t fnv1a(const void* buf, size_t len, uint32_t seed) {
  uint32_t h = seed;
  const unsigned char* p = (const unsigned char*)buf;
  for (size_t k = 0; k < len; k++) { h ^= p[k]; h *= 16777619u; }
  return h;
}

}  // namespace detail
}  // namespace yat
