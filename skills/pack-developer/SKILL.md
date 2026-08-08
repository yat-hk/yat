---
name: yat-pack-developer
description: Develop a YAT content pack — a declarative JSON page for the YAT e-ink display. Use when asked to create, modify, validate, or preview a pack (a .yat-pack.json), or to add a new information page/widget to a YAT device.
---

# Developing a YAT pack

A **pack** is one JSON file that turns a data source into an 800×480 e-ink page.
No code — a *data section* (HTTPS/inline sources + path-expression extraction)
and a *render section* (a widget tree). The device's firmware engine executes it.

**This repo (`yat-hk/yat`) is the engine/spec home; pack development itself
happens in the sibling [`yat-hk/yat-packs`](https://github.com/yat-hk/yat-packs)
repo.** Clone it next to this one if it isn't checked out yet. Everything
below that touches a pack file — writing it, validating it, capturing
fixtures, previewing it — runs from the `yat-packs` checkout; this repo only
supplies the spec, schema, and preview tooling those commands point back at
(`YAT_CORE`, defaulting to `../yat`, is `yat-packs`' own convention for that
path — see its `AGENTS.md`/`tests/run-tests.sh`).

**Normative references (read before writing anything):**
- `docs/PACK-SPEC.md` (in this repo) — the spec (grammar, widgets, filters, caps, security). It is written for you, an AI agent: one canonical form per construct, no shorthand, ambiguity forbidden.
- `schema/yat-pack.schema.json` (in this repo) — the machine contract. Your file MUST validate.
- `yat-packs/official/` (in the sibling repo) — 17 real packs. `hko-now.yat-pack.json` is the smallest; `tides` and `news-sections` show advanced constructs. A new pack you write goes in `yat-packs/community/` instead — only the YAT project's own releases add to `official/` directly (see `yat-packs/CONTRIBUTING.md`). This repo's own `packs/examples/render-test.yat-pack.json` is the engine-conformance pack, not an authoring example — leave it alone.

## Workflow

1. **Understand the use case.** What does the *end user* configure? Those become
   `params` (JSON Schema subset, spec §3) — the install form is auto-generated
   from them. Use `enum` + `enum_titles` (bilingual) for fixed choices,
   `default` on everything optional-feeling, `depends_on` for conditional fields.

2. **Probe the real API first.** `curl` the actual endpoint; look at the real
   response shape before writing extraction paths. HTTPS only, port 443. If an
   API needs a key, declare it under `secrets` with an honest `sent_to`
   host allowlist — never inline a key in the pack.

3. **Write the pack** to `community/<id>.yat-pack.json` in the `yat-packs`
   checkout (`id` = filename stem; only the YAT project's own releases add to
   `official/` directly — see `yat-packs/CONTRIBUTING.md`). Bilingual by
   default (Traditional Chinese + English — this is a Hong Kong-first
   product). Standard chrome on. Battery rules: extract only fields you
   display (`items|first(5)`, not all 20), quantize jittery numbers at
   extraction (`|round(0)`) — unchanged extracted data skips a 30-second panel
   refresh.

4. **Validate** (must pass, zero errors), from the `yat-packs` checkout
   against this repo's schema:
   ```
   npx --yes ajv-cli@5 validate --spec=draft2020 \
     -s ${YAT_CORE:-../yat}/schema/yat-pack.schema.json -d community/<id>.yat-pack.json
   ```
   The schema cannot check everything: also self-check the canonical forms —
   space-free placeholders `{{a|f(1)}}`, space-free extract pipelines `a|[0]`,
   one space around `when` comparators, zero-arg filters without `()`.

5. **Capture a fixture** so previews are deterministic and offline, into the
   `yat-packs` checkout's own `fixtures/`:
   ```
   curl -s '<real url with defaults substituted>' \
     > fixtures/<id>.<sourceid>.json
   ```

6. **Preview** with this repo's native engine, run from the `yat-packs`
   checkout:
   ```
   make -s -C ${YAT_CORE:-../yat}/tools/preview
   ${YAT_CORE:-../yat}/tools/preview/yat-preview community/<id>.yat-pack.json \
     --doc <sourceid>=fixtures/<id>.<sourceid>.json --out <id>.png
   ```
   Pin time with `--now <epoch>` for deterministic renders (standard chrome
   draws a clock; unpinned renders differ run to run).
   Look at the PNG. Iterate the render section until the page is glanceable:
   the primary figure in `bignum`, secondary facts in `text`, generous spacing.
   Once the pack has a golden under `yat-packs/goldens/`,
   `YAT_CORE=../yat ./tests/run-tests.sh` (run from `yat-packs`) golden-tests
   the whole pack library against this repo's engine.

7. **Test param variations**: render again with `--params '{"k":"v"}'` for at
   least one non-default configuration and confirm both extraction (stderr
   shows the extracted `data`) and the visual. `--params` **merges over** the
   pack's defaults (you only pass the keys you change). Caveat: `--doc`
   selects a fixture by source id and ignores the substituted URL — a fixture
   render can NOT prove URL-affecting params. To prove those, capture a
   second fixture from the non-default URL (or use `--live`).

## Engine v0.2 layout traps (until the v0.3 font/layout work lands)

- **Put `flex: 1` on your top-level container.** The root column distributes
  leftover space to `flex` children only; a top-level container without it is
  sized to its content and everything stacks at the top (a `spacer` with
  `flex` inside it contributes 0, so it cannot push the container taller).
- **Don't rely on row `align: center|end` around a vertical `divider`** — the
  divider collapses to a dot. Use `align: "start"` plus flex spacers instead.
- **A `row` is only as wide as its content.** In a row, non-flex children take
  their natural width, each clamped to what earlier siblings left free; the
  last child does not stretch to the right edge. Put `flex: 1` on the child
  that should absorb the slack (or a `spacer` with `flex: 1` before a
  right-aligned one).
- **v0.1 font metrics diverge from the spec §9.3 tables** (glyphs are
  integer-scaled 16 px bitmaps; e.g. bignum xlarge chars advance 64 px, text
  medium line-height is 36). Consequence: long `bignum` strings clip silently —
  keep them ≤5 characters at `xlarge` in a half-width column, or drop to
  `large`. Always eyeball the PNG for right-edge clipping.
- **`icon` is real and the §9.9 catalog is closed** — all 32 names draw real
  artwork. Each is a 24×24 1-bit bitmap blitted at integer scale ×1/×2/×4 for
  `small`/`medium`/`large`, in the widget's `color`, so nothing blurs or
  half-pixels at any size. The bitmaps are *generated*: author them as geometry
  in `tools/icons/gen_icons.py` (the source of truth) and run
  `python3 tools/icons/gen_icons.py` to rewrite `engine/src/icons.cpp` and the
  contact-sheet fixture — never hand-edit the C arrays. There is no way to add
  a 33rd name from a pack: the enum is closed (adding one is a spec-minor
  engine release), and an off-catalog name falls back to the bordered
  placeholder box with a `render warn:` naming §9.9. Practical sizing: at
  `small` an icon matches `text` `small`/`medium` line height well; at `large`
  (96 px) it holds its own next to an `xlarge` `bignum` — see
  `yat-packs/official/aqhi.yat-pack.json` for the large red `alert`.
  **`qr` is a real encoder** (§9.11, vendored Project Nayuki qrcodegen):
  black-on-white, ecc `L`/`M`/`Q`/`H`, quiet zone 4 modules, integer module
  scale centered in the declared `size` box. A payload over 512 bytes
  post-substitution, or an encode failure, falls back to the same bordered
  placeholder box (with a `render warn:` line).
- **`image` is real** (§9.10): fetched at *render* time (not during
  extraction), decoded, nearest-neighbor scaled into the declared
  `width`×`height` box per `fit`, and mapped to the 6-ink palette by
  `dither`. It has no source id — the native preview always fetches it as
  fixture id `"image"` (`--doc image=some.png`). `fit`/`dither` are literal
  enums, not param-templatable (unlike `color` and `list.max_rows`); a pack
  that wants a user-facing fit choice needs two mutually-exclusive `image`
  widgets gated by `when: "params.<fit_param> == '...'"` — see
  `yat-packs/official/photo-frame.yat-pack.json`, which does exactly this to
  stay within the §9.10 **[V] cap of 2 `image` widgets per pack**. Any
  failure (no fetch function, fetch error, >204800 fetched bytes, not a PNG,
  no decoder installed, decode failure, decoded pixels >800×480) draws the
  spec's empty box + a `render warn:` line, never a fatal error. **PNG
  decoding is pluggable and host-supplied** — the portable engine core links
  no image codec of its own (`yat::ImageDecoder`, `engine/include/yat/
  canvas.h`; `Engine::setImageDecoder()`). The native preview wires
  `stb_image` (`tools/preview/main.cpp`'s `StbImageDecoder`); **firmware
  wiring (pngle) is not done yet** — a pack using `image` previews correctly
  today but won't render on-device until that lands.

## Engine v0.2 support matrix (important)

The spec (0.3-draft) is larger than the current engine. The engine **renders**:
sources `https`/`inline` with formats `json`/`rss`/`csv`, source-level `when`,
`for_each`+`collect` (`{{each}}`), sequential source references
`{{sources.<id>.<field>}}` (§5.7 — a later source's URL path/query and
extract filter literals may substitute an earlier source's extracted field;
backward-only, null referent -> `E_REF_NULL` fails that source), `compute`;
extraction with one filter + projection + pipes
`[n] length first(n) last(n) round(n)`; RFC 3986 percent-encoding of URL
substitutions; placeholders `{{params.*}} {{data.*}} {{now}} {{strings.*}}
{{item...}} {{item[n]}} {{index}}` with filters
`round pad time_hhmm date_fmt date_diff_days upper weekday abs sign_char
pick_by_day enum_title`; `when` (truthiness, `== != < <= > >= has`,
`exists`/`missing`, `any`/`all`, `item`/`index` roots); widgets
`column row spacer divider text bignum list qr image icon` (incl. one nested
`list` level, §9.8a — outer bind a `for_each` source's `collect.field`,
inner bind `item.<prop>` over that source's `collect.extract`, outer x
nested `max_rows` ≤40 [V]); `icon` (§9.9) draws the full closed 32-name
catalog as real artwork at all three sizes; `strings`
table + `render.lang_param`; stale-serve (§11.3) and per-source
`min_refresh_min` (§5.1) — see below; `image` (§9.10) — see the layout trap
above for details, caveats, and the firmware-decoder gap.

Not yet rendering (spec-valid, engine says "not implemented"):
`{{sources.*}}` combined with a `for_each` source.

### Stale-serve (§11.3) and `min_refresh_min` (§5.1)

The engine persists each source's last successfully extracted fields +
timestamp via an optional `yat::StateStore` (`engine/include/yat/engine.h`;
`Engine::setStateStore()`). A null store (the default — firmware doesn't
wire one yet, and the native preview only does with `--state <dir>`)
disables persistence entirely: every failure yields fresh nulls, exactly
v0.2 behavior, and packs render identically either way.

With a store attached: on fetch/parse failure (incl. `E_REF_NULL` and any
failed `for_each` iteration) the engine loads that source's last-good
snapshot if one exists, serves its fields, and marks the source stale
(`Engine::anyStale()` / `oldestStaleSince()`); the standard chrome footer
then shows a small red "stale HH:MM" marker right of the source
attribution, and a render warning names it. No snapshot on record (source
has never succeeded) → fields stay `null`, not stale — that distinction is
normative (§11.3). Per-source `min_refresh_min` (5–10080 minutes, `https`
sources only): within the interval since the snapshot's timestamp, the
engine skips the network entirely and serves the snapshot's fields as
fresh (no stale flag, no glyph).

Preview it with `yat-preview pack.json --state <dir> ...` (`tools/preview/
main.cpp`'s `FileStateStore`, one JSON file per source under `<dir>`); omit
`--state` for the old fixture-only, no-persistence behavior.

**Rule:** if the use case fits the supported subset, target it so the preview
works. When hand-unrolling a missing `list` into fixed rows, watch the caps:
each unrolled row costs extract fields (≤16/source, ≤32/pack) — cap your row
count accordingly and say so in the pack description. If it genuinely needs more, write the pack to the FULL spec anyway
(validate against the schema), preview what you can, and report precisely which
constructs block the preview — using the vocabulary of `docs/SPEC-VALIDATION.md`.
Never invent syntax that is in neither the spec nor the schema.

## Design rules (non-negotiable)

- 6 fixed inks only: `black white red yellow green blue`. High contrast; no
  fine gradients.
- **Never let yellow be the only thing carrying a meaning** (§9.1a-a). On the
  real panel yellow measures ~1.1:1 against white: a yellow number, icon or
  rule is not faint, it is *absent* — and the preview's brighter yellow hides
  that from you. This is why `warn` draws its marks in **black** and keeps
  yellow for the field the engine lays behind them (a `bar`'s fill, the §11.4a
  banner). A raw `"yellow"` still draws yellow everywhere, so if you spell the
  ink, pair it with a word, a shape or a length that survives without it.
- **Spell colour as a role, not an ink** (§9.1a): `accent` `good` `warn`
  `danger` `info` `muted` — today red / green / black-on-yellow / red / blue /
  black.
  A role says what the value means and lets the engine pick the ink — and
  device profiles now exist: `yat-preview --profile e1001` renders any pack on
  the mono palette (chromatic inks clamp to black), which is the cheap way to
  check your pack still reads without colour. Write the raw ink only when the colour *is*
  the meaning and no other colour would do: official warning colours, operator
  livery, the HK market's red-up convention, a hot-red/cold-blue temperature
  scale. Both spellings work anywhere `color` is accepted, including inside
  the `{ "param": … }` form; anything else warns and draws the default ink.
- The panel refreshes in ~30 s. Pages are *glanced at*, not read — one primary
  figure, few words.
- Footer attribution is automatic (standard chrome), as is the optional
  battery glyph (`Engine::setBatteryPercent()`, G25/§11.4 — firmware-driven,
  not pack-controlled); never fake official HK warning iconography
  (firmware-reserved).
- Voice `aliases` in three scripts (`en`, `zh-Hant`, `jyutping`) — users switch
  pages by saying these.
