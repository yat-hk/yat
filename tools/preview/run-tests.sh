#!/bin/sh
# Golden tests for the YAT native engine target. Deterministic: pinned --now.
set -e
cd "$(dirname "$0")"
make -s
NOW=1785142800  # fixed instant
# hko-now is one of the 17 real-world packs that moved to yat-hk/yat-packs;
# the copy under testpacks/ is a frozen fixture for the engine tests below
# (warnings strip, battery glyph bands, empty-state card, --profile e1001) —
# see testpacks/README.md. Its own per-pack appearance golden moved with it.
P=testpacks/hko-now.yat-pack.json
# hko-now gained a second source in 0.2.0 (HKO warnsum, the active-warnings
# strip). Every pre-existing hko-now case below is about the OTHER source, so
# they all pin warnsum to the real "nothing in force" capture ({}) and keep
# asserting exactly what they asserted before. The warnings states get their
# own block further down.
WCLEAR="--doc warnsum=fixtures/hko-now.warnsum.json"

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --out /tmp/yat-t1.png 2>/tmp/yat-t1.log
grep -q '"temp": 28' /tmp/yat-t1.log || { echo "FAIL: temp extraction"; exit 1; }
H1=$(grep '^hash:' /tmp/yat-t1.log)

./yat-preview "$P" --doc current=fixtures/hko-hot.json $WCLEAR --now $NOW --out /tmp/yat-t2.png 2>/tmp/yat-t2.log
grep -q '"temp": 34' /tmp/yat-t2.log || { echo "FAIL: hot temp extraction"; exit 1; }
H2=$(grep '^hash:' /tmp/yat-t2.log)
[ "$H1" != "$H2" ] || { echo "FAIL: hash should differ between fixtures"; exit 1; }

# param overlay changes hash + extraction target
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --params '{"district":"香港天文台"}' --now $NOW --out /tmp/yat-t3.png 2>/tmp/yat-t3.log
grep -q '"temp": 27' /tmp/yat-t3.log || { echo "FAIL: param-driven extraction"; exit 1; }

# determinism: identical inputs -> identical PNG
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --out /tmp/yat-t1b.png 2>/dev/null
cmp -s /tmp/yat-t1.png /tmp/yat-t1b.png || { echo "FAIL: nondeterministic render"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-t1.png goldens/hko-now.png)
if [ -f goldens/hko-now.png ]; then
  cmp -s /tmp/yat-t1.png goldens/hko-now.png || { echo "FAIL: golden mismatch (goldens/hko-now.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/hko-now.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- hko-now 0.2.0: the active-warnings strip. A second source (HKO warnsum)
# ---- feeds one hand-unrolled, `when`-guarded row per warning code; the strip
# ---- and its rule are gated on two `compute` fields that concatenate the 11
# ---- extracted codes, so "nothing in force" costs the page nothing at all.
# ---- The two states that matter are the two asserted here: warnings present,
# ---- and the empty `{}` the endpoint really returns most of the year.
WACTIVE="--doc warnsum=fixtures/hko-now.warnsum-active.json"

# a) warnings in force: the three codes extract, and BOTH compute guards agree
#    with them (warn_a carries the three, warn_b stays empty).
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WACTIVE --now $NOW --out /tmp/yat-w1.png 2>/tmp/yat-w1.log
grep -q '"w_rain": "WRAINA"' /tmp/yat-w1.log || { echo "FAIL: warnings: WRAIN.code extraction"; exit 1; }
grep -q '"w_ts": "WTS"' /tmp/yat-w1.log || { echo "FAIL: warnings: WTS.code extraction"; exit 1; }
grep -q '"w_hot": "WHOT"' /tmp/yat-w1.log || { echo "FAIL: warnings: WHOT.code extraction"; exit 1; }
grep -q '"w_tc": null' /tmp/yat-w1.log || { echo "FAIL: warnings: an absent warning key should extract null"; exit 1; }
grep -q '"warn_a": "WRAINAWTSWHOT"' /tmp/yat-w1.log || { echo "FAIL: warnings: warn_a compute guard"; exit 1; }
grep -q '"warn_b": ""' /tmp/yat-w1.log || { echo "FAIL: warnings: warn_b should be empty here"; exit 1; }
grep -q 'render warn' /tmp/yat-w1.log && { echo "FAIL: warnings-active render produced a warning"; exit 1; }

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WACTIVE --now $NOW --out /tmp/yat-w1b.png 2>/dev/null
cmp -s /tmp/yat-w1.png /tmp/yat-w1b.png || { echo "FAIL: nondeterministic render (warnings active)"; exit 1; }

# The whole point: the warnings state must not look like the clear state.
if cmp -s /tmp/yat-t1.png /tmp/yat-w1.png; then echo "FAIL: active warnings did not change the render"; exit 1; fi

# golden compare (regenerate with: cp /tmp/yat-w1.png goldens/hko-now-warnings.png)
if [ -f goldens/hko-now-warnings.png ]; then
  cmp -s /tmp/yat-w1.png goldens/hko-now-warnings.png || { echo "FAIL: golden mismatch (goldens/hko-now-warnings.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/hko-now-warnings.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# b) zero warnings — the real `{}` the endpoint serves whenever nothing is in
#    force. Every code extracts null, both guards go empty, and the strip and
#    its rule vanish: /tmp/yat-t1.png (the golden compared above) IS this
#    render, so the assertion is that the page is the plain weather page.
grep -q '"warn_a": ""' /tmp/yat-t1.log || { echo "FAIL: empty warnsum should leave warn_a empty"; exit 1; }
grep -q '"warn_b": ""' /tmp/yat-t1.log || { echo "FAIL: empty warnsum should leave warn_b empty"; exit 1; }
grep -q '"temp": 28' /tmp/yat-t1.log || { echo "FAIL: empty warnsum must not disturb the weather source"; exit 1; }

# c) ...and an empty warnsum must not blank the page. The clear render has to
#    carry the same ink the pre-0.2.0 page did — a `when` guard that swallowed
#    the temperature would still be "no render warning", so assert the pixels.
python3 - <<'EOF' || exit 1
import struct, sys, zlib
def load(p):
    d = open(p, 'rb').read(); pos = 8; idat = b''; w = h = 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ, dat = d[pos+4:pos+8], d[pos+8:pos+8+ln]
        if typ == b'IHDR': w, h = struct.unpack('>II', dat[:8])
        if typ == b'IDAT': idat += dat
        pos += 12 + ln
    raw = zlib.decompress(idat); stride = w * 3; rows = []; prev = bytearray(stride); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+stride]); i += stride
        for x in range(stride):
            a = line[x-3] if x >= 3 else 0
            b = prev[x]
            c = prev[x-3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p_ = a + b - c
                pa, pb, pc = abs(p_-a), abs(p_-b), abs(p_-c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, rows

w, h, rows = load('/tmp/yat-t1.png')
# Content area only (§9.1: 44px header, 32px footer) — the chrome always draws.
ink = sum(1 for y in range(44, h-32) for x in range(w) if rows[y][x*3:x*3+3] != b'\xff\xff\xff')
if ink < 3000:
    print(f"FAIL: clear page has only {ink} content pixels — an empty warnsum blanked it"); sys.exit(1)
EOF

# d) the honest failure: warnsum fetched and FAILED (no snapshot to fall back
#    on) leaves every code null, so the strip hides and the page degrades to
#    exactly the clear render — byte-identical, not merely similar.
./yat-preview "$P" --doc current=fixtures/hko-now.current.json --doc warnsum=fixtures/does-not-exist.json --now $NOW --out /tmp/yat-wfail.png 2>/tmp/yat-wfail.log
grep -q 'source warnsum' /tmp/yat-wfail.log || { echo "FAIL: a failed warnsum fetch should be reported"; exit 1; }
grep -q '"temp": 28' /tmp/yat-wfail.log || { echo "FAIL: a failed warnsum must not take the weather source down with it"; exit 1; }
cmp -s /tmp/yat-t1.png /tmp/yat-wfail.png || { echo "FAIL: a failed warnsum should degrade to the clear page, not a broken one"; exit 1; }

# e) codes the pack does not draw (CANCEL is a real WTCSGNL code, §warnsum) must
#    match no row. The guards still go truthy, so this is the case that proves
#    the strip is code-matched rather than presence-matched: it renders the
#    clear page's content with an empty strip, never an invented row.
cat > /tmp/yat-warnsum-cancel.json <<'EOF'
{ "WTCSGNL": { "name": "熱帶氣旋警告信號", "code": "CANCEL", "actionCode": "CANCEL",
               "issueTime": "2026-07-27T16:00:00+08:00", "updateTime": "2026-07-27T16:00:00+08:00" } }
EOF
./yat-preview "$P" --doc current=fixtures/hko-now.current.json --doc warnsum=/tmp/yat-warnsum-cancel.json --now $NOW --out /tmp/yat-wcancel.png 2>/tmp/yat-wcancel.log
grep -q '"w_tc": "CANCEL"' /tmp/yat-wcancel.log || { echo "FAIL: CANCEL should still extract as a code"; exit 1; }
grep -q 'render warn' /tmp/yat-wcancel.log && { echo "FAIL: a CANCEL code produced a render warning"; exit 1; }
if cmp -s /tmp/yat-w1.png /tmp/yat-wcancel.png; then echo "FAIL: CANCEL drew the warnings strip"; exit 1; fi

# ---- G25/§11.4: standard-chrome footer battery glyph (Engine::
# ---- setBatteryPercent(), --battery <pct>). Omitting the flag (every run
# ---- above, including the hko-now golden compare just above) must stay
# ---- byte-identical to the pre-G25 goldens — that is what the compare above
# ---- already proves, with no --battery flag in sight. A set percentage must
# ---- visibly change the render, and the glyph is now banded by charge —
# ---- `good` above 50%, `warn` 21-50%, `danger` at/below 20% (glyph shape is
# ---- unchanged, only the ink) — so three distinct percentages, one per band,
# ---- must all render differently from each other and the <=20% band alone
# ---- keeps the bilingual low-battery hint.
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --battery 63 --out /tmp/yat-batt63.png 2>/tmp/yat-batt63.log
grep -q 'render warn' /tmp/yat-batt63.log && { echo "FAIL: battery glyph (63%) render produced a warning"; exit 1; }
if cmp -s /tmp/yat-t1.png /tmp/yat-batt63.png; then echo "FAIL: --battery 63 did not change the render"; exit 1; fi

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --battery 35 --out /tmp/yat-batt35.png 2>/tmp/yat-batt35.log
if cmp -s /tmp/yat-batt63.png /tmp/yat-batt35.png; then echo "FAIL: --battery 63 (good) and --battery 35 (warn) rendered identically"; exit 1; fi

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --battery 15 --out /tmp/yat-batt15.png 2>/tmp/yat-batt15.log
if cmp -s /tmp/yat-batt63.png /tmp/yat-batt15.png; then echo "FAIL: --battery 63 and --battery 15 rendered identically"; exit 1; fi
if cmp -s /tmp/yat-batt35.png /tmp/yat-batt15.png; then echo "FAIL: --battery 35 (warn) and --battery 15 (danger) rendered identically"; exit 1; fi

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --battery 63 --out /tmp/yat-batt63b.png 2>/dev/null
cmp -s /tmp/yat-batt63.png /tmp/yat-batt63b.png || { echo "FAIL: nondeterministic render (battery glyph)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-batt63.png goldens/battery-63.png)
if [ -f goldens/battery-63.png ]; then
  cmp -s /tmp/yat-batt63.png goldens/battery-63.png || { echo "FAIL: golden mismatch (goldens/battery-63.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/battery-63.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# the `warn` (21-50%) band, pinned so the three-way split cannot regress to
# a binary good/danger split without a test saying so.
# (regenerate with: cp /tmp/yat-batt35.png goldens/battery-35.png)
if [ -f goldens/battery-35.png ]; then
  cmp -s /tmp/yat-batt35.png goldens/battery-35.png || { echo "FAIL: golden mismatch (goldens/battery-35.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/battery-35.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# The low-battery footer is the one place standard chrome draws `danger`
# (§9.1a) — glyph and hint both. Pinned as a golden so the role table cannot be
# changed out from under the chrome without a test saying so.
# (regenerate with: cp /tmp/yat-batt15.png goldens/battery-15.png)
if [ -f goldens/battery-15.png ]; then
  cmp -s /tmp/yat-batt15.png goldens/battery-15.png || { echo "FAIL: golden mismatch (goldens/battery-15.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/battery-15.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- render-test: the v0.2 render constructs (list, {{item}}/{{index}},
# ---- strings/lang_param, enum_title, pick_by_day, sign_char, when-on-rows).
# ---- Inline source only, so the render is a pure function of the pinned now.
R=../../packs/examples/render-test.yat-pack.json

./yat-preview "$R" --now $NOW --out /tmp/yat-r1.png 2>/tmp/yat-r1.log
grep -q '"not_a_list": "not an array"' /tmp/yat-r1.log || { echo "FAIL: inline extraction"; exit 1; }
# a non-array bind renders nothing and warns E_BIND (§9.8/§11.3), never empty_text
grep -q "E_BIND: list bind 'data.not_a_list'" /tmp/yat-r1.log || { echo "FAIL: missing E_BIND warning"; exit 1; }

./yat-preview "$R" --now $NOW --out /tmp/yat-r1b.png 2>/dev/null
cmp -s /tmp/yat-r1.png /tmp/yat-r1b.png || { echo "FAIL: nondeterministic render (render-test)"; exit 1; }

# strings/lang_param + enum_title + list.max_rows param form all react to params
./yat-preview "$R" --params '{"lang":"en","market":"jp","rows_shown":4}' --now $NOW --out /tmp/yat-r2.png 2>/tmp/yat-r2.log
if cmp -s /tmp/yat-r1.png /tmp/yat-r2.png; then echo "FAIL: lang/market/max_rows params did not change the render"; exit 1; fi

# golden compare (regenerate with: cp /tmp/yat-r1.png goldens/render-test.png)
if [ -f goldens/render-test.png ]; then
  cmp -s /tmp/yat-r1.png goldens/render-test.png || { echo "FAIL: golden mismatch (goldens/render-test.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/render-test.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- negative tests: illegal / unimplemented constructs must fail by name ----
cat > /tmp/yat-bad-item.yat-pack.json <<'EOF'
{ "yat": 1, "id": "bad-item", "name": { "en": "Bad item" },
  "aliases": { "en": ["bad"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {}, "additionalProperties": false },
  "data": { "sources": [] },
  "render": { "widgets": [ { "type": "text", "value": "{{item.x}}" } ] },
  "schedule": { "default": { "every_min": 60 } } }
EOF
if ./yat-preview /tmp/yat-bad-item.yat-pack.json --now $NOW --out /tmp/yat-bad1.png 2>/tmp/yat-bad1.log; then
  echo "FAIL: {{item}} outside a list row should be an error"; exit 1
fi
grep -q 'list row' /tmp/yat-bad1.log || { echo "FAIL: unclear error for {{item}} outside a list row"; exit 1; }

# ---- §9.8a nested list: a `list` whose outer bind is not a for_each
# ---- source's collect.field must still be rejected at load, by name.
cat > /tmp/yat-bad-nest.yat-pack.json <<'EOF'
{ "yat": 1, "id": "bad-nest", "name": { "en": "Bad nest" },
  "aliases": { "en": ["bad"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {}, "additionalProperties": false },
  "data": { "sources": [] },
  "render": { "widgets": [ { "type": "list", "bind": "data.outer", "row": [
    { "type": "list", "bind": "item.inner", "row": [ { "type": "text", "value": "{{item}}" } ] } ] } ] },
  "schedule": { "default": { "every_min": 60 } } }
EOF
if ./yat-preview /tmp/yat-bad-nest.yat-pack.json --now $NOW --out /tmp/yat-bad2.png 2>/tmp/yat-bad2.log; then
  echo "FAIL: nested list with a non-collect-field outer bind should be rejected at load"; exit 1
fi
grep -q '§9.8a' /tmp/yat-bad2.log || { echo "FAIL: unclear error for a nested list with a bad outer bind"; exit 1; }

# ---- §9.8a: a THREE-level nesting must still be rejected (one level only) ----
cat > /tmp/yat-bad-nest3.yat-pack.json <<'EOF'
{ "yat": 1, "id": "bad-nest-3level", "name": { "en": "Bad nest 3 level" },
  "aliases": { "en": ["bad"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {
    "x": { "type": "array", "items": { "type": "string" }, "default": ["a"] } },
    "additionalProperties": false },
  "data": { "sources": [ { "id": "g", "type": "https", "for_each": "params.x",
    "url": "https://example.com/g", "format": "json",
    "collect": { "field": "outer", "extract": { "inner": "inner" } } } ] },
  "render": { "widgets": [ { "type": "list", "bind": "data.outer", "row": [
    { "type": "list", "bind": "item.inner", "row": [
      { "type": "list", "bind": "item.x", "row": [ { "type": "text", "value": "{{item}}" } ] }
    ] } ] } ] },
  "schedule": { "default": { "every_min": 60 } } }
EOF
if ./yat-preview /tmp/yat-bad-nest3.yat-pack.json --now $NOW --out /tmp/yat-bad2b.png 2>/tmp/yat-bad2b.log; then
  echo "FAIL: three-level nested list should be rejected at load"; exit 1
fi
grep -q '§9.8a' /tmp/yat-bad2b.log || { echo "FAIL: unclear error for a three-level nested list"; exit 1; }

# ---- §9.8a [V]: outer max_rows x nested max_rows must be <= 40 ----
cat > /tmp/yat-bad-nest-cap.yat-pack.json <<'EOF'
{ "yat": 1, "id": "bad-nest-cap", "name": { "en": "Bad nest cap" },
  "aliases": { "en": ["bad"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {
    "x": { "type": "array", "items": { "type": "string" }, "default": ["a"] } },
    "additionalProperties": false },
  "data": { "sources": [ { "id": "g", "type": "https", "for_each": "params.x",
    "url": "https://example.com/g", "format": "json",
    "collect": { "field": "outer", "extract": { "inner": "inner" } } } ] },
  "render": { "widgets": [ { "type": "list", "bind": "data.outer", "max_rows": 10, "row": [
    { "type": "list", "bind": "item.inner", "max_rows": 5,
      "row": [ { "type": "text", "value": "{{item}}" } ] } ] } ] },
  "schedule": { "default": { "every_min": 60 } } }
EOF
if ./yat-preview /tmp/yat-bad-nest-cap.yat-pack.json --now $NOW --out /tmp/yat-bad2c.png 2>/tmp/yat-bad2c.log; then
  echo "FAIL: outer x nested max_rows > 40 should be rejected at load"; exit 1
fi
grep -q '§9.8a' /tmp/yat-bad2c.log || { echo "FAIL: unclear error for outer x nested max_rows > 40"; exit 1; }
grep -q '50' /tmp/yat-bad2c.log || { echo "FAIL: max_rows cap error should name the computed product"; exit 1; }

# ---- v0.2: rss format (§5.4) — deterministic 3-item fixture, known values ----
# Exercises: RSS 2.0 tag scanning, CDATA unwrap, HTML-tag strip, entity
# decode, RFC822->ISO+08:00 pub_date normalization (GMT and +0800 inputs),
# and a missing pubDate -> null. Extraction-only (this pack's render uses
# only implemented widgets, so it renders cleanly too).
./yat-preview fixtures/packs/test-rss-mini.yat-pack.json --doc feed=fixtures/test-rss-mini.xml --now $NOW --out /tmp/yat-rss.png 2>/tmp/yat-rss.log
grep -q '"feed_title": "Test Feed & More"' /tmp/yat-rss.log || { echo "FAIL: rss feed.title entity decode"; exit 1; }
grep -q '"title1": "Hello World & Universe"' /tmp/yat-rss.log || { echo "FAIL: rss CDATA+tag-strip+entity-decode"; exit 1; }
grep -q '"desc1": "First item & details."' /tmp/yat-rss.log || { echo "FAIL: rss description cleanup"; exit 1; }
grep -q '"pubdate1": "2026-07-27T13:00:00+08:00"' /tmp/yat-rss.log || { echo "FAIL: rss RFC822 GMT -> +08:00"; exit 1; }
grep -q '"pubdate2": "2026-07-27T13:15:00+08:00"' /tmp/yat-rss.log || { echo "FAIL: rss RFC822 +0800 -> +08:00"; exit 1; }
grep -q '"pubdate3": null' /tmp/yat-rss.log || { echo "FAIL: rss missing pubDate -> null"; exit 1; }
grep -q '"count": 3' /tmp/yat-rss.log || { echo "FAIL: rss item count"; exit 1; }

# ---- v0.2: csv format (§5.5) — has_header true/false, quoted comma ----
./yat-preview fixtures/packs/test-csv-mini.yat-pack.json --doc hdr=fixtures/test-csv-mini.csv --doc nohdr=fixtures/test-csv-mini.csv --now $NOW --out /tmp/yat-csv.png 2>/tmp/yat-csv.log
grep -q '"city0": "Hong Kong, HK"' /tmp/yat-csv.log || { echo "FAIL: csv quoted-comma field"; exit 1; }
grep -q '"name1": "Bob"' /tmp/yat-csv.log || { echo "FAIL: csv second row (has_header)"; exit 1; }
grep -q '"count": 2' /tmp/yat-csv.log || { echo "FAIL: csv has_header row count"; exit 1; }
grep -q '"cell00": "name"' /tmp/yat-csv.log || { echo "FAIL: csv no-header array row (header row included)"; exit 1; }
grep -q '"count2": 3' /tmp/yat-csv.log || { echo "FAIL: csv no-header row count"; exit 1; }

# ---- v0.2: for_each + collect (§5.1a) — fan-out order, {{each}}, percent-encoding ----
# Three fixtures consumed in order (comma-list --doc extension); each iteration's
# URL exercises both {{each}} and {{params.*}} percent-encoding (space + CJK).
./yat-preview fixtures/packs/test-for-each-mini.yat-pack.json --doc results=fixtures/test-for-each.1.json,fixtures/test-for-each.2.json,fixtures/test-for-each.3.json --now $NOW --out /tmp/yat-foreach.png 2>/tmp/yat-foreach.log
grep -q 'fetch results <- https://example.com/api?code=AAA&owner=K%20K%20%E4%BD%A0%E5%A5%BD' /tmp/yat-foreach.log || { echo "FAIL: for_each url #1 (plain each + percent-encoded param)"; exit 1; }
grep -q 'fetch results <- https://example.com/api?code=BB%20CD&owner=K%20K%20%E4%BD%A0%E5%A5%BD' /tmp/yat-foreach.log || { echo "FAIL: for_each url #2 (space in each not encoded)"; exit 1; }
grep -q 'fetch results <- https://example.com/api?code=EF%E4%BD%A0%E5%A5%BD&owner=K%20K%20%E4%BD%A0%E5%A5%BD' /tmp/yat-foreach.log || { echo "FAIL: for_each url #3 (CJK in each not encoded)"; exit 1; }
L1=$(grep -n '"each": "AAA"' /tmp/yat-foreach.log | head -1 | cut -d: -f1)
L2=$(grep -n '"each": "BB CD"' /tmp/yat-foreach.log | head -1 | cut -d: -f1)
L3=$(grep -n '"each": "EF你好"' /tmp/yat-foreach.log | head -1 | cut -d: -f1)
[ -n "$L1" ] && [ -n "$L2" ] && [ -n "$L3" ] && [ "$L1" -lt "$L2" ] && [ "$L2" -lt "$L3" ] || { echo "FAIL: for_each collect array order / each echo"; exit 1; }
grep -q '"value": 30' /tmp/yat-foreach.log || { echo "FAIL: for_each collect.extract value"; exit 1; }

# empty array -> zero fetches, collect.field = [] (not an error)
./yat-preview fixtures/packs/test-for-each-mini.yat-pack.json --params '{"codes":[]}' --now $NOW --out /tmp/yat-foreach-empty.png 2>/tmp/yat-foreach-empty.log
grep -q '"results": \[\]' /tmp/yat-foreach-empty.log || { echo "FAIL: for_each empty array -> []"; exit 1; }

# ---- v0.3: nested list (§9.8a) — a valid one-level nesting must load AND
# ---- render: outer list bound to a for_each source's collect.field, nested
# ---- list bound to item.<prop> over that source's collect.extract array,
# ---- item/index correctly rebinding inside the inner rows.
./yat-preview fixtures/packs/test-nested-list-mini.yat-pack.json --doc groups=fixtures/test-nested-list.1.json,fixtures/test-nested-list.2.json --now $NOW --out /tmp/yat-nest.png 2>/tmp/yat-nest.log
grep -q 'not implemented' /tmp/yat-nest.log && { echo "FAIL: nested list hit an unimplemented construct"; exit 1; }
grep -q 'E_BIND\|render warn' /tmp/yat-nest.log && { echo "FAIL: nested list render produced a warning"; exit 1; }
grep -q '"name": "Alpha"' /tmp/yat-nest.log || { echo "FAIL: nested list: outer row 1's inner array not extracted"; exit 1; }
grep -q '"name": "One"' /tmp/yat-nest.log || { echo "FAIL: nested list: outer row 2's inner array not extracted"; exit 1; }
grep -q '"each": "g1"' /tmp/yat-nest.log || { echo "FAIL: nested list: outer item.each missing"; exit 1; }

./yat-preview fixtures/packs/test-nested-list-mini.yat-pack.json --doc groups=fixtures/test-nested-list.1.json,fixtures/test-nested-list.2.json --now $NOW --out /tmp/yat-nest-b.png 2>/dev/null
cmp -s /tmp/yat-nest.png /tmp/yat-nest-b.png || { echo "FAIL: nondeterministic render (nested list)"; exit 1; }

# a single-group render must differ from the two-group render (proves the
# outer list — not just the inner one — is really iterating)
./yat-preview fixtures/packs/test-nested-list-mini.yat-pack.json --params '{"groups":["g1"]}' --doc groups=fixtures/test-nested-list.1.json --now $NOW --out /tmp/yat-nest1.png 2>/tmp/yat-nest1.log
grep -q 'not implemented' /tmp/yat-nest1.log && { echo "FAIL: nested list (1 group) hit an unimplemented construct"; exit 1; }
if cmp -s /tmp/yat-nest.png /tmp/yat-nest1.png; then echo "FAIL: outer list group count did not change the render"; exit 1; fi

# ---- v0.2: compute (§7.4) — single-placeholder numeric typing vs. string ----
./yat-preview fixtures/packs/test-compute-mini.yat-pack.json --now $NOW --out /tmp/yat-compute.png 2>/tmp/yat-compute.log
grep -q '"days_left": 14' /tmp/yat-compute.log || { echo "FAIL: compute days_left not numeric (§7.4(4))"; exit 1; }
grep -q '"label": "T-minus 14 days"' /tmp/yat-compute.log || { echo "FAIL: compute label not a string"; exit 1; }

./yat-preview fixtures/packs/test-compute-mini.yat-pack.json --params '{"exam_date":"2026-07-20"}' --now $NOW --out /tmp/yat-compute2.png 2>/tmp/yat-compute2.log
grep -q '"days_left": -7' /tmp/yat-compute2.log || { echo "FAIL: compute negative days_left (past date)"; exit 1; }

# ---- v0.2: s2t filter (§8.7) — simplified->traditional Chinese conversion ----
./yat-preview fixtures/packs/test-s2t-mini.yat-pack.json --now $NOW --out /tmp/yat-s2t.png 2>/tmp/yat-s2t.log
grep -q '"txt": "简体转繁体测试"' /tmp/yat-s2t.log || { echo "FAIL: s2t extraction preserves raw simplified text"; exit 1; }
./yat-preview fixtures/packs/test-s2t-mini.yat-pack.json --now $NOW --out /tmp/yat-s2t-b.png 2>/dev/null
cmp -s /tmp/yat-s2t.png /tmp/yat-s2t-b.png || { echo "FAIL: nondeterministic render (s2t)"; exit 1; }

# ---- v0.3: sequential source references (§5.7) — {{sources.<id>.<field>}} ----
# a) backward reference: inline source `first`'s extracted field substitutes
#    into later https source `second`'s URL path/query.
./yat-preview fixtures/packs/test-sources-mini.yat-pack.json --doc second=fixtures/test-sources-mini.second.json --now $NOW --out /tmp/yat-sources.png 2>/tmp/yat-sources.log
grep -q 'fetch second <- https://example.com/api/AB12?owner=K' /tmp/yat-sources.log || { echo "FAIL: §5.7 backward reference not substituted into URL"; exit 1; }
grep -q '"value": 42' /tmp/yat-sources.log || { echo "FAIL: §5.7 referencing source did not extract"; exit 1; }

# b) forward reference (source `a` refers to later source `b`) must be
#    rejected with a load/eval error naming §5.7 — no request ever attempted.
cat > /tmp/yat-bad-forward-source.yat-pack.json <<'EOF'
{ "yat": 1, "id": "bad-forward-source", "name": { "en": "Bad forward source" },
  "aliases": { "en": ["bad"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {}, "additionalProperties": false },
  "data": { "sources": [
    { "id": "a", "type": "https", "url": "https://example.com/a/{{sources.b.x}}", "format": "json", "extract": { "y": "y" } },
    { "id": "b", "type": "inline", "data": { "x": 1 }, "extract": { "x": "x" } }
  ] },
  "render": { "widgets": [ { "type": "text", "value": "hi" } ] },
  "schedule": { "default": { "every_min": 60 } } }
EOF
if ./yat-preview /tmp/yat-bad-forward-source.yat-pack.json --now $NOW --out /tmp/yat-bad3.png 2>/tmp/yat-bad3.log; then
  echo "FAIL: forward source reference should be rejected"; exit 1
fi
grep -q '§5.7' /tmp/yat-bad3.log || { echo "FAIL: forward-reference error doesn't name §5.7"; exit 1; }
grep -q 'fetch a ' /tmp/yat-bad3.log && { echo "FAIL: forward reference must not attempt a fetch"; exit 1; }

# ---- v0.3: stale-serve (§11.3) + min_refresh_min (§5.1) via --state ----
# A null store (no --state, the default used by every test above) must keep
# v0.2 behavior byte-identical — this is asserted implicitly by every golden
# comparison above already running with no --state flag.
STATEDIR=/tmp/yat-state-test
rm -rf "$STATEDIR"

# a) populate: a clean fetch persists source `current`'s snapshot.
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --state "$STATEDIR" --out /tmp/yat-state1.png 2>/tmp/yat-state1.log
grep -q '"temp": 28' /tmp/yat-state1.log || { echo "FAIL: stale-serve: populate run did not extract temp"; exit 1; }
[ -f "$STATEDIR/hko-now.current.json" ] || { echo "FAIL: stale-serve: snapshot file not written"; exit 1; }

# b) fetch fails (fixture path doesn't exist) but the same state dir has a
#    snapshot on record -> stale-serve: data still shows the snapshot's temp,
#    and stderr names the stale path (both the fetch-warn and render-warn).
./yat-preview "$P" --doc current=fixtures/does-not-exist.json $WCLEAR --now $NOW --state "$STATEDIR" --out /tmp/yat-state2.png 2>/tmp/yat-state2.log
grep -q '(stale-serve)' /tmp/yat-state2.log || { echo "FAIL: stale-serve: fetch-failure warn doesn't mention stale-serve"; exit 1; }
grep -q '^stale:' /tmp/yat-state2.log || { echo "FAIL: stale-serve: no stale: log line"; exit 1; }
grep -q 'render warn: stale since' /tmp/yat-state2.log || { echo "FAIL: stale-serve: render warn doesn't mention stale"; exit 1; }
grep -q '"temp": 28' /tmp/yat-state2.log || { echo "FAIL: stale-serve: data does not show the snapshot's temp value"; exit 1; }

# c) never-succeeded (no snapshot on record for this source id) -> plain
#    nulls, NOT stale (§11.3) — a fresh state dir, first call itself fails.
STATEDIR2=/tmp/yat-state-test-never
rm -rf "$STATEDIR2"
./yat-preview "$P" --doc current=fixtures/does-not-exist.json $WCLEAR --now $NOW --state "$STATEDIR2" --out /tmp/yat-state3.png 2>/tmp/yat-state3.log
grep -q '(stale-serve)' /tmp/yat-state3.log && { echo "FAIL: never-succeeded source must not stale-serve"; exit 1; }
grep -q '^stale:' /tmp/yat-state3.log && { echo "FAIL: never-succeeded source must not set anyStale()"; exit 1; }
grep -q '"temp": null' /tmp/yat-state3.log || { echo "FAIL: never-succeeded source should extract nulls"; exit 1; }
rm -rf "$STATEDIR2"

# d) min_refresh_min (§5.1): first run fetches + persists; a second run
#    inside the interval must skip the network entirely (no "fetch quote"
#    line at all) and log the no-fetch path; a run past the interval fetches
#    again.
MR=fixtures/packs/test-min-refresh-mini.yat-pack.json
./yat-preview "$MR" --doc quote=fixtures/test-min-refresh.json --now $NOW --state "$STATEDIR" --out /tmp/yat-mr1.png 2>/tmp/yat-mr1.log
grep -q '^fetch quote ' /tmp/yat-mr1.log || { echo "FAIL: min_refresh_min: first run should fetch"; exit 1; }
grep -q '"value": 7' /tmp/yat-mr1.log || { echo "FAIL: min_refresh_min: first run extraction"; exit 1; }

./yat-preview "$MR" --now $NOW --state "$STATEDIR" --out /tmp/yat-mr2.png 2>/tmp/yat-mr2.log
grep -q '^fetch quote ' /tmp/yat-mr2.log && { echo "FAIL: min_refresh_min: second run (within interval) should not fetch"; exit 1; }
grep -q 'served from snapshot (min_refresh_min)' /tmp/yat-mr2.log || { echo "FAIL: min_refresh_min: no-fetch log line missing"; exit 1; }
grep -q '"value": 7' /tmp/yat-mr2.log || { echo "FAIL: min_refresh_min: second run should still show the snapshot value"; exit 1; }
grep -q '^stale:' /tmp/yat-mr2.log && { echo "FAIL: min_refresh_min: within-interval serve must not be stale"; exit 1; }

NOW_PAST_INTERVAL=$((NOW + 61 * 60))
./yat-preview "$MR" --doc quote=fixtures/test-min-refresh.json --now $NOW_PAST_INTERVAL --state "$STATEDIR" --out /tmp/yat-mr3.png 2>/tmp/yat-mr3.log
grep -q '^fetch quote ' /tmp/yat-mr3.log || { echo "FAIL: min_refresh_min: run past the interval should fetch again"; exit 1; }

rm -rf "$STATEDIR"

# ---- v0.3: image widget (§9.10) — fetched at RENDER time (not extract), via
# ---- the FetchFn stashed by fetchAndExtract(); image widgets have no source
# ---- id, so the native preview always fetches them as id "image" (--doc
# ---- image=file.png). Fixture is a deterministic 200x120 RGB gradient with
# ---- 4 corner swatches (tools/preview/fixtures/test-image-gradient.png).
# ---- test-image-mini declares 2 image widgets (the §9.10 [V] cap), mutually
# ---- exclusive via `when: params.fit`, so a single param switches between
# ---- fit contain (letterboxed, 400x300 box vs. a 200x120/5:3 source) and
# ---- fit stretch (fills the box, no letterbox bars) — genuinely different
# ---- renders, not just a relabeled default.
IMG=fixtures/packs/test-image-mini.yat-pack.json

./yat-preview "$IMG" --doc image=fixtures/test-image-gradient.png --now $NOW --out /tmp/yat-image-contain.png 2>/tmp/yat-image-contain.log
grep -q 'render warn' /tmp/yat-image-contain.log && { echo "FAIL: image (fit contain) render produced a warning"; exit 1; }

./yat-preview "$IMG" --doc image=fixtures/test-image-gradient.png --now $NOW --out /tmp/yat-image-contain-b.png 2>/dev/null
cmp -s /tmp/yat-image-contain.png /tmp/yat-image-contain-b.png || { echo "FAIL: nondeterministic render (image, fit contain)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-image-contain.png goldens/test-image-contain.png)
if [ -f goldens/test-image-contain.png ]; then
  cmp -s /tmp/yat-image-contain.png goldens/test-image-contain.png || { echo "FAIL: golden mismatch (goldens/test-image-contain.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/test-image-contain.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

./yat-preview "$IMG" --doc image=fixtures/test-image-gradient.png --params '{"fit":"stretch"}' --now $NOW --out /tmp/yat-image-stretch.png 2>/tmp/yat-image-stretch.log
grep -q 'render warn' /tmp/yat-image-stretch.log && { echo "FAIL: image (fit stretch) render produced a warning"; exit 1; }
if cmp -s /tmp/yat-image-contain.png /tmp/yat-image-stretch.png; then echo "FAIL: fit contain vs stretch should render differently"; exit 1; fi

# fetch failure (no --doc/--live for this run) -> §9.10 "empty box + footer
# glyph": render still succeeds (non-fatal), with a render warning naming it.
./yat-preview "$IMG" --now $NOW --out /tmp/yat-image-nofetch.png 2>/tmp/yat-image-nofetch.log
grep -q 'render warn: image fetch failed' /tmp/yat-image-nofetch.log || { echo "FAIL: image fetch failure should produce a render warning"; exit 1; }

# a fetched body that isn't a PNG must fail cleanly by name (§9.10 PNG only),
# not crash or fall through to some other decoder.
echo "not a png" > /tmp/yat-not-a-png.txt
./yat-preview "$IMG" --doc image=/tmp/yat-not-a-png.txt --now $NOW --out /tmp/yat-image-notpng.png 2>/tmp/yat-image-notpng.log
grep -q 'render warn: image: fetched body is not a PNG' /tmp/yat-image-notpng.log || { echo "FAIL: non-PNG fetch body should warn 'not a PNG'"; exit 1; }

# ---- §9.10 [V]: at most 2 image widgets per pack — a 3rd, nested inside a
# ---- column and a list row template, must still be counted and rejected.
cat > /tmp/yat-bad-image-cap.yat-pack.json <<'EOF'
{ "yat": 1, "id": "bad-image-cap", "name": { "en": "Bad image cap" },
  "aliases": { "en": ["bad"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {}, "additionalProperties": false },
  "data": { "sources": [] },
  "render": { "widgets": [
    { "type": "image", "src": "{{params.a}}", "width": 10, "height": 10 },
    { "type": "column", "children": [
      { "type": "image", "src": "{{params.a}}", "width": 10, "height": 10 },
      { "type": "list", "bind": "data.x", "row": [
        { "type": "image", "src": "{{params.a}}", "width": 10, "height": 10 }
      ] }
    ] }
  ] },
  "schedule": { "default": { "every_min": 60 } } }
EOF
if ./yat-preview /tmp/yat-bad-image-cap.yat-pack.json --now $NOW --out /tmp/yat-bad-image.png 2>/tmp/yat-bad-image.log; then
  echo "FAIL: 3 image widgets (across nesting) should be rejected at load"; exit 1
fi
grep -q '§9.10' /tmp/yat-bad-image.log || { echo "FAIL: unclear error for >2 image widgets"; exit 1; }

# ---- v0.3: icon catalog (§9.9) — every one of the 32 closed-catalog names is
# ---- a real 24x24 1-bit bitmap (engine/src/icons.cpp, authored as geometry by
# ---- tools/icons/gen_icons.py), blitted at integer scale x1/x2/x4 for size
# ---- small/medium/large. The contact-sheet fixture draws all 32 at medium with
# ---- labels, so one golden covers the whole catalog; `chrome: none` and no
# ---- sources keep it a pure function of the bitmaps (no clock, no attribution).
# ---- Regenerate the fixture + bitmaps together with:
# ----     python3 tools/icons/gen_icons.py
ICO=fixtures/packs/test-icons-sheet.yat-pack.json

./yat-preview "$ICO" --now $NOW --out /tmp/yat-icons.png 2>/tmp/yat-icons.log
grep -q 'placeholder box' /tmp/yat-icons.log && { echo "FAIL: a catalog icon still drew a placeholder box"; exit 1; }
grep -q 'render warn' /tmp/yat-icons.log && { echo "FAIL: icon sheet render produced a warning"; exit 1; }

./yat-preview "$ICO" --now $NOW --out /tmp/yat-icons-b.png 2>/dev/null
cmp -s /tmp/yat-icons.png /tmp/yat-icons-b.png || { echo "FAIL: nondeterministic render (icon sheet)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-icons.png goldens/test-icons-sheet.png)
if [ -f goldens/test-icons-sheet.png ]; then
  cmp -s /tmp/yat-icons.png goldens/test-icons-sheet.png || { echo "FAIL: golden mismatch (goldens/test-icons-sheet.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/test-icons-sheet.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# small/medium/large must be three genuinely different renders — the x1/x2/x4
# integer-scale blit of one base bitmap, not one size drawn three times.
cat > /tmp/yat-icon-sizes.yat-pack.json <<'EOF'
{ "yat": 1, "id": "icon-sizes", "name": { "en": "Icon sizes" },
  "aliases": { "en": ["icon sizes"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {
    "sz": { "type": "string", "title": "Size", "enum": ["small", "medium", "large"], "default": "small" } },
    "additionalProperties": false },
  "data": { "sources": [] },
  "render": { "chrome": "none", "widgets": [
    { "type": "icon", "name": "star", "size": "small",  "when": "params.sz == 'small'" },
    { "type": "icon", "name": "star", "size": "medium", "when": "params.sz == 'medium'" },
    { "type": "icon", "name": "star", "size": "large",  "when": "params.sz == 'large'" } ] },
  "schedule": { "default": { "every_min": 1440 } } }
EOF
for S in small medium large; do
  ./yat-preview /tmp/yat-icon-sizes.yat-pack.json --params "{\"sz\":\"$S\"}" --now $NOW --out "/tmp/yat-icon-$S.png" 2>"/tmp/yat-icon-$S.log"
  grep -q 'placeholder box' "/tmp/yat-icon-$S.log" && { echo "FAIL: icon size $S drew a placeholder box"; exit 1; }
done
cmp -s /tmp/yat-icon-small.png /tmp/yat-icon-medium.png && { echo "FAIL: icon size small and medium render identically"; exit 1; }
cmp -s /tmp/yat-icon-medium.png /tmp/yat-icon-large.png && { echo "FAIL: icon size medium and large render identically"; exit 1; }

# A name outside the catalog is unreachable from a schema-valid pack (§9.9 is a
# closed enum), but the engine must still fall back to the honest bordered box
# and say so by section rather than silently drawing nothing. Firmware-reserved
# warning-signal names (§12.3) are exactly what would land here.
cat > /tmp/yat-bad-icon.yat-pack.json <<'EOF'
{ "yat": 1, "id": "bad-icon", "name": { "en": "Bad icon" },
  "aliases": { "en": ["bad"], "zh-Hant": [], "jyutping": [] },
  "params": { "type": "object", "properties": {}, "additionalProperties": false },
  "data": { "sources": [] },
  "render": { "chrome": "none", "widgets": [ { "type": "icon", "name": "typhoon_8", "size": "large" } ] },
  "schedule": { "default": { "every_min": 1440 } } }
EOF
./yat-preview /tmp/yat-bad-icon.yat-pack.json --now $NOW --out /tmp/yat-bad-icon.png 2>/tmp/yat-bad-icon.log
grep -q '§9.9 catalog' /tmp/yat-bad-icon.log || { echo "FAIL: an off-catalog icon name should warn naming §9.9"; exit 1; }
if cmp -s /tmp/yat-bad-icon.png /tmp/yat-icon-large.png; then echo "FAIL: off-catalog icon should not draw a catalog bitmap"; exit 1; fi

# ---- v0.3: real typography (§9.3/§9.7). One sheet covering every face and
# ---- weight the engine has (16/24/32/48 regular+bold, bignum 96) plus the three
# ---- font paths no other golden touches:
# ----   * ellipsis — proportional advances mean the ellipsis has to be trimmed
# ----     INTO the box, not appended past it (the v0.1 bug);
# ----   * per-character CJK wrap vs. Latin space wrap sharing one line box;
# ----   * the two fallbacks in engine/src/fonts.cpp — a 48px CJK ideograph
# ----     served by the 24px face at 2x (no pack uses xlarge CJK, so that face
# ----     ships without ideographs), and a codepoint no face covers at all,
# ----     which must draw the tofu box rather than a silent hole.
# ---- Regenerate the font tables with:
# ----     tools/fonts/.venv/bin/python tools/fonts/gen_fonts.py   (see its README)
TYP=fixtures/packs/test-typography.yat-pack.json

./yat-preview "$TYP" --now $NOW --out /tmp/yat-typo.png 2>/tmp/yat-typo.log
grep -q 'render warn' /tmp/yat-typo.log && { echo "FAIL: typography sheet render produced a warning"; exit 1; }

./yat-preview "$TYP" --now $NOW --out /tmp/yat-typo-b.png 2>/dev/null
cmp -s /tmp/yat-typo.png /tmp/yat-typo-b.png || { echo "FAIL: nondeterministic render (typography sheet)"; exit 1; }

# The bignum face is solved so its digits measure exactly the §9.3 glyph height
# (96px for `large`); a regression in gen_fonts.py's em search would show up
# here as an off-by-several-pixels ink box and nowhere else.
python3 - <<'EOF' || exit 1
import struct, zlib, sys
def rows(path):
    d = open(path, 'rb').read()
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        if f == 1:
            for x in range(3, len(line)): line[x] = (line[x] + line[x-3]) & 255
        elif f == 2:
            for x in range(len(line)): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(len(line)): line[x] = (line[x] + ((line[x-3] if x>=3 else 0) + prev[x])//2) & 255
        elif f == 4:
            def pd(a,b,c):
                p=a+b-c; pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                return a if pa<=pb and pa<=pc else (b if pb<=pc else c)
            for x in range(len(line)):
                line[x] = (line[x] + pd(line[x-3] if x>=3 else 0, prev[x], prev[x-3] if x>=3 else 0)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
w, h, px = rows('/tmp/yat-typo.png')
ink = lambda x, y: px[y][x*3] < 128
# The bignum row is the band between the sheet's two full-width dividers, and the
# leftmost group in it is the digits-only "08" — located by scanning for the first
# wide blank gutter rather than by hardcoded pixel columns, so this keeps working
# if the face metrics ever shift.
divs = [y for y in range(h) if sum(1 for x in range(w) if ink(x, y)) > 700]
if len(divs) < 2:
    print(f"FAIL: typography sheet: expected 2 divider rows, found {len(divs)}"); sys.exit(1)
band = range(divs[0]+1, divs[1])
cols = [x for x in range(w) if any(ink(x, y) for y in band)]
if not cols:
    print("FAIL: typography sheet: no bignum ink between the dividers"); sys.exit(1)
right = cols[0]
for x in cols:
    if x - right > 20: break   # first gutter wider than 20px ends the "08" group
    right = x
ys = [y for y in band if any(ink(x, y) for x in range(cols[0], right+1))]
digits = ys[-1] - ys[0] + 1
if digits != 96:
    print(f"FAIL: bignum `large` digit ink height is {digits}px, expected 96 (§9.3)"); sys.exit(1)
EOF

# golden compare (regenerate with: cp /tmp/yat-typo.png goldens/test-typography.png)
if [ -f goldens/test-typography.png ]; then
  cmp -s /tmp/yat-typo.png goldens/test-typography.png || { echo "FAIL: golden mismatch (goldens/test-typography.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/test-typography.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- §9.3(5) clipping. The regression this guards was found on-device: a
# ---- `temp-trend` whose bar rows added up to more than the content area drew
# ---- the tail rows straight over the standard-chrome footer — the engine
# ---- positioned children but nothing ever clipped them, so the panel showed
# ---- bars sitting on top of the source attribution. This pack overflows by
# ---- design (ten 30px bars + a paragraph in a 404px area); with the clip in
# ---- place the footer band belongs to the chrome alone, and the paragraph —
# ---- which starts below the content bottom — must not appear at all.
CLIP=fixtures/packs/test-clip-overflow.yat-pack.json
./yat-preview "$CLIP" --now $NOW --out /tmp/yat-clip.png 2>/tmp/yat-clip.log
grep -q 'layout overflow' /tmp/yat-clip.log || { echo "FAIL: clip-overflow pack rendered without reporting the overflow"; exit 1; }

python3 - <<'EOF' || exit 1
import struct, zlib, sys
def rows(path):
    d = open(path, 'rb').read()
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        if f == 1:
            for x in range(3, len(line)): line[x] = (line[x] + line[x-3]) & 255
        elif f == 2:
            for x in range(len(line)): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(len(line)): line[x] = (line[x] + ((line[x-3] if x>=3 else 0) + prev[x])//2) & 255
        elif f == 4:
            def pd(a,b,c):
                p=a+b-c; pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                return a if pa<=pb and pa<=pc else (b if pb<=pc else c)
            for x in range(len(line)):
                line[x] = (line[x] + pd(line[x-3] if x>=3 else 0, prev[x], prev[x-3] if x>=3 else 0)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
w, h, px = rows('/tmp/yat-clip.png')
ink = lambda x, y: px[y][x*3] < 128
# Below the footer rule (h-32) the chrome owns two clusters and nothing in
# between: the voice hint hard left (this pack's aliases are one short word per
# language) and the brand mark hard right; an inline source contributes no
# attribution at all. Columns 300-599 of that band are therefore chrome-free,
# and the overflowing paragraph — which wraps the full content width — put ink
# across ~223 of them before the engine clipped.
strays = [(x, y) for y in range(h-31, h) for x in range(300, 600) if ink(x, y)]
if strays:
    print(f"FAIL: {len(strays)} content pixel(s) escaped into the footer band, "
          f"first at x={strays[0][0]} y={strays[0][1]} (§9.3(5) clipping)"); sys.exit(1)
# ...and the clip must not have eaten the chrome's own footer rule with it.
if sum(1 for x in range(w) if ink(x, h-32)) < 700:
    print("FAIL: footer separator rule is missing or partial"); sys.exit(1)
EOF

./yat-preview "$CLIP" --now $NOW --out /tmp/yat-clip-b.png 2>/dev/null
cmp -s /tmp/yat-clip.png /tmp/yat-clip-b.png || { echo "FAIL: nondeterministic render (clip overflow)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-clip.png goldens/test-clip-overflow.png)
if [ -f goldens/test-clip-overflow.png ]; then
  cmp -s /tmp/yat-clip.png goldens/test-clip-overflow.png || { echo "FAIL: golden mismatch (goldens/test-clip-overflow.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/test-clip-overflow.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- §9.12a `chart`, line mode: every case on one sheet — auto-scale, pinned
# ---- min/max, a flat series (mid-box, not a divide-by-zero), gaps (a null
# ---- hour breaks the line instead of diving to zero), numeric strings from
# ---- string-typed feeds, a single sample, and the two honest failures
# ---- (nothing numeric -> E_TYPE, bound to a scalar -> E_BIND). The last two
# ---- draw an empty box and say so; they are the reason the warning strings
# ---- are asserted rather than just the pixels.
CHT=fixtures/packs/test-chart-sheet.yat-pack.json
./yat-preview "$CHT" --now $NOW --out /tmp/yat-chart.png 2>/tmp/yat-chart.log
grep -q 'E_TYPE: chart series has no numeric values' /tmp/yat-chart.log || { echo "FAIL: chart: all-non-numeric series did not report E_TYPE"; exit 1; }
grep -q "E_BIND: chart bind 'data.scalar' is not an array" /tmp/yat-chart.log || { echo "FAIL: chart: scalar bind did not report E_BIND"; exit 1; }
grep -q 'layout overflow' /tmp/yat-chart.log && { echo "FAIL: chart sheet overflows its own content box"; exit 1; }

./yat-preview "$CHT" --now $NOW --out /tmp/yat-chart-b.png 2>/dev/null
cmp -s /tmp/yat-chart.png /tmp/yat-chart-b.png || { echo "FAIL: nondeterministic render (chart sheet)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-chart.png goldens/test-chart-sheet.png)
if [ -f goldens/test-chart-sheet.png ]; then
  cmp -s /tmp/yat-chart.png goldens/test-chart-sheet.png || { echo "FAIL: golden mismatch (goldens/test-chart-sheet.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/test-chart-sheet.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- temp-trend: the pack the clipping bug was reported against, now a line
# ---- chart of the day (§9.12a) instead of eight hand-unrolled bar rows. The
# ---- chart takes `flex: 1`, so the page cannot overflow however the hourly
# ---- series moves — which is what the no-warning assertion below pins down.
# ---- temp-trend is one of the 17 packs that moved to yat-hk/yat-packs; the
# ---- copy under testpacks/ is a frozen fixture for this regression and the
# ---- winter auto-scale case below — see testpacks/README.md.
TT=testpacks/temp-trend.yat-pack.json
./yat-preview "$TT" --doc meteo=fixtures/temp-trend.meteo.json --now $NOW --out /tmp/yat-tt.png 2>/tmp/yat-tt.log
grep -q '"temps": \[' /tmp/yat-tt.log || { echo "FAIL: temp-trend: hourly series did not extract as an array"; exit 1; }
grep -q 'layout overflow' /tmp/yat-tt.log && { echo "FAIL: temp-trend overflows its content box (the reported bug)"; exit 1; }
grep -q 'render warn' /tmp/yat-tt.log && { echo "FAIL: temp-trend render produced a warning"; exit 1; }

./yat-preview "$TT" --doc meteo=fixtures/temp-trend.meteo.json --now $NOW --out /tmp/yat-tt-b.png 2>/dev/null
cmp -s /tmp/yat-tt.png /tmp/yat-tt-b.png || { echo "FAIL: nondeterministic render (temp-trend)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-tt.png goldens/temp-trend.png)
if [ -f goldens/temp-trend.png ]; then
  cmp -s /tmp/yat-tt.png goldens/temp-trend.png || { echo "FAIL: golden mismatch (goldens/temp-trend.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/temp-trend.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- user-reported bug: "the line chart always show same chart". Root cause
# ---- was NOT the extraction (the hourly series does vary), NOT a fixed time
# ---- window (Open-Meteo's forecast_days=1 window tracks the real calendar
# ---- day), and NOT an engine bug that ignores `bind` (a genuinely different
# ---- series does move the polyline — see the `test-chart-sheet` cases
# ---- above). It was the chart's default **auto-scale**: every render fits
# ---- the series' own min/max into the same box, so a 25-28C summer day and a
# ---- 10-18C winter day both draw the identical hump filling the identical
# ---- pixel band — proven by rendering both against the unpinned 2.0.0 pack
# ---- and finding the ink occupied rows 260-393 in *both*. §9.12a says to pin
# ---- `min`/`max` whenever the absolute level is the point, which for a page
# ---- whose whole job is "how hot is it today" it clearly is; 2.1.0 pins the
# ---- chart to an 8-36C scale (HKO's own cold/hot-weather-warning bracket,
# ---- widened for headroom) so distinct actual temperatures land in visibly
# ---- distinct places on the panel instead of both filling the box the same.
# ---- This block renders a second, wildly different fixture (a cold snap) at
# ---- the same pinned `--now` and asserts the plotted line itself — not just
# ---- the header digits — moved to a different vertical band.
./yat-preview "$TT" --doc meteo=fixtures/temp-trend.meteo-winter.json --now $NOW --out /tmp/yat-tt-winter.png 2>/tmp/yat-tt-winter.log
grep -q '"temp_now": 14' /tmp/yat-tt-winter.log || { echo "FAIL: temp-trend winter fixture: temp_now extraction"; exit 1; }
grep -q '"hi_today": 18' /tmp/yat-tt-winter.log || { echo "FAIL: temp-trend winter fixture: hi_today extraction"; exit 1; }
grep -q '"lo_today": 10' /tmp/yat-tt-winter.log || { echo "FAIL: temp-trend winter fixture: lo_today extraction"; exit 1; }
grep -q 'render warn' /tmp/yat-tt-winter.log && { echo "FAIL: temp-trend winter render produced a warning"; exit 1; }
if cmp -s /tmp/yat-tt.png /tmp/yat-tt-winter.png; then echo "FAIL: summer and winter fixtures render byte-identical (the reported bug)"; exit 1; fi

./yat-preview "$TT" --doc meteo=fixtures/temp-trend.meteo-winter.json --now $NOW --out /tmp/yat-tt-winter-b.png 2>/dev/null
cmp -s /tmp/yat-tt-winter.png /tmp/yat-tt-winter-b.png || { echo "FAIL: nondeterministic render (temp-trend winter)"; exit 1; }

python3 - <<'EOF' || exit 1
import struct, zlib, sys
def rows(path):
    d = open(path, 'rb').read()
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        if f == 1:
            for x in range(3, len(line)): line[x] = (line[x] + line[x-3]) & 255
        elif f == 2:
            for x in range(len(line)): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(len(line)): line[x] = (line[x] + ((line[x-3] if x>=3 else 0) + prev[x])//2) & 255
        elif f == 4:
            def pd(a,b,c):
                p=a+b-c; pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                return a if pa<=pb and pa<=pc else (b if pb<=pc else c)
            for x in range(len(line)):
                line[x] = (line[x] + pd(line[x-3] if x>=3 else 0, prev[x], prev[x-3] if x>=3 else 0)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
# Chart plot band is roughly y 260-400 (above the 00:00/21:00 label row).
# temp-trend 2.2.0 turned on `show_bounds`, which paints the pinned 36/8
# labels at the box's top-left/bottom-left corners in the *same* rows on
# every render (the scale is pinned, so the labels never move) — exactly
# the fixed-pixel-band signature this test exists to catch, but from a
# label instead of the auto-scale bug. Starting the x-scan at 60 clears
# the two-character label's width (it sits flush against the box's left
# edge) so this only ever sees the polyline itself.
def top_ink_row(path):
    w, h, px = rows(path)
    for y in range(260, 400):
        if any(px[y][x*3] < 128 for x in range(60, w, 2)):
            return y
    return None
summer_top = top_ink_row('/tmp/yat-tt.png')
winter_top = top_ink_row('/tmp/yat-tt-winter.png')
if summer_top is None or winter_top is None:
    print("FAIL: could not find chart ink in either render"); sys.exit(1)
# The colder fixture's line must sit measurably lower (larger y) than the
# hotter one now that the scale is pinned instead of auto-fit per render.
if winter_top - summer_top < 20:
    print(f"FAIL: chart did not shift with absolute temperature (summer top={summer_top}, "
          f"winter top={winter_top}, diff={winter_top - summer_top} < 20px) — auto-scale bug regressed")
    sys.exit(1)
EOF

# golden compare (regenerate with: cp /tmp/yat-tt-winter.png goldens/temp-trend-winter.png)
if [ -f goldens/temp-trend-winter.png ]; then
  cmp -s /tmp/yat-tt-winter.png goldens/temp-trend-winter.png || { echo "FAIL: golden mismatch (goldens/temp-trend-winter.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/temp-trend-winter.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- §11.3 empty state: "the engine never renders a blank page". When EVERY
# ---- source failed with no snapshot to fall back on, the content band gets a
# ---- fallback card instead of the pack's widgets — which in that state would
# ---- substitute empty strings into every placeholder and leave a page-shaped
# ---- ghost. The four states this has to tell apart are the four asserted
# ---- here, and only the first is the card: all-failed-nothing-on-record;
# ---- one source still answering; stale-serve (old content + the footer
# ---- badge, §11.3's own answer, which must NOT be overridden); and
# ---- `when`-skipped sources, which the taxonomy says are not failures at all.
# ---- test-empty-state declares two https sources, both `when`-gated on one
# ---- param, so a single pack drives all four.
ES=fixtures/packs/test-empty-state.yat-pack.json

# a) both sources fail, no --state -> the card.
./yat-preview "$ES" --now $NOW --out /tmp/yat-es-card.png 2>/tmp/yat-es-card.log
grep -q 'render warn: empty state' /tmp/yat-es-card.log || { echo "FAIL: empty state: all-sources-failed did not draw the fallback card"; exit 1; }
grep -q '^empty:' /tmp/yat-es-card.log || { echo "FAIL: empty state: no machine-greppable empty: log line"; exit 1; }

./yat-preview "$ES" --now $NOW --out /tmp/yat-es-card-b.png 2>/dev/null
cmp -s /tmp/yat-es-card.png /tmp/yat-es-card-b.png || { echo "FAIL: nondeterministic render (empty-state card)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-es-card.png goldens/test-empty-state.png)
if [ -f goldens/test-empty-state.png ]; then
  cmp -s /tmp/yat-es-card.png goldens/test-empty-state.png || { echo "FAIL: golden mismatch (goldens/test-empty-state.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/test-empty-state.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# b) SOME data -> render normally. One source answering is a page, however
#    thin; the card is only for a page with nothing at all.
./yat-preview "$ES" --doc alpha=fixtures/test-empty-state.alpha.json --now $NOW --out /tmp/yat-es-some.png 2>/tmp/yat-es-some.log
grep -q 'empty state' /tmp/yat-es-some.log && { echo "FAIL: empty state: one working source must not draw the card"; exit 1; }
grep -q '"a": "alpha answered"' /tmp/yat-es-some.log || { echo "FAIL: empty state: the working source did not extract"; exit 1; }
if cmp -s /tmp/yat-es-card.png /tmp/yat-es-some.png; then echo "FAIL: empty state: card and normal render are identical"; exit 1; fi

# c) stale-serve is NOT empty: a snapshot on record is content, and §11.3
#    already answers for it with the footer badge. Populate, then fail both.
ESSTATE=/tmp/yat-es-state
rm -rf "$ESSTATE"
./yat-preview "$ES" --doc alpha=fixtures/test-empty-state.alpha.json --doc beta=fixtures/test-empty-state.beta.json --now $NOW --state "$ESSTATE" --out /tmp/yat-es-pop.png 2>/tmp/yat-es-pop.log
grep -q 'empty state' /tmp/yat-es-pop.log && { echo "FAIL: empty state: a fully successful fetch drew the card"; exit 1; }
./yat-preview "$ES" --now $NOW --state "$ESSTATE" --out /tmp/yat-es-stale.png 2>/tmp/yat-es-stale.log
grep -q 'empty state' /tmp/yat-es-stale.log && { echo "FAIL: empty state: stale-serve must render the page, not the card"; exit 1; }
grep -q '^empty:' /tmp/yat-es-stale.log && { echo "FAIL: empty state: stale-serve must not be reported as empty"; exit 1; }
grep -q 'render warn: stale since' /tmp/yat-es-stale.log || { echo "FAIL: empty state: stale-serve lost its own badge"; exit 1; }
grep -q '"a": "alpha answered"' /tmp/yat-es-stale.log || { echo "FAIL: empty state: stale-serve did not serve the snapshot"; exit 1; }
rm -rf "$ESSTATE"

# d) `when`-skipped sources are not failures (§11.3 taxonomy). Every field is
#    null and the page renders empty-ish — but nothing went wrong, so telling
#    the household "can't reach this page's data" would be a lie.
./yat-preview "$ES" --params '{"mode":"skip"}' --now $NOW --out /tmp/yat-es-skip.png 2>/tmp/yat-es-skip.log
grep -q '^empty:' /tmp/yat-es-skip.log && { echo "FAIL: empty state: when-skipped sources must not count as failures"; exit 1; }
grep -q 'render warn' /tmp/yat-es-skip.log && { echo "FAIL: empty state: a clean when-skip should render clean"; exit 1; }

# e) a pack with NO sources can never be empty in this sense — test-image-mini
#    declares zero, so its image-fetch failure must stay an image warning and
#    never escalate to the page-level card (which would swallow the widget
#    that is the whole point of that pack).
grep -q 'empty state' /tmp/yat-image-nofetch.log && { echo "FAIL: empty state: a zero-source pack must never draw the card"; exit 1; }

# f) the real thing: hko-now with BOTH sources down and nothing on record —
#    what a household sees when HKO is unreachable on a cold device. Also the
#    case a "is every field null?" test gets wrong: hko-now's `compute` fields
#    resolve to "" (not null) even here, so the predicate has to be the
#    per-source outcome tally, not the shape of data().
./yat-preview "$P" --doc current=fixtures/does-not-exist.json --doc warnsum=fixtures/does-not-exist.json --now $NOW --out /tmp/yat-hko-empty.png 2>/tmp/yat-hko-empty.log
grep -q 'render warn: empty state' /tmp/yat-hko-empty.log || { echo "FAIL: empty state: hko-now with both sources down did not draw the card"; exit 1; }
grep -q '"warn_a": ""' /tmp/yat-hko-empty.log || { echo "FAIL: empty state: expected hko-now's compute fields to be non-null here"; exit 1; }

./yat-preview "$P" --doc current=fixtures/does-not-exist.json --doc warnsum=fixtures/does-not-exist.json --now $NOW --out /tmp/yat-hko-empty-b.png 2>/dev/null
cmp -s /tmp/yat-hko-empty.png /tmp/yat-hko-empty-b.png || { echo "FAIL: nondeterministic render (hko-now empty state)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-hko-empty.png goldens/hko-now-empty.png)
if [ -f goldens/hko-now-empty.png ]; then
  cmp -s /tmp/yat-hko-empty.png goldens/hko-now-empty.png || { echo "FAIL: golden mismatch (goldens/hko-now-empty.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/hko-now-empty.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- §9.10 image widget: photo-frame's whole body is one 800x404 image
# ---- widget, so both failure modes matter — an empty frame with the reason
# ---- inside it (fetch failure) and a real decoded photo (fetched at RENDER
# ---- time, not extraction). photo-frame is one of the 17 packs that moved to
# ---- yat-hk/yat-packs; its own per-pack appearance goldens moved with it.
# ---- What stays here is the ink-floor check below (a placeholder must be
# ---- visible ink, not a relabeled blank) and the with-image render, which
# ---- the --profile e1001 trio further down reuses as its baseline — see
# ---- testpacks/README.md.
PF=testpacks/photo-frame.yat-pack.json
PFPARAMS='{"image_url":"https://example.com/photo.png","fit":"contain"}'

./yat-preview "$PF" --params "$PFPARAMS" --now $NOW --out /tmp/yat-pf-noimage.png 2>/tmp/yat-pf-noimage.log
grep -q 'render warn: image fetch failed' /tmp/yat-pf-noimage.log || { echo "FAIL: photo-frame: a failed image fetch should still warn"; exit 1; }

./yat-preview "$PF" --params "$PFPARAMS" --doc image=fixtures/photo-frame.image.png --now $NOW --out /tmp/yat-pf-image.png 2>/tmp/yat-pf-image.log
grep -q 'render warn' /tmp/yat-pf-image.log && { echo "FAIL: photo-frame (with image) render produced a warning"; exit 1; }

./yat-preview "$PF" --params "$PFPARAMS" --doc image=fixtures/photo-frame.image.png --now $NOW --out /tmp/yat-pf-image-b.png 2>/dev/null
cmp -s /tmp/yat-pf-image.png /tmp/yat-pf-image-b.png || { echo "FAIL: nondeterministic render (photo-frame with image)"; exit 1; }

# the fetched-image state must not look like the fetch-failure placeholder
if cmp -s /tmp/yat-pf-noimage.png /tmp/yat-pf-image.png; then echo "FAIL: photo-frame with-image render is identical to the no-image placeholder"; exit 1; fi

# The placeholder has to be *ink*, not a relabeled blank: a golden compare
# alone would happily lock in an empty content band. Same content-area pixel
# count the clear-page assertion above uses, applied to both new surfaces.
python3 - <<'EOF' || exit 1
import struct, sys, zlib
def load(p):
    d = open(p, 'rb').read(); pos = 8; idat = b''; w = h = 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ, dat = d[pos+4:pos+8], d[pos+8:pos+8+ln]
        if typ == b'IHDR': w, h = struct.unpack('>II', dat[:8])
        if typ == b'IDAT': idat += dat
        pos += 12 + ln
    raw = zlib.decompress(idat); stride = w * 3; rows = []; prev = bytearray(stride); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+stride]); i += stride
        for x in range(stride):
            a = line[x-3] if x >= 3 else 0
            b = prev[x]
            c = prev[x-3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p_ = a + b - c
                pa, pb, pc = abs(p_-a), abs(p_-b), abs(p_-c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, rows

for path, floor, what in (('/tmp/yat-hko-empty.png', 2000, 'empty-state card'),
                          ('/tmp/yat-pf-noimage.png', 2000, 'image placeholder')):
    w, h, rows = load(path)
    # Content area only (§9.1: 44px header, 32px footer) — the chrome always draws.
    ink = sum(1 for y in range(44, h-32) for x in range(w)
              if rows[y][x*3:x*3+3] != b'\xff\xff\xff')
    if ink < floor:
        print(f"FAIL: {what} put only {ink} pixels in the content band — that is still a blank page")
        sys.exit(1)
EOF

# ---- §9.1a semantic ink roles. The role table (paintForRole, engine/src/
# ---- render.cpp) is phase 1 of the colour system and the point a device
# ---- capability profile will later swap, so it needs a test that fails the
# ---- moment a role stops meaning what the shipped packs already draw.
# ---- test-ink-roles renders every role through icon/text/bar/divider, taking
# ---- each colour from a param whose enum holds BOTH spellings — so the same
# ---- pack rendered with role names and with the raw inks they map to is the
# ---- table, asserted as a byte comparison rather than as a comment.
IR=fixtures/packs/test-ink-roles.yat-pack.json
# Five of the six roles are still exactly their ink. `warn` is the one that
# is not, and cannot be: §9.1a-a — yellow measures ~1.1:1 against this panel's
# white, so `warn` marks in black and spends its yellow on the field behind
# them. It stays spelled as the role here; the raw-yellow case is asserted
# separately below, in the direction that now matters (they must DIFFER).
IRRAW='{"c_accent":"red","c_good":"green","c_warn":"warn","c_danger":"red","c_info":"blue","c_muted":"black"}'

./yat-preview "$IR" --now $NOW --out /tmp/yat-roles.png 2>/tmp/yat-roles.log
grep -q 'render warn\|not implemented' /tmp/yat-roles.log && { echo "FAIL: ink-roles render produced a warning"; exit 1; }

./yat-preview "$IR" --params "$IRRAW" --now $NOW --out /tmp/yat-roles-raw.png 2>/tmp/yat-roles-raw.log
grep -q 'render warn' /tmp/yat-roles-raw.log && { echo "FAIL: ink-roles (raw spelling) render produced a warning"; exit 1; }
cmp -s /tmp/yat-roles.png /tmp/yat-roles-raw.png || { echo "FAIL: role names and raw inks rendered differently — the §9.1a table moved"; exit 1; }

# ---- §9.1a-a: `warn` is a black mark on a yellow field, and an ink literal
# ---- is still the ink. Asserted as pixels, because this is the one rule the
# ---- panel itself enforces and a byte compare alone would not say why:
# ----   * role-spelled: yellow appears in exactly ONE horizontal band (the
# ----     bar's fill) and nowhere else — no yellow icon, no yellow type, no
# ----     yellow rule, because a yellow mark on white is not visible on the
# ----     hardware at all;
# ----   * ink-spelled ("yellow"): yellow appears in SEVERAL bands, because a
# ----     pack that names the ink outright has told the engine the colour is
# ----     the identity of the thing (livery, an official amber signal) and
# ----     gets it, marks included.
./yat-preview "$IR" --params '{"c_warn":"yellow"}' --now $NOW --out /tmp/yat-roles-ylw.png 2>/tmp/yat-roles-ylw.log
if cmp -s /tmp/yat-roles.png /tmp/yat-roles-ylw.png; then
  echo "FAIL: color 'warn' and color 'yellow' rendered identically — §9.1a-a's whole point is that they must not"; exit 1
fi
python3 - <<'EOF' || exit 1
import struct, sys, zlib
def rows(path):
    d = open(path, 'rb').read(); pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        for x in range(w*3):
            a = line[x-3] if x >= 3 else 0; b = prev[x]; c = prev[x-3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b)//2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
YELLOW = b'\xfa\xe8\x98'   # tools/preview's palette entry for Ink::Yellow
def bands(path):
    """contiguous row ranges that contain any yellow pixel"""
    w, h, px = rows(path)
    hit = [any(px[y][x*3:x*3+3] == YELLOW for x in range(w)) for y in range(h)]
    out, start = [], None
    for y, v in enumerate(hit):
        if v and start is None: start = y
        elif not v and start is not None: out.append((start, y - 1)); start = None
    if start is not None: out.append((start, h - 1))
    return out
role = bands('/tmp/yat-roles.png')
ink  = bands('/tmp/yat-roles-ylw.png')
if len(role) != 1:
    print(f"FAIL: `warn` put yellow in {len(role)} band(s) {role} — §9.1a-a allows exactly one, the bar's field"); sys.exit(1)
if role[0][1] - role[0][0] + 1 > 14:
    print(f"FAIL: `warn`'s yellow band {role[0]} is taller than the 14px bar — a mark got painted yellow"); sys.exit(1)
if len(ink) <= len(role):
    print(f"FAIL: color 'yellow' produced {len(ink)} yellow band(s), same or fewer than the role's {len(role)} — the ink literal stopped being the ink"); sys.exit(1)
EOF

# golden compare (regenerate with: cp /tmp/yat-roles.png goldens/test-ink-roles.png)
if [ -f goldens/test-ink-roles.png ]; then
  cmp -s /tmp/yat-roles.png goldens/test-ink-roles.png || { echo "FAIL: golden mismatch (goldens/test-ink-roles.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/test-ink-roles.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- §9.1a-a, settled on hardware: yellow works as a SURFACE and fails as a
# ---- STROKE. Three behaviours follow, and this asserts all three off one
# ---- render (test-ink-roles with the `warn` row spelled as the ink literal
# ---- "yellow", so a single page carries yellow type, a yellow bar fill, a
# ---- yellow icon and a yellow divider):
# ----   * TYPE is never painted yellow. It becomes black glyphs on a yellow
# ----     plain BLACK glyphs (second revision: the highlight band broke column
# ----     coherence on hardware); yellow never reaches the panel at stroke scale.
# ----   * SURFACES (bar fill, icon bitmap, the band itself) stay PURE yellow —
# ----     proved by yellow surviving in the slots the amber weave would have
# ----     turned red.
# ----   * STROKES with no field to fall back on (a `divider`, a chart line)
# ----     ARE woven. A woven row is red and yellow strictly alternating, so its
# ----     longest solid red run is 1-2px; a native red fill (the accent and
# ----     danger rows of this same page) runs solid for hundreds. That
# ----     difference is the whole discriminator — nothing else tells a woven
# ----     divider from an ordinary red one.
python3 - <<'EOF' || exit 1
import struct, sys, zlib
def rows(path):
    d = open(path, 'rb').read(); pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        for x in range(w*3):
            a = line[x-3] if x >= 3 else 0; b = prev[x]; c = prev[x-3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b)//2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
# engine/src/render.cpp: kBayer8, and kAmber's red share (candidate D, 32/64).
BAYER = [0,32,8,40,2,34,10,42,48,16,56,24,50,18,58,26,
         12,44,4,36,14,46,6,38,60,28,52,20,62,30,54,22,
         3,35,11,43,1,33,9,41,51,19,59,27,49,17,57,25,
         15,47,7,39,13,45,5,37,63,31,55,23,61,29,53,21]
RED_SLOTS = 32
YELLOW = b'\xfa\xe8\x98'
RED = b'\xc0\x1e\x28'
w, h, px = rows('/tmp/yat-roles-ylw.png')
surface_yellow = 0   # pure yellow surviving where the weave would have reddened
widest_yellow = 0
woven_rows = []
for y in range(h):
    row = px[y]
    run = ys = rs = solid_red = cur_red = 0
    for x in range(w):
        c = row[x*3:x*3+3]
        if c == YELLOW:
            ys += 1; run += 1
            if run > widest_yellow: widest_yellow = run
            if BAYER[(y & 7) * 8 + (x & 7)] < RED_SLOTS: surface_yellow += 1
        else:
            run = 0
            if c == RED: rs += 1
        cur_red = cur_red + 1 if c == RED else 0
        if cur_red > solid_red: solid_red = cur_red
    if ys and rs and solid_red <= 2: woven_rows.append(y)
# §9.1a-a second revision: yellow type clamps to plain BLACK (the highlight
# band broke column coherence on the real panel). So the assertion flips:
# every yellow pixel on the page must belong to a SURFACE (a wide run — the
# bar fill) or to a woven stroke row; yellow at glyph-stroke scale anywhere
# means the clamp regressed.
# Icon bitmaps live in the left column and legitimately paint thin yellow
# segments, so the stroke hunt starts at x=80 — past every icon slot, inside
# the type region. Glyph strokes are <=8px; a surface (bar) run crosses the
# boundary wide.
stroke_yellow_rows = 0
for y in range(h):
    row = px[y]; run = 0; runs = []
    for x in range(80, w):
        if row[x*3:x*3+3] == YELLOW: run += 1
        else:
            if run: runs.append(run)
            run = 0
    if run: runs.append(run)
    if runs and max(runs) <= 8 and y not in woven_rows:
        stroke_yellow_rows += 1
if stroke_yellow_rows > 2:
    print(f"FAIL: {stroke_yellow_rows} rows carry yellow only at stroke scale (<40px runs) — "
          f"yellow type is reaching the panel as glyphs again (§9.1a-a: clamps to black)")
    sys.exit(1)
if surface_yellow == 0:
    print("FAIL: no pure yellow survives in the amber pattern's red slots — a SURFACE is being "
          "woven. Fills, fields and bars must reach the panel as the ink the pack asked for")
    sys.exit(1)
if not woven_rows:
    print("FAIL: no woven stroke found — a yellow `divider` must be amber-woven, since it is "
          "the one shape with no field to put behind it"); sys.exit(1)
EOF

# §9.1 `color` param form. Specified and schema'd in 0.3, never implemented:
# an object is not a string, so every param-coloured widget drew black and
# stock-ticker's user-chosen up/down colours were black on every panel. A
# param that names a different colour must change the render.
./yat-preview "$IR" --params '{"c_danger":"green"}' --now $NOW --out /tmp/yat-roles-swap.png 2>/dev/null
if cmp -s /tmp/yat-roles.png /tmp/yat-roles-swap.png; then echo "FAIL: color param form did not change the render"; exit 1; fi

# A colour name that resolves to nothing is a warning, not a silent black:
# invisible in review, indistinguishable on the panel from a deliberate black.
./yat-preview "$IR" --params '{"c_warn":"chartreuse"}' --now $NOW --out /tmp/yat-roles-junk.png 2>/tmp/yat-roles-junk.log
grep -q "unknown color 'chartreuse'" /tmp/yat-roles-junk.log || { echo "FAIL: unknown colour name did not warn"; exit 1; }

# ---- sushiro-queue: the wait-time column read ragged (numbers not aligned
# ---- into a column, so magnitude wasn't glanceable) because `align: "right"`
# ---- on the combined "{{item.wait}} {{strings.wait_min}}" string is a no-op
# ---- when the text box is sized to its own content (the row-is-only-as-
# ---- wide-as-its-content trap) — right-alignment only matters when there is
# ---- slack inside the box, and there wasn't any. Padding the number to a
# ---- fixed 2-character width (`pad(2,' ')`) equalizes the combined string's
# ---- length across rows instead, which shifts the *box position* (via the
# ---- row's flex distribution) so the ones-digit lands in the same column
# ---- for every row, tens digit (10, 15) extending left of it — the standard
# ---- right-justified-number-column look. sushiro-queue is one of the 17
# ---- packs that moved to yat-hk/yat-packs; the copy under testpacks/ is a
# ---- frozen fixture for this layout regression and for one leg of the
# ---- --profile e1001 trio below — see testpacks/README.md.
SQ=testpacks/sushiro-queue.yat-pack.json
./yat-preview "$SQ" --doc stores=fixtures/sushiro-queue.stores.json --now $NOW --out /tmp/yat-sq.png 2>/tmp/yat-sq.log
grep -q 'render warn' /tmp/yat-sq.log && { echo "FAIL: sushiro-queue render produced a warning"; exit 1; }

./yat-preview "$SQ" --doc stores=fixtures/sushiro-queue.stores.json --now $NOW --out /tmp/yat-sq-b.png 2>/dev/null
cmp -s /tmp/yat-sq.png /tmp/yat-sq-b.png || { echo "FAIL: nondeterministic render (sushiro-queue)"; exit 1; }

python3 - <<'EOF' || exit 1
import struct, zlib, sys
def rows(path):
    d = open(path, 'rb').read()
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        if f == 1:
            for x in range(3, len(line)): line[x] = (line[x] + line[x-3]) & 255
        elif f == 2:
            for x in range(len(line)): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(len(line)): line[x] = (line[x] + ((line[x-3] if x>=3 else 0) + prev[x])//2) & 255
        elif f == 4:
            def pd(a,b,c):
                p=a+b-c; pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                return a if pa<=pb and pa<=pc else (b if pb<=pc else c)
            for x in range(len(line)):
                line[x] = (line[x] + pd(line[x-3] if x>=3 else 0, prev[x], prev[x-3] if x>=3 else 0)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
w, h, px = rows('/tmp/yat-sq.png')
def col_has_ink(x, y0, y1):
    for y in range(y0, y1):
        r, g, b = px[y][x*3], px[y][x*3+1], px[y][x*3+2]
        if not (r > 240 and g > 240 and b > 240): return True
    return False
# Five rows, waits 5,5,0,10,15 (fixtures/sushiro-queue.stores.json), each ~54px
# apart starting ~y=65 (colour varies per row — good/warn — so ink is
# detected as "not white" on any channel, not by a fixed dark threshold).
# The ones-digit occupies x 548-563 in every row; the tens digit (only
# present for the two-digit waits, 10 and 15) sits to its left at x 535-545.
# Right-justifying the number column means: ones-digit band lit in all 5
# rows, tens-digit band lit only in the two 2-digit rows.
row_ys = [70, 124, 178, 232, 286]
expect_tens = [False, False, False, True, True]  # waits: 5, 5, 0, 10, 15
for y0, want_tens in zip(row_ys, expect_tens):
    if not any(col_has_ink(x, y0, y0 + 16) for x in range(548, 563)):
        print(f"FAIL: ones-digit column (x 548-563) has no ink at y~{y0} — column alignment broke")
        sys.exit(1)
    got_tens = any(col_has_ink(x, y0, y0 + 16) for x in range(535, 545))
    if got_tens != want_tens:
        print(f"FAIL: tens-digit band at y~{y0} expected ink={want_tens}, got={got_tens} — "
              "the ragged wait-time column bug regressed")
        sys.exit(1)
EOF

# golden compare (regenerate with: cp /tmp/yat-sq.png goldens/sushiro-queue.png)
if [ -f goldens/sushiro-queue.png ]; then
  cmp -s /tmp/yat-sq.png goldens/sushiro-queue.png || { echo "FAIL: golden mismatch (goldens/sushiro-queue.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/sushiro-queue.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- commute-combo: three independent operators on one page — KMB is a
# ---- for_each source (one fetch per configured stop, collected into
# ---- data.kmb_etas), Citybus and MTR are each a single `when`-gated source
# ---- (show_ctb / show_mtr) that drops out of the page entirely when off.
# ---- commute-combo is one of the 17 packs that moved to yat-hk/yat-packs;
# ---- its own per-pack appearance golden moved with it. What stays is the
# ---- full-success baseline render below, which the for_each partial/bindfail
# ---- tests further down (the RFC-foreach-partial-results.md pair) compare
# ---- their degraded renders against — see testpacks/README.md.
CC=testpacks/commute-combo.yat-pack.json
CCDOC="--doc kmb=fixtures/commute-combo.kmb.1.json,fixtures/commute-combo.kmb.2.json --doc ctb=fixtures/commute-combo.ctb.json --doc mtr=fixtures/commute-combo.mtr.json"

./yat-preview "$CC" $CCDOC --now $NOW --out /tmp/yat-cc.png 2>/tmp/yat-cc.log
grep -q 'render warn' /tmp/yat-cc.log && { echo "FAIL: commute-combo render produced a warning"; exit 1; }

# ---- RFC-foreach-partial-results.md option D: for_each partial results,
# ---- fresh-only. Stop 1's (route 40) fixture is given; stop 2's (route
# ---- 272K) is withheld, so the harness fails that iteration once its
# ---- fixture list is exhausted (main.cpp's per-id fixture queue) — exactly
# ---- the "one flaky stop" case the RFC opens with. The array must stay
# ---- full-length (2 elements, input order): stop 1 keeps its real, fresh
# ---- ETA; stop 2's `each` (route/label) survives but its extract fields go
# ---- null with status:"failed", so the pack's own pre-existing
# ---- `item.eta1 missing` guard draws its dash with ZERO pack changes.
# ---- --state is pre-populated with an OLDER full-success snapshot and the
# ---- partial wake runs against it — the no-mixing assertion: a snapshot on
# ---- record for this source must never backfill the failed element, so
# ---- stop 2's populate-run time (13:37, unique to that fixture) must not
# ---- appear anywhere in the partial run's data at all.
CCSTATE=/tmp/yat-cc-partial-state
rm -rf "$CCSTATE"

./yat-preview "$CC" $CCDOC --now $NOW --state "$CCSTATE" --out /tmp/yat-cc-pop.png 2>/tmp/yat-cc-pop.log
grep -q '"kmb_etas"' /tmp/yat-cc-pop.log || { echo "FAIL: commute-combo partial: populate run did not extract kmb_etas"; exit 1; }

CCPARTIALDOC="--doc kmb=fixtures/commute-combo.kmb.1.json --doc ctb=fixtures/commute-combo.ctb.json --doc mtr=fixtures/commute-combo.mtr.json"
./yat-preview "$CC" $CCPARTIALDOC --now $NOW --state "$CCSTATE" --out /tmp/yat-cc-partial.png 2>/tmp/yat-cc-partial.log
grep -q 'E_ITER' /tmp/yat-cc-partial.log || { echo "FAIL: commute-combo partial: no E_ITER warning"; exit 1; }
grep -q '1/2 iteration' /tmp/yat-cc-partial.log || { echo "FAIL: commute-combo partial: E_ITER warning doesn't name 1/2 failed"; exit 1; }
grep -q '(stale-serve)' /tmp/yat-cc-partial.log && { echo "FAIL: commute-combo partial: must not take the stale-serve path"; exit 1; }
grep -q '^stale:' /tmp/yat-cc-partial.log && { echo "FAIL: commute-combo partial: must not report anyStale()"; exit 1; }
grep -q 'render warn: empty state' /tmp/yat-cc-partial.log && { echo "FAIL: commute-combo partial: page has content, must not draw the empty-state card"; exit 1; }
grep -q '"dest_tc": "麗港城"' /tmp/yat-cc-partial.log || { echo "FAIL: commute-combo partial: working stop's dest_tc missing"; exit 1; }
grep -q '"status": "ok"' /tmp/yat-cc-partial.log || { echo "FAIL: commute-combo partial: working element missing status:ok"; exit 1; }
grep -q '"status": "failed"' /tmp/yat-cc-partial.log || { echo "FAIL: commute-combo partial: failed element missing status:failed"; exit 1; }
grep -q '"eta1": null' /tmp/yat-cc-partial.log || { echo "FAIL: commute-combo partial: failed element's eta1 should be null, not backfilled"; exit 1; }
# no-mixing: the OLD snapshot's stop-2 (272K) time must not leak into this
# fresh partial array anywhere.
grep -q '13:37:00' /tmp/yat-cc-partial.log && { echo "FAIL: commute-combo partial: stale snapshot value leaked into the partial array (no-mixing violation)"; exit 1; }

./yat-preview "$CC" $CCPARTIALDOC --now $NOW --state "$CCSTATE" --out /tmp/yat-cc-partial-b.png 2>/dev/null
cmp -s /tmp/yat-cc-partial.png /tmp/yat-cc-partial-b.png || { echo "FAIL: nondeterministic render (commute-combo partial)"; exit 1; }

if cmp -s /tmp/yat-cc.png /tmp/yat-cc-partial.png; then echo "FAIL: commute-combo partial rendered identically to the full-success page"; exit 1; fi

# golden compare (regenerate with: cp /tmp/yat-cc-partial.png goldens/commute-combo-partial.png)
if [ -f goldens/commute-combo-partial.png ]; then
  cmp -s /tmp/yat-cc-partial.png goldens/commute-combo-partial.png || { echo "FAIL: golden mismatch (goldens/commute-combo-partial.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/commute-combo-partial.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi
rm -rf "$CCSTATE"

# ---- RFC-foreach-partial-results.md option A: engine-owned line on a
# ---- list's own E_BIND slot. Every KMB iteration fails here (no --doc kmb=
# ---- at all, so every fetch call for it errors identically) and no
# ---- snapshot is on record, so data.kmb_etas stays null and the `list`
# ---- bound to it hits E_BIND — exactly the "MTR and CTB succeed, KMB
# ---- collapses to a hole" case the RFC opens with. The list must now draw
# ---- one engine-owned line instead of nothing; CTB and MTR (both still fed
# ---- real fixtures) must render exactly as they do on the full-success page.
CCBINDDOC="--doc ctb=fixtures/commute-combo.ctb.json --doc mtr=fixtures/commute-combo.mtr.json"
./yat-preview "$CC" $CCBINDDOC --now $NOW --out /tmp/yat-cc-bindfail.png 2>/tmp/yat-cc-bindfail.log
grep -q "E_BIND: list bind 'data.kmb_etas'" /tmp/yat-cc-bindfail.log || { echo "FAIL: commute-combo all-KMB-failed: no E_BIND warning"; exit 1; }
grep -q 'render warn: empty state' /tmp/yat-cc-bindfail.log && { echo "FAIL: commute-combo all-KMB-failed: CTB/MTR still have data, must not draw the page-level empty card"; exit 1; }
grep -q '"dest_en": "Central (Macao Ferry)"' /tmp/yat-cc-bindfail.log || { echo "FAIL: commute-combo all-KMB-failed: Citybus section should still extract"; exit 1; }

./yat-preview "$CC" $CCBINDDOC --now $NOW --out /tmp/yat-cc-bindfail-b.png 2>/dev/null
cmp -s /tmp/yat-cc-bindfail.png /tmp/yat-cc-bindfail-b.png || { echo "FAIL: nondeterministic render (commute-combo all-KMB-failed)"; exit 1; }

if cmp -s /tmp/yat-cc.png /tmp/yat-cc-bindfail.png; then echo "FAIL: all-KMB-failed rendered identically to the full-success page"; exit 1; fi
if cmp -s /tmp/yat-cc-partial.png /tmp/yat-cc-bindfail.png; then echo "FAIL: all-KMB-failed (A) rendered identically to the partial page (D)"; exit 1; fi

# golden compare (regenerate with: cp /tmp/yat-cc-bindfail.png goldens/commute-combo-bindfail.png)
if [ -f goldens/commute-combo-bindfail.png ]; then
  cmp -s /tmp/yat-cc-bindfail.png goldens/commute-combo-bindfail.png || { echo "FAIL: golden mismatch (goldens/commute-combo-bindfail.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/commute-combo-bindfail.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# ---- device capability profiles (ROADMAP): Engine::render()'s optional
# ---- `profile` argument (canvas.h's DeviceProfile) and this CLI's --profile
# ---- flag. Every render above omits the flag and must stay byte-identical to
# ---- its pre-existing golden (already proven — none of those goldens needed
# ---- regenerating for this change); this section covers the flag itself.
# ---- e1001 is the mono reTerminal at the same 800x480 (PRD.md: colour and
# ---- refresh speed are what differ, not size) — every chromatic ink, whether
# ---- a pack wrote a role ("danger") or a literal ("red"), collapses to black,
# ---- and the `image` dither palette narrows to black/white.

# a) an unknown profile name fails cleanly rather than silently defaulting.
if ./yat-preview "$P" --now $NOW --profile bogus --out /tmp/yat-profile-bad.png 2>/tmp/yat-profile-bad.log; then
  echo "FAIL: unknown --profile should be rejected"; exit 1
fi
grep -q "unknown --profile" /tmp/yat-profile-bad.log || { echo "FAIL: unclear error for unknown --profile"; exit 1; }

# b) explicit --profile e1002 must render byte-identical to omitting the flag
# entirely — proving the *default* really is e1002, not just documented as one.
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --profile e1002 --out /tmp/yat-profile-e1002.png 2>/dev/null
cmp -s /tmp/yat-t1.png /tmp/yat-profile-e1002.png || { echo "FAIL: --profile e1002 differs from the no-flag default"; exit 1; }

# c) hko-now, warnings active: the active-warnings strip draws its icons/text
# in raw ink literals (red/yellow/blue — §9.1a's "ink stays valid forever"
# case, official warning colours), which have no role to swap and so exercise
# the literal-ink half of the profile clamp, not just inkForRole.
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WACTIVE --now $NOW --profile e1001 --out /tmp/yat-e1001-hko.png 2>/tmp/yat-e1001-hko.log
grep -q 'render warn' /tmp/yat-e1001-hko.log && { echo "FAIL: hko-now (e1001) render produced a warning"; exit 1; }
if cmp -s /tmp/yat-w1.png /tmp/yat-e1001-hko.png; then echo "FAIL: --profile e1001 did not change the hko-now render"; exit 1; fi

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WACTIVE --now $NOW --profile e1001 --out /tmp/yat-e1001-hko-b.png 2>/dev/null
cmp -s /tmp/yat-e1001-hko.png /tmp/yat-e1001-hko-b.png || { echo "FAIL: nondeterministic render (hko-now, e1001)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-e1001-hko.png goldens/hko-now-e1001.png)
if [ -f goldens/hko-now-e1001.png ]; then
  cmp -s /tmp/yat-e1001-hko.png goldens/hko-now-e1001.png || { echo "FAIL: golden mismatch (goldens/hko-now-e1001.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/hko-now-e1001.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# d) sushiro-queue: the good/warn/danger wait-time bands are role-spelled
# (§9.1a), the other half of the clamp — same test, opposite ink path.
./yat-preview "$SQ" --doc stores=fixtures/sushiro-queue.stores.json --now $NOW --profile e1001 --out /tmp/yat-e1001-sq.png 2>/tmp/yat-e1001-sq.log
grep -q 'render warn' /tmp/yat-e1001-sq.log && { echo "FAIL: sushiro-queue (e1001) render produced a warning"; exit 1; }
if cmp -s /tmp/yat-sq.png /tmp/yat-e1001-sq.png; then echo "FAIL: --profile e1001 did not change the sushiro-queue render"; exit 1; fi

./yat-preview "$SQ" --doc stores=fixtures/sushiro-queue.stores.json --now $NOW --profile e1001 --out /tmp/yat-e1001-sq-b.png 2>/dev/null
cmp -s /tmp/yat-e1001-sq.png /tmp/yat-e1001-sq-b.png || { echo "FAIL: nondeterministic render (sushiro-queue, e1001)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-e1001-sq.png goldens/sushiro-queue-e1001.png)
if [ -f goldens/sushiro-queue-e1001.png ]; then
  cmp -s /tmp/yat-e1001-sq.png goldens/sushiro-queue-e1001.png || { echo "FAIL: golden mismatch (goldens/sushiro-queue-e1001.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/sushiro-queue-e1001.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# e) photo-frame with a real fetched-and-decoded image: the dither palette
# itself must narrow to black/white (paletteForProfile, engine/src/
# render.cpp), not just the widget-coloured chrome/text the two cases above
# already cover.
./yat-preview "$PF" --params "$PFPARAMS" --doc image=fixtures/photo-frame.image.png --now $NOW --profile e1001 --out /tmp/yat-e1001-pf.png 2>/tmp/yat-e1001-pf.log
grep -q 'render warn' /tmp/yat-e1001-pf.log && { echo "FAIL: photo-frame (e1001) render produced a warning"; exit 1; }
if cmp -s /tmp/yat-pf-image.png /tmp/yat-e1001-pf.png; then echo "FAIL: --profile e1001 did not change the photo-frame render"; exit 1; fi

./yat-preview "$PF" --params "$PFPARAMS" --doc image=fixtures/photo-frame.image.png --now $NOW --profile e1001 --out /tmp/yat-e1001-pf-b.png 2>/dev/null
cmp -s /tmp/yat-e1001-pf.png /tmp/yat-e1001-pf-b.png || { echo "FAIL: nondeterministic render (photo-frame, e1001)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-e1001-pf.png goldens/photo-frame-e1001.png)
if [ -f goldens/photo-frame-e1001.png ]; then
  cmp -s /tmp/yat-e1001-pf.png goldens/photo-frame-e1001.png || { echo "FAIL: golden mismatch (goldens/photo-frame-e1001.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/photo-frame-e1001.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# f) the mono promise, checked in pixels rather than trusted from a comment:
# every one of the three e1001 renders above (chrome/text/icon ink, a
# role-spelled bar/text ink, and a dithered photograph) must contain ONLY the
# panel's black and white RGB values (kPalette in engine/src/render.cpp) — no
# red/yellow/green/blue survives the clamp, and no grey either (the engine has
# none to draw). Same manual PNG-decode idiom as the clear-page content-pixel
# check above.
python3 - <<'EOF' || exit 1
import struct, sys, zlib
def load(p):
    d = open(p, 'rb').read(); pos = 8; idat = b''; w = h = 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ, dat = d[pos+4:pos+8], d[pos+8:pos+8+ln]
        if typ == b'IHDR': w, h = struct.unpack('>II', dat[:8])
        if typ == b'IDAT': idat += dat
        pos += 12 + ln
    raw = zlib.decompress(idat); stride = w * 3; rows = []; prev = bytearray(stride); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+stride]); i += stride
        for x in range(stride):
            a = line[x-3] if x >= 3 else 0
            b = prev[x]
            c = prev[x-3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p_ = a + b - c
                pa, pb, pc = abs(p_-a), abs(p_-b), abs(p_-c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, rows

allowed = {b'\x10\x10\x10', b'\xff\xff\xff'}  # kPalette's Black, White
for name in ["/tmp/yat-e1001-hko.png", "/tmp/yat-e1001-sq.png", "/tmp/yat-e1001-pf.png"]:
    w, h, rows = load(name)
    colors = set()
    for row in rows:
        for x in range(w):
            colors.add(row[x*3:x*3+3])
    bad = colors - allowed
    if bad:
        print(f"FAIL: {name} has non-mono pixel colour(s): {[c.hex() for c in bad]}")
        sys.exit(1)
EOF

# ---- §11.4a device notices: the three tiers standard chrome can surface, and
# ---- the rule that separates them — Info and Degraded are things the ENGINE
# ---- works out (this render's own data warnings; a snapshot gone old), Action
# ---- is the only one a host can name, because only the device knows a
# ---- provider rejected its key. Omitting --notice (every test above) draws
# ---- nothing at all, which is what those goldens already prove.
#
# a) Action — the top banner. Takes its 34px off the content band rather than
#    covering it, so the render must change; and the copy is per-code, so two
#    codes must not render the same.
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --notice voice --out /tmp/yat-nv.png 2>/tmp/yat-nv.log
grep -q 'render warn' /tmp/yat-nv.log && { echo "FAIL: notice (voice) render produced a warning"; exit 1; }
grep -q '^notice: action (voice)' /tmp/yat-nv.log || { echo "FAIL: notice: no greppable action line"; exit 1; }
if cmp -s /tmp/yat-t1.png /tmp/yat-nv.png; then echo "FAIL: --notice voice did not change the render"; exit 1; fi

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --notice storage --out /tmp/yat-ns.png 2>/dev/null
if cmp -s /tmp/yat-nv.png /tmp/yat-ns.png; then echo "FAIL: the voice and storage banners say the same thing"; exit 1; fi
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --notice config --out /tmp/yat-nc.png 2>/dev/null
if cmp -s /tmp/yat-nv.png /tmp/yat-nc.png; then echo "FAIL: the voice and config banners say the same thing"; exit 1; fi

./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --notice voice --out /tmp/yat-nv-b.png 2>/dev/null
cmp -s /tmp/yat-nv.png /tmp/yat-nv-b.png || { echo "FAIL: nondeterministic render (action banner)"; exit 1; }

# An unknown condition name is a usage error, not a silently ignored flag.
if ./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --notice nonsense --out /tmp/yat-nx.png 2>/tmp/yat-nx.log; then
  echo "FAIL: an unknown --notice name should be rejected"; exit 1
fi
grep -q "unknown --notice 'nonsense'" /tmp/yat-nx.log || { echo "FAIL: unclear error for an unknown --notice name"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-nv.png goldens/notice-action.png)
if [ -f goldens/notice-action.png ]; then
  cmp -s /tmp/yat-nv.png goldens/notice-action.png || { echo "FAIL: golden mismatch (goldens/notice-action.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/notice-action.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# a2) THE §9.1a-a REGRESSION TEST: the banner remains legible with field=White.
#     Twice now a `warn` surface shipped that the preview showed and the panel
#     did not, because the yellow was doing work black was not — first as a
#     mark, then as a field. `--profile e1001` renders the field=White case
#     exactly (mono has no field to give), so the two profiles' banners are
#     rendered and compared as STRUCTURE rather than as bytes:
#       * the black pixels of the banner band must be pixel-identical between
#         the two — anything the colour panel draws that the mono one doesn't
#         is, by definition, something the field was carrying;
#       * and that black must actually amount to a banner: 3px rules across the
#         full width top and bottom, hazard bars at both ends around half ink,
#         and a solid triangle at least 18px across at its base.
#     A future edit that puts the signal back into the yellow fails here rather
#     than on someone's wall.
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --notice voice --profile e1001 --out /tmp/yat-nv-mono.png 2>/tmp/yat-nv-mono.log
grep -q 'render warn' /tmp/yat-nv-mono.log && { echo "FAIL: mono action-banner render produced a warning"; exit 1; }
python3 - <<'EOF' || exit 1
import struct, sys, zlib
def rows(path):
    d = open(path, 'rb').read(); pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        for x in range(w*3):
            a = line[x-3] if x >= 3 else 0; b = prev[x]; c = prev[x-3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b)//2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
BLACK = b'\x10\x10\x10'
YELLOW = b'\xfa\xe8\x98'
BANNER_H, RULE_H, HAZ_W = 34, 3, 28   # kActionBannerH / kActionRuleH / kHazardBlockW
def px(row, x): return row[x*3:x*3+3]
def analyse(path):
    w, h, r = rows(path)
    # The banner's own rules are the only full-width black runs 3+ rows deep in
    # the top half of the page; the header's 1px rule abuts the upper one, so
    # anchor on the LOWER group and step back the banner's known height.
    full = [y for y in range(h) if all(px(r[y], x) == BLACK for x in range(w))]
    groups = []
    for y in full:
        if groups and y == groups[-1][-1] + 1: groups[-1].append(y)
        else: groups.append([y])
    thick = [g for g in groups if len(g) >= RULE_H]
    if len(thick) < 2:
        print(f"FAIL: {path} has {len(thick)} full-width rule(s) {RULE_H}px+ deep — the action "
              f"banner is supposed to be ruled top AND bottom"); sys.exit(1)
    top = thick[-1][-1] - (BANNER_H - 1)
    inner = range(top + RULE_H, top + BANNER_H - RULE_H)
    haz = []
    for x0 in (0, w - HAZ_W):
        n = sum(1 for y in inner for x in range(x0, x0 + HAZ_W) if px(r[y], x) == BLACK)
        haz.append(n / float(HAZ_W * len(inner)))
    widest = 0
    for y in inner:
        run = 0
        for x in range(HAZ_W, HAZ_W + 40):
            run = run + 1 if px(r[y], x) == BLACK else 0
            widest = max(widest, run)
    black = {(x, y) for y in range(top, top + BANNER_H) for x in range(w)
             if px(r[y], x) == BLACK}
    yellow = sum(1 for y in range(top, top + BANNER_H) for x in range(w)
                 if px(r[y], x) == YELLOW)
    return top, haz, widest, black, yellow
ctop, chaz, cwide, cblack, cyellow = analyse('/tmp/yat-nv.png')
mtop, mhaz, mwide, mblack, myellow = analyse('/tmp/yat-nv-mono.png')
if cblack != mblack:
    print(f"FAIL: the action banner's black structure differs between the colour and mono "
          f"panels ({len(cblack ^ mblack)} px) — something in it is being carried by the "
          f"yellow field, which the hardware cannot show (§9.1a-a)"); sys.exit(1)
if myellow != 0:
    print(f"FAIL: the mono banner drew {myellow} yellow px — the clamp is broken"); sys.exit(1)
if cyellow == 0:
    print("FAIL: the colour banner drew no yellow field at all — the decoration is gone, not "
          "just demoted"); sys.exit(1)
for name, haz in (("colour", chaz), ("mono", mhaz)):
    for i, frac in enumerate(haz):
        if not (0.30 <= frac <= 0.70):
            print(f"FAIL: {name} banner's {'left' if i == 0 else 'right'} hazard bars are "
                  f"{frac:.0%} ink — expected roughly half; they have stopped reading as "
                  f"stripes"); sys.exit(1)
if cwide < 18:
    print(f"FAIL: the banner's warning triangle is only {cwide}px across at its widest — a "
          f"solid glyph is the one part of this banner that says 'warning' without words")
    sys.exit(1)
EOF

# golden compare (regenerate with: cp /tmp/yat-nv-mono.png goldens/notice-action-e1001.png)
if [ -f goldens/notice-action-e1001.png ]; then
  cmp -s /tmp/yat-nv-mono.png goldens/notice-action-e1001.png || { echo "FAIL: golden mismatch (goldens/notice-action-e1001.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/notice-action-e1001.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# b) Action draws under `"chrome": "none"` too. A household whose only page is
#    a photo frame is exactly the one that would otherwise never find out its
#    voice control stopped working — so the banner is the one piece of chrome a
#    pack cannot decline. (The icon sheet is the in-tree chrome:none pack.)
./yat-preview "$ICO" --now $NOW --notice voice --out /tmp/yat-nv-nochrome.png 2>/dev/null
if cmp -s /tmp/yat-icons.png /tmp/yat-nv-nochrome.png; then
  echo "FAIL: the action banner did not draw under chrome:none"; exit 1
fi

# c) Degraded — engine-derived, no flag: a source stale-serving for 3h+. The
#    threshold is the assertion. Same state dir, same failing fetch, two
#    different `now`s: at +2h the footer badge carries it alone, at +4h the
#    strip appears and says what it means in words.
DEGDIR=/tmp/yat-state-degraded
rm -rf "$DEGDIR"
./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --state "$DEGDIR" --out /tmp/yat-deg0.png 2>/dev/null
./yat-preview "$P" --doc current=fixtures/does-not-exist.json $WCLEAR --now $((NOW + 2 * 3600)) --state "$DEGDIR" --out /tmp/yat-deg2h.png 2>/tmp/yat-deg2h.log
grep -q '^stale:' /tmp/yat-deg2h.log || { echo "FAIL: degraded: the 2h case should still be stale-serving"; exit 1; }
grep -q '^notice: degraded' /tmp/yat-deg2h.log && { echo "FAIL: degraded strip fired at 2h — the threshold is 3h"; exit 1; }

./yat-preview "$P" --doc current=fixtures/does-not-exist.json $WCLEAR --now $((NOW + 4 * 3600)) --state "$DEGDIR" --out /tmp/yat-deg4h.png 2>/tmp/yat-deg4h.log
grep -q '^notice: degraded' /tmp/yat-deg4h.log || { echo "FAIL: degraded strip did not fire at 4h stale"; exit 1; }

./yat-preview "$P" --doc current=fixtures/does-not-exist.json $WCLEAR --now $((NOW + 4 * 3600)) --state "$DEGDIR" --out /tmp/yat-deg4h-b.png 2>/dev/null
cmp -s /tmp/yat-deg4h.png /tmp/yat-deg4h-b.png || { echo "FAIL: nondeterministic render (degraded strip)"; exit 1; }

# golden compare (regenerate with: cp /tmp/yat-deg4h.png goldens/notice-degraded.png)
if [ -f goldens/notice-degraded.png ]; then
  cmp -s /tmp/yat-deg4h.png goldens/notice-degraded.png || { echo "FAIL: golden mismatch (goldens/notice-degraded.png)"; exit 1; }
elif [ -z "$YAT_GOLDEN_BOOTSTRAP" ]; then
  echo "FAIL: golden missing (goldens/notice-degraded.png) — set YAT_GOLDEN_BOOTSTRAP=1 to (re)generate"; exit 1;
fi

# d) Neither tier may be hash-skipped into invisibility (§11.2): a device that
#    decided the panel is already right would never draw the banner it just
#    earned. Both edges have to move the hash, and a clean page's hash must not
#    have moved at all — every golden above still compares byte-for-byte, and
#    this pins the number itself.
HCLEAN=$(grep '^hash:' /tmp/yat-t1.log)
HNOTICE=$(./yat-preview "$P" --doc current=fixtures/hko-now.current.json $WCLEAR --now $NOW --notice voice --out /dev/null 2>&1 | grep '^hash:')
[ "$HCLEAN" != "$HNOTICE" ] || { echo "FAIL: an action notice did not change the hash — it could be skipped"; exit 1; }
H2H=$(grep '^hash:' /tmp/yat-deg2h.log)
H4H=$(grep '^hash:' /tmp/yat-deg4h.log)
[ "$H2H" != "$H4H" ] || { echo "FAIL: crossing the degraded threshold did not change the hash"; exit 1; }
rm -rf "$DEGDIR"

# e) The Info tier is the §11.4 data-warning glyph, and it is drawn from what
#    the render actually produced rather than from a flag: render-test (an
#    E_BIND every run) and commute-combo's partial/bindfail cases carry it in
#    their goldens above, and a clean page must not.
#
#    Found by its BLACK shape, not by its yellow. Until §9.1a-a grew to cover
#    fields this test looked for yellow pixels in the footer band, which meant
#    the only evidence it ever demanded was the one thing the panel cannot show
#    — it would have passed just as happily on the invisible glyph the owner
#    reported. The triangle is solid now, so it has a signature no run of type
#    produces: a 15px black base, 13px one row above it, 11px three rows above,
#    all sharing a centre. The same search runs over the mono render, where
#    there is no yellow in the file at all.
./yat-preview "$R" --now $NOW --profile e1001 --out /tmp/yat-r1-e1001.png 2>/dev/null
python3 - <<'EOF' || exit 1
import struct, sys, zlib
def rows(path):
    d = open(path, 'rb').read(); pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        if typ == b'IHDR': w, h = struct.unpack('>II', d[pos+8:pos+16])
        elif typ == b'IDAT': idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat); out = []; prev = bytearray(w*3); i = 0
    for _ in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i+w*3]); i += w*3
        for x in range(w*3):
            a = line[x-3] if x >= 3 else 0; b = prev[x]; c = prev[x-3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b)//2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(line)); prev = line
    return w, h, out
BLACK = b'\x10\x10\x10'
def black_runs(row, w):
    out, s = [], None
    for x in range(w + 1):
        on = x < w and row[x*3:x*3+3] == BLACK
        if on and s is None: s = x
        elif not on and s is not None: out.append((s, x - 1)); s = None
    return out
def warn_glyphs(path):
    """footer-band occurrences of the solid 15x12 triangle (drawWarnTriangle)"""
    w, h, px = rows(path)
    found = []
    for y in range(h - 32, h):
        for a, b in black_runs(px[y], w):
            if b - a + 1 != 15: continue
            cx = (a + b) // 2
            up1 = [r for r in black_runs(px[y-1], w) if r[1]-r[0]+1 == 13 and (r[0]+r[1])//2 == cx]
            up3 = [r for r in black_runs(px[y-3], w) if r[1]-r[0]+1 == 11 and (r[0]+r[1])//2 == cx]
            if up1 and up3: found.append((a, y))
    return found
warned = warn_glyphs('/tmp/yat-r1.png')         # render-test: E_BIND every run
mono = warn_glyphs('/tmp/yat-r1-e1001.png')     # the same page with no yellow to find
clean = warn_glyphs('/tmp/yat-t1.png')          # hko-now, nothing wrong
if not warned:
    print("FAIL: a render carrying E_BIND drew no §11.4 data-warning glyph"); sys.exit(1)
if not mono:
    print("FAIL: the data-warning glyph vanished on a mono panel — it is being carried by the "
          "yellow field rather than by its own black shape (§9.1a-a)"); sys.exit(1)
if clean:
    print(f"FAIL: a clean render drew a data-warning glyph at {clean} — misses are silent (§11.4)")
    sys.exit(1)
EOF

echo "ALL TESTS PASS"
