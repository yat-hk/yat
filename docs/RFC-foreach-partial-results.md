# RFC — Partial results for `for_each` sources

**Status:** Adopted 2026-08-04, implemented. Both D and A landed together (`engine/src/extract.cpp`, `engine/src/render.cpp`), plus the spec/schema deltas below (PACK-SPEC §5.1a, §9.8, §9.8a, §11.3, §11.4; `schema/yat-pack.schema.json`'s `collectExtractMap`). See PACK-SPEC's 2026-08-04 changelog entry for the full summary. This document is left as-written below as the design record; it is no longer "for owner decision."
**Date:** 2026-08-04 · **Target:** PACK-SPEC 0.4 / engine v0.3
**Touches:** PACK-SPEC §5.1a, §9.8, §11.3, §11.4 · `engine/src/extract.cpp` · `schema/yat-pack.schema.json`
**Companion docs:** [PACK-SPEC.md](PACK-SPEC.md) · [PRD.md](../PRD.md) · [ROADMAP.md](../ROADMAP.md)

---

## 1. Problem

PACK-SPEC §5.1a rule 5: failure of **any** `for_each` iteration marks the whole source failed. `commute-combo` fans out over `params.kmb_stops`; when one stop's fetch or parse fails — 272K on a reduced Sunday service is the household's likeliest trigger — the engine discards the good stop too. `data.kmb_etas` becomes `null`, the bound `list` hits `E_BIND`, and per §9.8 `E_BIND` renders **nothing** and suppresses the pack's `empty_text`. The KMB block collapses to a bare header with a 3 px footer glyph under it. One flaky stop costs the user every stop.

The all-or-nothing rule was not an accident, and the steelman is strong: **mixed-freshness rows are actively misleading.** A list showing a live 08:14 next to a two-hours-stale 08:14 is worse than showing neither, because nothing on the row says which is which. For transit specifically, a wrong departure time sends someone to the stop for a bus that left. §5.1a rule 5's last sentence — "per-iteration partial results are not mixed across wakes" — is defending exactly that.

Two corrections to the ROADMAP note, both verified against the code:

- **"Blanks every KMB row" is the no-snapshot case only.** `extract.cpp:433-444` stale-serves the whole last-good array when one exists. On a device that has fetched successfully once (`g_fsReady`, `firmware/src/main.cpp:6503`), the common failure is *every row silently stale*, footer badge only — not blank. Blank is the first-wake path, the no-filesystem path, and every golden (preview persistence is opt-in via `--state`, `tools/preview/main.cpp:177`). Both outcomes are bad; they are bad differently, and a fix must cover both.
- **The shipped empty-state card does not catch this.** §11.3's fallback card fires only when *every* source is empty. `commute-combo`'s MTR and CTB sources succeed, so the page renders "fine" with a hole in it.

Also worth noting: `commute-combo`'s row template **already** handles per-item absence — `when: "item.eta1 missing"` draws a `—`. The author's mental model for "this one row has no time" exists and works. It is the source-level verdict that overrides it.

---

## 2. Options

### A. Status quo + let `E_BIND` say something

**Spec delta:** small, but not the one the ROADMAP proposes. "Show `empty_text` on `E_BIND`" contradicts a normative MUST NOT in §9.8: *"'add symbols in settings' (empty install) and 'your API key is rejected' (never-succeeded source → `null` → `E_BIND`) are different user messages and MUST NOT share one string."* That rule is right. The workable version is **engine-owned copy**: on `E_BIND`, the list draws one secondary-type line, 暫時攞唔到 / "Can't get this right now", in the pack's own voice-neutral chrome style — the widget-scale sibling of the §11.3 fallback card. New: §9.8 amendment, §11.3 row edit.

**Engine:** ~15 lines in `render.cpp:659` (`if (!lp.bindOk) return 0;` becomes a call into the same helper as `emptyTextRow`). No `extract.cpp` change.

**Author model:** unchanged. Authors keep writing `empty_text` for genuinely-empty; the engine owns the failure line.

**On screen:** the KMB block shows a header and one grey line instead of a header and a void. The working stop's live ETAs are still thrown away.

**Note:** this conflicts with §11.3's "the engine never substitutes error text into the pack's own widgets" — today engine copy appears only at page scale. Adopting A means accepting engine copy at widget scale. That is a real principle change, and it is one worth making regardless of which option wins, because `E_BIND` has causes beyond `for_each`.

### B. Partial results with per-iteration staleness

**Spec delta:** large. Rewrite §5.1a rule 5; add reserved element members `stale` and `as_of` alongside `each` (rule 3); rework §11.3's stale-serve from per-source to per-element; add a per-row staleness marking rule to §9.8, because an unmarked stale row *is* the misleading case the current rule exists to prevent.

**Engine:** the snapshot shape changes. Today `{"t":epoch,"fields":{"kmb_etas":[…]}}` carries one timestamp per source (`extract.cpp:333`). Per-element staleness needs per-element timestamps keyed by element identity (canonical `each`, so the map survives the user reordering their stops) plus merge-on-write. Perhaps 150 lines across `fetchForEach`, `loadSnapshot`, `putSnapshot`, and a render-side marker.

**Author model:** heavier. Every `for_each` pack now has three per-row states to think about, and an author who ignores them ships the misleading render.

**On screen:** best case, all stops shown, stale ones marked. Worst case — author didn't mark — a stale ETA sitting next to a live one.

### C. Opt-in via `"on_iteration_error": "skip" | "fail_all"`, default `fail_all`

**Spec delta:** one optional member in the §5.1 source table, one paragraph in §5.1a, one schema enum. Smallest delta that changes behaviour at all.

**Engine:** the branch in §D below, gated on a flag. Same code, plus the flag.

**Author model:** an author must know the flag exists to get the good behaviour. Non-technical households install gallery packs; they will never set it. So the flag's value is entirely in what *we* set on the three shipped `for_each` packs — which means the default is doing no work except preserving a bug for third-party packs.

**On screen:** identical to D for packs that opt in, identical to today for packs that don't.

### D. Partial results, fresh-only — no mixing (recommended)

Drop the all-or-nothing verdict but **never mix stale into a partial array**, which removes the entire justification for the current rule instead of managing it.

Per wake, a `for_each` source lands in one of three states:

| Iterations | `collect.field` | Staleness | Snapshot |
|---|---|---|---|
| all succeed | full fresh array | fresh | written (as today) |
| some fail | full-length array; failed elements carry `each` + `status: "failed"` + `null` extract fields | **fresh** — no stale values mixed in | not written |
| all fail | `null` → stale-serve the whole last-good array (unchanged) | stale, footer badge | not written |

The array is always one element per input element in input order (§5.1a rule 3 holds), so `{{index}}` is stable and a failed stop never silently vanishes from the list.

**Spec delta:** rewrite §5.1a rule 5 (~8 lines); add reserved member `status` (`"ok"` | `"failed"`) to rule 3 plus a **[V]** rule that `collect.extract` field names may not be `each` or `status` — note §5.1a reserves `each` today with no such [V] rule, a latent gap either way; add an `E_ITER` row to the §11.3 table and extend §11.4's data-warning glyph trigger set to include it.

**Engine:** `extract.cpp` only, ~30 lines. In `fetchForEach` (line 270), `break` on a failed iteration becomes `continue` after appending a `status: "failed"` element; the `failed` flag becomes a count so the caller can distinguish some-failed from all-failed. The all-failed signal must move off the `out[fieldName].isNull()` sentinel used at line 433 to an explicit out-param. Gate the `putSnapshot` at line 450 on a zero failure count, so a later total outage still stale-serves a *complete* array.

**Author model:** the one already in `commute-combo`. A failed iteration's fields are `null`, so existing `when: "item.eta1 missing"` guards fire correctly with **no pack change**. Authors who want to distinguish "no bus scheduled" from "couldn't ask" add `when: "item.status == 'failed'"` — legal today under §6.5 with no grammar change.

**On screen:** the good stop shows live times; the flaky stop shows its route and `—`; the footer shows the data-warning glyph. Nothing on the page is stale, so nothing is misleading.

**Cost:** today a failed iteration short-circuits the rest (`break`). Under D, three dead stops cost three timeouts instead of one. Worst-case fetch count is unchanged (already counted as `maxItems` in the ≤8 cap, §12.1), and the per-wake network budget remains the backstop via `E_BUDGET` — but wake duration under a full outage grows, which matters for battery.

---

## 3. Recommendation

**Adopt D, and ship A alongside it.**

D against the product bar: the screen is never silently blank (the working stop renders, the failed row renders its label and a dash), and nothing misleading appears, because a partial array is *entirely fresh* — the mixed-freshness hazard is designed out rather than mitigated with markers the author may forget. It reaches the household's likeliest failure — exactly one flaky stop — with zero changes to any shipped pack, because the per-item absence idiom is already in the packs. Cost is ~30 lines in one file and no snapshot-format migration.

A is not redundant under D. `E_BIND` still occurs when a `for_each` source fails entirely with no snapshot on record — first wake, factory-fresh device, the exact moment a new owner is deciding whether the thing works — and for every non-`for_each` list too. A guarantees no list ever renders as a void.

B is the same product outcome as D at five times the spec and engine surface, and it re-admits the misleading render as its own failure mode. C is D behind a flag whose default preserves a defect for everyone who doesn't read the spec; if the owner wants the escape hatch, add `"on_iteration_error"` in D's shape with default `"skip"` and `"fail_all"` available for a pack that genuinely needs whole-array coherence.

---

## 4. Migration and compatibility

- **No pack file changes.** All three shipped `for_each` packs (`commute-combo`, `news-sections`, `stock-ticker`) parse and render unchanged. `commute-combo`'s existing `item.eta1 missing` guard covers the new element shape as-is.
- **No golden churn.** Goldens run without `--state` and with a full fixture list per source, so every iteration succeeds and D's changed branch is never entered. Verified by inspection of `tools/preview/fixtures/commute-combo.kmb.{1,2}.json` and the opt-in store at `tools/preview/main.cpp:177`. A's engine copy only renders on `E_BIND`, which no current golden reaches.
- **New goldens to add** (both cheap — the harness already fails an iteration when its fixture list runs out, `main.cpp:194`): `commute-combo` with one KMB fixture instead of two (D's partial path), and any list pack with a failing single source (A's line).
- **Behaviour that does change**, deliberately: a mixed-outcome wake now renders partial data where it previously rendered whole-array stale or nothing. That is the point of the RFC; it is not reachable by any pack that was previously rendering correctly.
- **Schema:** one optional enum if C's flag is adopted; otherwise the `collect.extract` reserved-name **[V]** rule only.

---

## 5. Open questions for the owner

1. **Widget-scale engine copy.** A breaks §11.3's "never substitutes error text into the pack's own widgets". Accept the principle change, or keep engine copy strictly at page scale and let lists render nothing?
2. **Snapshot staleness under a permanently dead iteration.** D declines to write a snapshot on a partial wake, so if one stop fails forever, the last-good array ages indefinitely and only surfaces on a total outage. Acceptable, or should a partial wake merge its successful elements into the stored snapshot (which pulls in B's per-element keying)?
3. **Does `status` earn a place in the render, or only in `when`?** Proposed as `when`-only in D. Exposing `{{item.status}}` as text invites authors to print a raw enum on a household screen.
4. **`"on_iteration_error"` at all?** Recommendation is no flag. If a third-party pack genuinely needs whole-array coherence (a diff or a ratio across iterations, which no shipped pack does), the flag is the escape hatch — but it is spec surface bought for a hypothetical.
5. **Bilingual copy for A** — the fallback card's 暫時攞唔到資料 is page-scale phrasing. A one-line widget-scale variant needs the copy owner, not this RFC.
6. **Does this land in 0.4, or does the 0.3 freeze hold?** D is ~30 lines and no format migration; it is the cheapest fix the spec will ever get for this, and every wake before it ships is a wake where a Sunday bus schedule can blank the commute page.
