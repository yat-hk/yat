# YAT Pack Spec — Use-Case Validation

**Status:** working document (pre-v0.1 workstream) — **two adversarial forge rounds complete** (round 1: 34 gaps → spec 0.2-draft; round 2: 10 real packs, 52 gap reports → spec 0.3-draft)
**Purpose:** the spec is the product's toughest bet — packs are declarative JSON executed by the firmware engine, so anything the spec can't express is something the platform can't do without an upstream firmware change. Before freezing Spec v1, we simulate real use cases against the draft spec on paper, log gaps, and decide: *extend the spec*, *defer to v1.x/v2*, or *declare out of scope*.

**Method:** for each use case, write the actual pack JSON against the draft spec ([PACK-SPEC.md](PACK-SPEC.md), currently 0.3-draft). A use case **passes** only if the full pack — data extraction *and* rendering — can be written with no hand-waving. Every gap gets a proposed spec addition with a cost estimate. Exit criterion: ≥80% of the matrix passes with Spec v1 grammar; the rest have a named path. Rounds 1–2 upgraded "on paper" to "as real files": the ten round-2 packs live in `yat-packs`' `official/` and validate against `schema/yat-pack.schema.json`.

## Baseline

The capability baseline is **PACK-SPEC.md 0.3-draft** and its schema — this file no longer keeps a parallel capability enumeration (parallel lists drifted once; per the ARCHITECTURE §2 ruling they are not maintained).

## Validation matrix

| # | Use case | Needs | Verdict | Notes / disposition |
|---|---|---|---|---|
| 1 | HKO weather + forecast (v1 pack) | JSON GET, filter by district, icons | ✅ pass | Baseline driver of the grammar; the PACK-SPEC §13 complete example |
| 2 | KMB/Citybus/MTR ETA (v1 pack) | JSON, list repeater, commute windows, free station choice | ✅ pass (0.3) | Exercised round 2 as `commute-combo`. Parameterized quoted-member steps (§6.1) freed the MTR station choice (no more 4-station preset); `exists`/`missing` gives the "—" fallback rows. Sanctioned presentation is **grouped by operator**; a *merged* single-timeline board stays inexpressible — typed-compute merge/sort queued v1.x with key normalization (KMB ISO timestamps vs MTR ttnt minutes) as the RFC gate |
| 3 | RSS headlines (v1 pack) | RSS parser, list | ✅ pass | Exercised round 2 as `headlines-simple` (status: complete). `first(n)` now shares the `max_rows` param (§6.1 count-arg) |
| 4 | Any-image page (v1 pack) | image widget from URL | ✅ pass | Not re-exercised in rounds 1–2; grammar unchanged |
| 5 | Bus + weather combo page | multiple sources, one layout | ✅ pass | Subsumed by `commute-combo` (#2) — `sources[]` is per-pack, not per-API |
| 6 | AQHI air quality | JSON GET, banded colors, bilingual advice | ✅ pass | Exercised round 2 as `aqhi` (status: complete). Comparators + `any`/`all` (0.2) removed the worst guard stacking; `strings` cap now 200 chars for advice lines; residual band duplication waits on `match` (v1.x, top of queue) |
| 7 | Tide tables / sunrise (HKO) | string-typed numeric feed, next-tide marker | ✅ pass (0.3) | Exercised round 2 as `tides`. Both round-2 blockers accepted: ordering comparators coerce fully-numeric string context values (§6.5), and `{{…}}` spans lex atomically so `'{{now\|date_fmt('HHmm')}}'` is legal in `when` literals (§6.1). Hash-skip hashes the boolean outcome of `now`-bearing guards; `enum_title` ends the duplicated inline station table |
| 8 | Stock/crypto ticker (API key) | JSON + secret header, fan-out, up/down styling | ✅ pass (0.3) | Exercised round 2 as `stock-ticker`. Secret `sent_to` model exercised; all six round-2 papercuts landed: `abs` (kills `sign_char`+`round` double-signing), `color` param form, `{{data.*}}` steps in render strings, `index` as `when` root, `E_BIND`-renders-nothing vs `empty_text` split, space-free pipelines |
| 9 | Countdown ("DSE in 42 days", holidays) | current time in context + date math | ✅ pass (landed v1) | `{{now}}` + `date_diff_days`/`weekday`/`date_fmt` (0.1→0.2) plus `compute` for lifecycle `when`s and `abs` for "was 3 days ago" (0.3). Exercised round 2 as `countdown` (status: complete); §7.4 documents the 0-is-falsy and no-quantization-in-compute footguns |
| 10 | Static content: family notice, name card + QR, quote of the day | no network at all | ✅ pass (landed v1) | `source.type: "inline"` + `pick_by_day` (0.1→0.2); `count`, array-param `depends_on`, item-property defaults §3.3(5) (0.3). Exercised round 2 as `family-board` (status: complete). Item-conditional row styling waits on `match` — pre-committed to be legal inside list rows when it lands (Appendix A) |
| 11 | Home Assistant / sensor dashboard | MQTT | ⚠️ gap → v1.x | Persistent subscribe is impossible under deep sleep, but **retained messages** fit polling: connect on wake, read retained topics, disconnect. `source.type: "mqtt"` queued v1.x (MQTT client in firmware; LAN consent at install per §12.3) |
| 12 | Personal calendar (public/secret iCal URL) | ICS parsing, date math | ⚠️ gap → v1.x | `format: "ics"` parser (events → uniform list); date filters already landed via #9. Covers Google Calendar secret-URL mode without OAuth |
| 13 | Sparkline / 24h temperature or AQHI trend | tiny chart widget bound to numeric array | ✅ pass (0.3) | The 8-bar-row unroll shipped as the livable fallback through 0.2 and then overflowed the footer on-device, which is what forced the issue. `chart` **line mode landed at 0.3** (PACK-SPEC §9.12a): bound to a numeric array, auto-scaled or pinned, gaps for non-numeric elements. `temp-trend` 2.0.0 is a day curve and no longer unrolls anything; golden at `tools/preview/goldens/temp-trend.png`. Bar mode and the `zip()` pairing need stay v1.x |
| 14 | Two-step fetch (lookup ID, then query by ID) | source B's URL references source A's output | ✅ pass (0.3) | **Promoted to v1** at 0.3-draft on the GMB flagship case: `{{sources.<id>.<field>}}` in later sources' path/query/headers/body/filter literals, host literal, no forward/self refs (PACK-SPEC §5.7). Exercised as `gmb-minibus` (#19) |
| 15 | Derived values (fare total, % change, unit conversion) | arithmetic on extracted fields | ⚠️ deliberately narrow | `compute` (0.2) plus the total functions `abs` and `count` (0.3) cover every case the forge rounds actually reported. A general `expr` filter (`+ - * /`) remains **rejected** — a slippery slope toward a language; pre-compute upstream or live without |
| 16 | Google Calendar / Strava / Octopus (OAuth) | OAuth flows, token refresh | ❌ v2+ | Token refresh on a sleeping device is a real subsystem. v1 answer: secret-URL modes (#12) or an `image` page from a self-hosted generator |
| 17 | Push-triggered page (doorbell, parcel) | inbound push to a sleeping device | ❌ out of scope v1 | No listener exists while asleep. Honest answers: tighter poll cadence, or v2 MQTT-retained + shorter wakes; true push contradicts the power model — say so in docs |
| 18 | Web page screenshot (old `snapshot` idea) | a browser somewhere | ❌ out of scope | By design: no server. `image` pack (#4) + bring-your-own generator is the escape hatch |
| 19 | Minibus (GMB) ETA | data.gov.hk JSON, two-step ID resolution | ✅ pass (0.3) | Exercised round 2 as `gmb-minibus`. All four round-2 gaps landed: sequential refs §5.7 (in-pack route-ID resolution — no more agent-supplied numeric ID), `index` steps inside a projection (route→directions[0]→dest), `min_refresh_min` (static lookups stop refetching at ETA cadence), fractional-second timestamps in §8.3. Good first-PR tutorial subject |
| 20 | Horse racing / sports fixtures | JSON/RSS variability | ✅ pass (data-source dependent) | Still unexercised — validates grammar against messier feeds during v0.3 |
| 21 | Multi-section news board (pick N feeds, headlines per section) | fan-out over an enum array, per-section lists | ✅ pass (0.3) | Exercised round 2 as `news-sections` — round 1's fan-out flagship, finished by round 2: both blockers accepted (source-object cap raised to 8; nested `list` over `for_each` collect fields, §9.8a) plus `enum_title` for section headers and multi-select array-of-enum gallery semantics (§3.2) || 22 | Sushiro branch queue widget (restaurant wait times) | bare-array JSON root, per-row wait bands, optional per-branch queue | ✅ pass (workaround, 0.3) | Exercised post-freeze-candidate as `sushiro-queue` (real pack, live `sushipass.sushiro.com.hk` API): nearest-N branch list with color wait bands (ordering comparators), optional now-calling via source-`when` + `enum_title`, lunch/dinner windows. One gap found: **bare-array JSON document root is unaddressable** (`first-step = member|index|filter` has no root token; storelist returns a bare array) — legal workaround is a first-step filter (`[?storeStatus=='OPEN']`), which this pack uses. Proposed fix (grammar-free): normalize bare-array/scalar HTTPS JSON documents to `{"root": <value>}` at parse, like the rss/csv canonical shapes — then `root` addresses the whole document. Queued below |
| 23 | Exact-time scheduling ("render at 07:00 sharp") | anchored triggers, guaranteed window-open wake | ⚠️ gap → v1.x | Found by external Codex audit (hacker-news-highlights): §10 has only unanchored cadence + cadence-changing windows; no exact-time trigger, no guaranteed wake at window open. Honest v1 approximation: short window with tight cadence. **Queue:** `schedule.at: ["07:00"]` anchored trigger — needs firmware scheduler support first |

**Current score (post round 2, spec 0.3-draft): 14 of 21 rows pass clean; +1 sanctioned-workaround with a real pack (#13); 2 queued v1.x (#11 mqtt, #12 ics); 1 deliberately narrowed (#15); 3 deliberately out (#16, #17, #18). In-scope pass rate: 14/17 = 82% strict, 15/17 = 88% counting #13's sanctioned pack — the ≥80% exit bar is met, and 10 of the passing rows exist as real schema-valid packs in `yat-packs`' `official/`.**

## Spec additions accepted into the v1 grammar from this exercise

From the original matrix pass (0.1):

1. `{{now}}` context + date filters (`date_diff_days`, `weekday`, `date_fmt`) — #9, feeds #12
2. `source.type: "inline"` + `pick_by_day` — #10

From forge round 1 (0.2-draft):

3. Source-level `when` (conditional fetch, params-rooted) and `for_each`+`collect` fan-out with `{{each}}`/`{{each.prop}}` (worst-case total fetches ≤8)
4. `when` comparators `< <= > >= has`; object form `{"any":[…]}`/`{"all":[…]}`; filtered `{{now|…}}` in widget-`when` literals
5. Extraction pipe stages `first(n)`, `last(n)`, `round(n)` (quantize-before-hash-skip)
6. Top-level `compute` (derived fields, numeric result typing) and `strings` + `render.lang_param` (bilingual labels)
7. Params: `enum_titles`, `format:"date"`, `depends_on`, optional-param semantics, self-validating defaults/enums; array params behind array→scalar filters; `list.max_rows` and `bar.min`/`max` param forms
8. `{{index}}`, `{{item[n]}}`, `sign_char`, bare-`HHMM` `time_hhmm`, offset-less-ISO = device-local, §9.3 cross-axis width table, space-free placeholders

From forge round 2 (0.3-draft):

9. **Sequential source references** `{{sources.<id>.<field>}}` (§5.7 — promoted from v1.x, #14/#19) and per-source `min_refresh_min`
10. Source-object cap 8 (fetch budget unchanged); source-`when` literals normatively `now`-free
11. Parameterized quoted-member steps (`{{params.*}}`/`{{each.*}}` substituted before parse); `index` steps inside projections; `first(`/`last(` count-arg param form; space-free canonical document expressions
12. `exists`/`missing` unary tests; ordering-comparator numeric coercion of string context values; `{{data.*}}` in widget-`when` literals (data-vs-data); `index` as a list-row `when` root; atomic `{{…}}` lexing; boolean-outcome hashing of `now`-bearing guards
13. Filters `abs`, `count`, `enum_title('<param>')` (13 total); `weekday('auto')` via `lang_param`; closed accepted-input list for `time_hhmm`/`date_fmt` (space separator, optional/fractional seconds)
14. Params: `implies` (gallery-only cross-param lock), `uniqueItems`, array-param `depends_on`, item-property default semantics §3.3(5), array-of-enum multi-select rendering
15. Render: nested `list` for collect fields (§9.8a, outer×inner ≤40), `color` param form, install-validated templated `bar` bounds, `E_BIND`-renders-nothing vs `empty_text` split, `strings` values ≤200, warning glyph restricted to `E_TYPE`/`E_BIND`

## Queued for v1.x (spec-minor, firmware OTA)

- `match`/default container — **top of the queue**; deferral re-affirmed twice on livability grounds; pre-committed to be legal inside list row templates with item-rooted arms (Appendix A)
- `chart` widget (sparkline/bars) — #13; subsumes the bar-row unroll and the `zip()` pairing need
- `source.type: "mqtt"` (retained-read on wake) — #11
- `format: "ics"` — #12
- Typed-compute merge/sort (`{"concat":[…],"sort_by":…,"limit":n}`) — the merged cross-source arrival board (#2); RFC gate: key normalization across unit systems (ISO timestamps vs minutes-to-arrival)
- `map('<inline_field>')` lookup filter — data-side code→name tables (`enum_title` already covers the param-table subset)
- `zip()` + `pluck()` — parallel-array extraction, one RFC
- Bilingual param `title`/`description`
- `first_after_now()` selection
- `llm` transform step (AI daily brief) — already on the roadmap
- **Numeric-string coercion in render filters** — found live 2026-07-30 (`hsi` pack, both CNBC and Twelve Data feeds). `round`/`abs`/`sign_char` require true JSON numeric types and do NOT coerce numeric-looking strings, while `when` ordering comparators (§6.5) deliberately DO. Real finance/data APIs commonly stringify numeric fields for precision → the `sign_char`+`abs` change-prefix idiom is unusable against them. Fix options: a `num` filter (string→number), or extend those three filters to coerce like the comparators do (asymmetry is the surprising part). Compounds with the known no-arithmetic gap (#15) — the `hsi` pack ships without a computed change value as a result.
- **Bare `YYYYMMDD` date input** — found live 2026-07-30 (`hko-9day` pack). HKO's `fnd` feed returns `forecastDate: "20260731"` (no separators); none of `weekday`/`date_fmt`/`date_diff_days` accept it (§8.3–8.6 require `YYYY-MM-DD` or full ISO). A pack cannot reformat or weekday-derive HKO's own forecast date. Minimal fix: a bare-`YYYYMMDD` branch mirroring `time_hhmm`'s existing bare-`HHMM` special case. (Routed around in the pack via HKO's pre-localized `week` field.)
- Bare-array/scalar JSON root normalization to `{"root": <value>}` — #22 (sushiro-queue); parse-level canonical shape, zero grammar change; until then the sanctioned pattern is a first-step filter

## Explicitly rejected / out of scope (do not re-propose without new evidence)

- General arithmetic `expr` filters (#15) — `compute` + `abs`/`count` are the v1 answer
- General substring/slice filters; general parameterized filter arguments (`weekday('auto')` is the single pack-state-resolving token)
- `oneOf`/`const`+`title` enum encoding (use `enum_titles`); cross-midnight schedule windows (split into two); `list` `error_text` (`E_BIND` renders nothing by design)
- OAuth flows (#16, v2+); push-to-sleeping-device (#17); webpage screenshots (#18)

## Process notes

- This matrix is a living file; every proposed community pack that *can't* be expressed gets a row before it gets a workaround.
- The forge protocol worked twice and is the template for round 3 (post-engine): adversarial authors write real packs, report gaps in this matrix's vocabulary, a synthesis editor rules accept/defer/reject, and the spec + schema move in lockstep.
- The ten round-2 packs in `yat-packs`' `official/` all validate under the documented ajv command (zero strict-mode warnings). Known debt: `aqhi`, `commute-combo`, `gmb-minibus` still carry **spaced pipelines** (`… | [0]`) that predate the 0.3 space-free canonical rule — ajv cannot police spacing; the full validator will flag them. Several packs also predate constructs accepted *because of them* (`gmb-minibus` still takes `route_id` as a param instead of §5.7; `tides` ships the inline station table `enum_title` obsoletes; `news-sections` uses header stacks instead of §9.8a + `enum_title`; `commute-combo` hardcodes the 4-station preset). A mechanical modernization pass is required before these become golden-render fixtures.
- The developer skill runs this exercise automatically: when an agent designs a pack, unsupported needs are reported against this matrix's vocabulary ("needs typed-compute merge/sort — queued for v1.x") instead of failing vaguely.
- Grammar freeze rule (ROADMAP v0.3): Spec v1 freezes only after every ✅ row has been written as a real spec file **and rendered by the engine**. The grammar and implementation gates are now met: the engine renders the entire example corpus (15/15 packs in `yat-packs`' `official/`) as of engine v0.2, meaning every exercised ✅ matrix row is backed by a real engine render.
