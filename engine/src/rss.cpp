// RSS 2.0 / Atom parser — Pack Spec §5.4. Normalizes either feed shape into
// the canonical document {feed:{title}, items:[{title,link,description,
// pub_date}]} that extraction expressions run against. Deliberately NOT a
// general XML parser: a small tolerant tag scanner that only recognizes
// channel/item (RSS) or feed/entry (Atom), plus the handful of child
// elements the spec names (title/link/description/summary/content/pubDate/
// published/updated). No external XML library, per the spec's engine
// footprint constraints.
#include <yat/engine.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace yat {
namespace detail {

namespace {

// ---------------------------------------------------------------------
// Minimal tag scanner
// ---------------------------------------------------------------------

std::string lower(const std::string& s) {
  std::string o = s;
  for (auto& c : o)
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
  return o;
}

bool isNameChar(char c) {
  return isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':';
}

// Finds the next start tag named `nameLower` (case-insensitive) at or after
// `from`, skipping comments/declarations/closing tags encountered along the
// way. On success: tagStart = index of '<'; tagEnd = index just after the
// tag's closing '>'; attrs = raw text between the tag name and '>' (for
// attribute lookups, e.g. Atom's <link href="...">); selfClosing = true iff
// the tag ends "/>".
bool findOpenTag(const std::string& doc, const std::string& nameLower, size_t from,
                 size_t& tagStart, size_t& tagEnd, std::string& attrs, bool& selfClosing) {
  size_t pos = from;
  while (true) {
    pos = doc.find('<', pos);
    if (pos == std::string::npos) return false;
    if (pos + 1 >= doc.size()) return false;
    char n1 = doc[pos + 1];
    if (n1 == '/' || n1 == '!' || n1 == '?') { pos++; continue; }  // close/comment/decl/pi
    size_t nameStart = pos + 1;
    size_t nameEnd = nameStart;
    while (nameEnd < doc.size() && isNameChar(doc[nameEnd])) nameEnd++;
    std::string tagName = lower(doc.substr(nameStart, nameEnd - nameStart));
    // Scan to the matching '>', honoring quoted attribute values so a '>'
    // inside e.g. href="a>b" doesn't terminate the tag early.
    size_t scan = nameEnd;
    bool inQ = false;
    char qc = 0;
    while (scan < doc.size()) {
      char c = doc[scan];
      if (inQ) {
        if (c == qc) inQ = false;
      } else if (c == '"' || c == '\'') {
        inQ = true;
        qc = c;
      } else if (c == '>') {
        break;
      }
      scan++;
    }
    if (scan >= doc.size()) return false;  // unterminated tag — bail tolerantly
    bool self = scan > nameEnd && doc[scan - 1] == '/';
    if (tagName == nameLower) {
      tagStart = pos;
      attrs = doc.substr(nameEnd, (self ? scan - 1 : scan) - nameEnd);
      tagEnd = scan + 1;
      selfClosing = self;
      return true;
    }
    pos = scan + 1;
  }
}

// Finds "</nameLower>" (case-insensitive) at or after `from`.
bool findCloseTag(const std::string& doc, const std::string& nameLower, size_t from,
                  size_t& closeStart, size_t& closeEnd) {
  size_t pos = from;
  while (true) {
    pos = doc.find("</", pos);
    if (pos == std::string::npos) return false;
    size_t nameStart = pos + 2;
    size_t nameEnd = nameStart;
    while (nameEnd < doc.size() && isNameChar(doc[nameEnd])) nameEnd++;
    std::string tagName = lower(doc.substr(nameStart, nameEnd - nameStart));
    size_t gt = doc.find('>', nameEnd);
    if (gt == std::string::npos) return false;
    if (tagName == nameLower) { closeStart = pos; closeEnd = gt + 1; return true; }
    pos = gt + 1;
  }
}

// Locates one <nameLower ...>...</nameLower> (or self-closing) block at or
// after `from`. openTagStart is the '<' of the opening tag (useful as a
// tolerant upper bound for the enclosing scope's own child search); the
// block's content spans [innerStart, innerEnd); `nextPos` resumes scanning
// right after the block.
bool findBlock(const std::string& doc, const std::string& nameLower, size_t from,
              size_t& openTagStart, size_t& innerStart, size_t& innerEnd, size_t& nextPos) {
  size_t tagStart, tagEnd;
  std::string attrs;
  bool selfClosing;
  if (!findOpenTag(doc, nameLower, from, tagStart, tagEnd, attrs, selfClosing)) return false;
  openTagStart = tagStart;
  if (selfClosing) { innerStart = innerEnd = tagEnd; nextPos = tagEnd; return true; }
  size_t closeStart, closeEnd;
  if (!findCloseTag(doc, nameLower, tagEnd, closeStart, closeEnd)) {
    // Tolerant: no closing tag found — treat the remainder as the content.
    innerStart = tagEnd;
    innerEnd = doc.size();
    nextPos = doc.size();
    return true;
  }
  innerStart = tagEnd;
  innerEnd = closeStart;
  nextPos = closeEnd;
  return true;
}

// Extracts the raw inner text of the first <nameLower>...</nameLower> (or
// empty for a self-closing tag) found within [from, limit). `attrsOut`
// carries the opening tag's raw attribute text (used for Atom's <link
// href="...">). Returns false if not found before `limit`.
bool extractElement(const std::string& doc, const std::string& nameLower, size_t from, size_t limit,
                    std::string& innerOut, std::string& attrsOut) {
  size_t tagStart, tagEnd;
  bool selfClosing;
  if (!findOpenTag(doc, nameLower, from, tagStart, tagEnd, attrsOut, selfClosing)) return false;
  if (tagStart >= limit) return false;
  if (selfClosing) { innerOut.clear(); return true; }
  size_t closeStart, closeEnd;
  if (!findCloseTag(doc, nameLower, tagEnd, closeStart, closeEnd) || closeStart > limit) {
    size_t end = limit < doc.size() ? limit : doc.size();
    innerOut = tagEnd < end ? doc.substr(tagEnd, end - tagEnd) : std::string();
    return true;
  }
  innerOut = doc.substr(tagEnd, closeStart - tagEnd);
  return true;
}

// name/value attribute lookup within a tag's raw attribute text.
std::string extractAttr(const std::string& attrs, const std::string& nameLowerWanted) {
  size_t i = 0;
  while (i < attrs.size()) {
    while (i < attrs.size() && isspace((unsigned char)attrs[i])) i++;
    size_t ns = i;
    while (i < attrs.size() && isNameChar(attrs[i])) i++;
    if (i == ns) break;  // no progress — malformed remainder, stop
    std::string name = lower(attrs.substr(ns, i - ns));
    while (i < attrs.size() && isspace((unsigned char)attrs[i])) i++;
    std::string val;
    if (i < attrs.size() && attrs[i] == '=') {
      i++;
      while (i < attrs.size() && isspace((unsigned char)attrs[i])) i++;
      if (i < attrs.size() && (attrs[i] == '"' || attrs[i] == '\'')) {
        char q = attrs[i++];
        size_t vs = i;
        while (i < attrs.size() && attrs[i] != q) i++;
        val = attrs.substr(vs, i - vs);
        if (i < attrs.size()) i++;
      } else {
        size_t vs = i;
        while (i < attrs.size() && !isspace((unsigned char)attrs[i])) i++;
        val = attrs.substr(vs, i - vs);
      }
    }
    if (name == nameLowerWanted) return val;
  }
  return "";
}

// Atom entries may carry several <link> elements (rel="alternate",
// rel="self", ...). RSS items have exactly one plain-text <link>...</link>.
// Prefers rel="alternate" (or an unspecified rel, RSS's case); falls back to
// the first link seen with any rel.
std::string extractLink(const std::string& doc, size_t start, size_t end) {
  size_t pos = start;
  std::string fallback;
  bool haveFallback = false;
  while (pos < end) {
    size_t tagStart, tagEnd;
    std::string attrs;
    bool selfClosing;
    if (!findOpenTag(doc, "link", pos, tagStart, tagEnd, attrs, selfClosing) || tagStart >= end) break;
    std::string href = extractAttr(attrs, "href");
    std::string rel = lower(extractAttr(attrs, "rel"));
    std::string content;
    size_t nextPos = tagEnd;
    if (!selfClosing) {
      size_t closeStart, closeEnd;
      if (findCloseTag(doc, "link", tagEnd, closeStart, closeEnd) && closeStart <= end) {
        content = doc.substr(tagEnd, closeStart - tagEnd);
        nextPos = closeEnd;
      }
    }
    std::string candidate = !href.empty() ? href : content;
    if (!candidate.empty()) {
      if (rel.empty() || rel == "alternate") return candidate;  // cleaned by caller
      if (!haveFallback) { fallback = candidate; haveFallback = true; }
    }
    pos = nextPos > pos ? nextPos : pos + 1;
  }
  return fallback;
}

// ---------------------------------------------------------------------
// Text cleanup: CDATA unwrap -> tag strip -> entity decode -> whitespace
// ---------------------------------------------------------------------

void appendUtf8(std::string& out, unsigned long cp) {
  if (cp <= 0x7F) {
    out += (char)cp;
  } else if (cp <= 0x7FF) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

// Unwraps one or more <![CDATA[ ... ]]> spans, concatenating their raw
// content (and any text between/around them — feeds rarely mix the two, but
// this keeps the transform total rather than partial).
std::string unwrapCdata(const std::string& raw) {
  if (raw.find("<![CDATA[") == std::string::npos) return raw;
  std::string out;
  size_t pos = 0;
  while (true) {
    size_t cs = raw.find("<![CDATA[", pos);
    if (cs == std::string::npos) { out += raw.substr(pos); break; }
    out += raw.substr(pos, cs - pos);
    size_t ce = raw.find("]]>", cs + 9);
    if (ce == std::string::npos) { out += raw.substr(cs + 9); break; }
    out += raw.substr(cs + 9, ce - (cs + 9));
    pos = ce + 3;
  }
  return out;
}

// Decodes &amp; &lt; &gt; &quot; &apos; and numeric refs &#NNN; / &#xHH;.
// Unknown/malformed entities pass through literally (tolerant, per spec:
// "there is no escape mechanism" is a v1 *pack-authoring* rule, not a
// mandate to reject hostile fetched bytes here).
std::string decodeEntities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '&') {
      size_t semi = s.find(';', i + 1);
      if (semi != std::string::npos && semi - i <= 10) {
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") { out += '&'; i = semi + 1; continue; }
        if (ent == "lt") { out += '<'; i = semi + 1; continue; }
        if (ent == "gt") { out += '>'; i = semi + 1; continue; }
        if (ent == "quot") { out += '"'; i = semi + 1; continue; }
        if (ent == "apos") { out += '\''; i = semi + 1; continue; }
        if (ent.size() > 1 && ent[0] == '#') {
          char* end = nullptr;
          long cp;
          bool ok;
          if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
            cp = strtol(ent.c_str() + 2, &end, 16);
            ok = end && *end == '\0' && end != ent.c_str() + 2;
          } else {
            cp = strtol(ent.c_str() + 1, &end, 10);
            ok = end && *end == '\0' && end != ent.c_str() + 1;
          }
          if (ok && cp > 0) { appendUtf8(out, (unsigned long)cp); i = semi + 1; continue; }
        }
      }
    }
    out += s[i++];
  }
  return out;
}

// Replaces every "<...>" span with a single space (so stripped inline tags
// like <br/> don't glue adjacent words together), then collapses runs of
// whitespace to one space and trims the ends.
std::string stripTagsAndCollapse(const std::string& s) {
  std::string noTags;
  noTags.reserve(s.size());
  bool inTag = false;
  for (char c : s) {
    if (inTag) { if (c == '>') { inTag = false; noTags += ' '; } continue; }
    if (c == '<') { inTag = true; continue; }
    noTags += c;
  }
  std::string out;
  bool lastSpace = true;  // swallow leading whitespace
  for (char c : noTags) {
    bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    if (isSpace) {
      if (!lastSpace) out += ' ';
      lastSpace = true;
    } else {
      out += c;
      lastSpace = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

// Order matters: unwrap CDATA first (its content is literal), strip real
// markup next (so an escaped "&lt;b&gt;" isn't mistaken for a tag), THEN
// decode entities, then normalize whitespace.
std::string cleanText(const std::string& raw) {
  std::string s = unwrapCdata(raw);
  s = stripTagsAndCollapse(s);
  s = decodeEntities(s);
  return s;
}

// ---------------------------------------------------------------------
// pub_date normalization: RFC 822 / ISO 8601 -> ISO 8601 with +08:00
// ---------------------------------------------------------------------

// Howard Hinnant's civil-calendar <-> days-since-epoch conversion — pure
// integer arithmetic, portable across the native/WASM/ESP32 targets without
// relying on timegm()/mktime() timezone quirks.
long long daysFromCivil(int y, int mo, int d) {
  y -= (mo <= 2) ? 1 : 0;
  long long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned mAdj = (unsigned)(mo + (mo > 2 ? -3 : 9));
  unsigned doy = (153 * mAdj + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long long)doe - 719468;
}

void civilFromDays(long long z, int& y, int& mo, int& d) {
  z += 719468;
  long long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long long yyy = (long long)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  mo = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yyy + (mo <= 2 ? 1 : 0));
}

long long toEpoch(int y, int mo, int d, int h, int mi, int s) {
  return daysFromCivil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + s;
}

void fromEpoch(long long epoch, int& y, int& mo, int& d, int& h, int& mi, int& s) {
  long long days = (epoch >= 0) ? (epoch / 86400) : ((epoch - 86399) / 86400);
  long long secOfDay = epoch - days * 86400;
  civilFromDays(days, y, mo, d);
  h = (int)(secOfDay / 3600);
  mi = (int)((secOfDay % 3600) / 60);
  s = (int)(secOfDay % 60);
}

std::string formatIso8(int y, int mo, int d, int h, int mi, int s) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d+08:00", y, mo, d, h, mi, s);
  return buf;
}

int monthFromName(const std::string& m3) {
  static const char* names[12] = {"jan", "feb", "mar", "apr", "may", "jun",
                                  "jul", "aug", "sep", "oct", "nov", "dec"};
  std::string lm = lower(m3);
  for (int i = 0; i < 12; i++)
    if (lm == names[i]) return i + 1;
  return 0;
}

bool validYmd(int y, int mo, int d) { return y > 0 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31; }

// RFC 822 / 1123: "[Www, ] DD Mon YYYY HH:MM[:SS] ZONE".
bool parseRfc822(const std::string& raw, int& y, int& mo, int& d, int& h, int& mi, int& s,
                 int& offMin) {
  std::string t = raw;
  size_t comma = t.find(',');
  if (comma != std::string::npos && comma < 5) t = t.substr(comma + 1);
  size_t p = 0;
  while (p < t.size() && t[p] == ' ') p++;
  t = t.substr(p);

  char monStr[8] = {0};
  char zone[16] = {0};
  int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
  int n = sscanf(t.c_str(), "%d %7s %d %d:%d:%d %15s", &day, monStr, &year, &hh, &mm, &ss, zone);
  if (n < 6) {
    ss = 0;
    zone[0] = 0;
    n = sscanf(t.c_str(), "%d %7s %d %d:%d %15s", &day, monStr, &year, &hh, &mm, zone);
    if (n < 5) return false;
  }
  int monNum = monthFromName(monStr);
  if (monNum == 0) return false;
  if (year < 100) year += (year < 70) ? 2000 : 1900;  // tolerant 2-digit-year fallback
  if (!validYmd(year, monNum, day)) return false;
  y = year; mo = monNum; d = day; h = hh; mi = mm; s = ss;

  std::string z = lower(zone);
  for (auto& c : z) c = (char)toupper((unsigned char)c);
  if (z.empty() || z == "GMT" || z == "UT" || z == "UTC" || z == "Z") { offMin = 0; return true; }
  static const std::pair<const char*, int> kNamed[] = {
      {"EST", -300}, {"EDT", -240}, {"CST", -360}, {"CDT", -300},
      {"MST", -420}, {"MDT", -360}, {"PST", -480}, {"PDT", -420}};
  for (auto& kv : kNamed)
    if (z == kv.first) { offMin = kv.second; return true; }
  if (z.size() == 5 && (z[0] == '+' || z[0] == '-') && isdigit((unsigned char)z[1]) &&
      isdigit((unsigned char)z[2]) && isdigit((unsigned char)z[3]) && isdigit((unsigned char)z[4])) {
    int sign = z[0] == '-' ? -1 : 1;
    int oh = (z[1] - '0') * 10 + (z[2] - '0');
    int om = (z[3] - '0') * 10 + (z[4] - '0');
    offMin = sign * (oh * 60 + om);
    return true;
  }
  offMin = 0;  // unrecognized zone abbreviation — tolerant default (treat as UTC)
  return true;
}

// ISO 8601: "YYYY-MM-DD[T| ]HH:MM[:SS][.frac][Z|±HH:MM|±HHMM]" or bare date.
bool parseIso8601(const std::string& raw, int& y, int& mo, int& d, int& h, int& mi, int& s,
                  bool& hasOffset, int& offMin) {
  int Y, M, D, H = 0, Mi = 0, S = 0;
  if (sscanf(raw.c_str(), "%4d-%2d-%2d", &Y, &M, &D) != 3) return false;
  if (!validYmd(Y, M, D)) return false;
  hasOffset = false;
  offMin = 0;
  size_t p = 10;
  if (p < raw.size() && (raw[p] == 'T' || raw[p] == ' ')) {
    p++;
    if (p + 4 >= raw.size() || sscanf(raw.c_str() + p, "%2d:%2d", &H, &Mi) != 2) return false;
    p += 5;
    if (p < raw.size() && raw[p] == ':' && p + 2 < raw.size() && isdigit((unsigned char)raw[p + 1])) {
      sscanf(raw.c_str() + p + 1, "%2d", &S);
      p += 3;
    }
    if (p < raw.size() && raw[p] == '.') {
      p++;
      while (p < raw.size() && isdigit((unsigned char)raw[p])) p++;
    }
    if (p < raw.size()) {
      if (raw[p] == 'Z' || raw[p] == 'z') {
        hasOffset = true;
        offMin = 0;
      } else if (raw[p] == '+' || raw[p] == '-') {
        int sign = raw[p] == '-' ? -1 : 1;
        size_t q = p + 1;
        if (q + 1 < raw.size() && isdigit((unsigned char)raw[q]) && isdigit((unsigned char)raw[q + 1])) {
          int oh = (raw[q] - '0') * 10 + (raw[q + 1] - '0');
          q += 2;
          if (q < raw.size() && raw[q] == ':') q++;
          int om = 0;
          if (q + 1 < raw.size() && isdigit((unsigned char)raw[q]) && isdigit((unsigned char)raw[q + 1])) {
            om = (raw[q] - '0') * 10 + (raw[q + 1] - '0');
          }
          hasOffset = true;
          offMin = sign * (oh * 60 + om);
        }
      }
    }
  }
  y = Y; mo = M; d = D; h = H; mi = Mi; s = S;
  return true;
}

// Detects RFC 822 by the presence of a 3-letter month abbreviation (ISO 8601
// dates never contain three consecutive letters).
bool looksRfc822(const std::string& raw) {
  for (size_t i = 0; i + 2 < raw.size(); i++) {
    if (isalpha((unsigned char)raw[i]) && isalpha((unsigned char)raw[i + 1]) &&
        isalpha((unsigned char)raw[i + 2]) && monthFromName(raw.substr(i, 3)) != 0)
      return true;
  }
  return false;
}

bool normalizePubDate(const std::string& raw, std::string& isoOut) {
  if (raw.empty()) return false;
  int y, mo, d, h, mi, s, offMin;
  if (looksRfc822(raw)) {
    if (!parseRfc822(raw, y, mo, d, h, mi, s, offMin)) return false;
  } else {
    bool hasOffset;
    if (!parseIso8601(raw, y, mo, d, h, mi, s, hasOffset, offMin)) return false;
    if (!hasOffset) { isoOut = formatIso8(y, mo, d, h, mi, s); return true; }
  }
  // Convert wall-clock (y,mo,d,h,mi,s) at offMin minutes from UTC into the
  // device's fixed +08:00 wall-clock.
  long long epochUtc = toEpoch(y, mo, d, h, mi, s) - (long long)offMin * 60;
  long long localEpoch = epochUtc + 8LL * 3600;
  int ly, lmo, ld, lh, lmi, ls;
  fromEpoch(localEpoch, ly, lmo, ld, lh, lmi, ls);
  isoOut = formatIso8(ly, lmo, ld, lh, lmi, ls);
  return true;
}

}  // namespace

bool parseRss(const std::string& body, JsonDocument& out, std::string& err) {
  out.clear();
  JsonObject root = out.to<JsonObject>();
  JsonObject feedObj = root["feed"].to<JsonObject>();
  JsonArray items = root["items"].to<JsonArray>();

  size_t rootTagStart, rootInnerStart, rootInnerEnd, afterRoot;
  bool isAtom;
  std::string itemTag;

  if (findBlock(body, "channel", 0, rootTagStart, rootInnerStart, rootInnerEnd, afterRoot)) {
    isAtom = false;
    itemTag = "item";
  } else if (findBlock(body, "feed", 0, rootTagStart, rootInnerStart, rootInnerEnd, afterRoot)) {
    isAtom = true;
    itemTag = "entry";
  } else {
    err = "rss: no <channel> or <feed> root element found";
    return false;
  }

  // feed.title: the first <title> strictly before the first item/entry.
  {
    size_t firstItemTagStart = rootInnerEnd, s2, e2, n2;
    findBlock(body, itemTag, rootInnerStart, firstItemTagStart, s2, e2, n2);
    std::string innerText, attrs;
    if (extractElement(body, "title", rootInnerStart, firstItemTagStart, innerText, attrs))
      feedObj["title"] = cleanText(innerText);
    else
      feedObj["title"] = "";
  }

  // items/entries, feed order preserved, capped at 20 (§5.4).
  size_t pos = rootInnerStart;
  int count = 0;
  while (count < 20) {
    size_t blkTagStart, blkInnerStart, blkInnerEnd, blkNext;
    if (!findBlock(body, itemTag, pos, blkTagStart, blkInnerStart, blkInnerEnd, blkNext)) break;
    if (blkTagStart >= rootInnerEnd) break;  // don't wander past </channel> or </feed>
    pos = blkNext;

    JsonObject it = items.add<JsonObject>();
    std::string innerText, attrs;

    if (extractElement(body, "title", blkInnerStart, blkInnerEnd, innerText, attrs))
      it["title"] = cleanText(innerText);
    else
      it["title"] = "";

    it["link"] = cleanText(extractLink(body, blkInnerStart, blkInnerEnd));

    if (!isAtom) {
      if (extractElement(body, "description", blkInnerStart, blkInnerEnd, innerText, attrs))
        it["description"] = cleanText(innerText);
      else
        it["description"] = "";
    } else if (extractElement(body, "summary", blkInnerStart, blkInnerEnd, innerText, attrs)) {
      it["description"] = cleanText(innerText);
    } else if (extractElement(body, "content", blkInnerStart, blkInnerEnd, innerText, attrs)) {
      it["description"] = cleanText(innerText);
    } else {
      it["description"] = "";
    }

    std::string rawDate;
    bool haveDate = false;
    if (!isAtom) {
      haveDate = extractElement(body, "pubdate", blkInnerStart, blkInnerEnd, rawDate, attrs);
    } else if (extractElement(body, "published", blkInnerStart, blkInnerEnd, rawDate, attrs)) {
      haveDate = true;
    } else if (extractElement(body, "updated", blkInnerStart, blkInnerEnd, rawDate, attrs)) {
      haveDate = true;
    }
    std::string iso;
    if (haveDate && normalizePubDate(cleanText(rawDate), iso))
      it["pub_date"] = iso;
    else
      it["pub_date"] = nullptr;

    count++;
  }

  return true;
}

}  // namespace detail
}  // namespace yat
