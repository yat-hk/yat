# Frozen engine-test packs

Every `.yat-pack.json` in this directory is a **frozen copy**, pinned at the
moment the 17 real-world example packs moved to the community repo
([yat-hk/yat-packs](https://github.com/yat-hk/yat-packs)). They exist solely
as fixtures for `tools/preview/run-tests.sh` blocks that test an **engine**
feature (warnings strip, battery glyph bands, `for_each` partial/bindfail
results, the chart auto-scale regression, the `--profile e1001` mono clamp,
the sushiro-queue column-alignment layout regression, the `image` widget's
render-time fetch) through a real pack as the vehicle.

These are copies, not the living packs — do not edit them to add features or
fix pack-authoring bugs; that work happens on the real pack in the
`yat-packs` repo. Only touch a file here if the *engine* test it backs needs a
different fixture shape, and keep the diff from the original pack as small as
that requires.

| File | Why it's frozen here |
|---|---|
| `hko-now.yat-pack.json` | warnings strip (§warnsum second source), standard-chrome battery glyph bands (G25/§11.4), the §11.3 empty-state card, `--profile e1001`'s literal-ink clamp |
| `temp-trend.yat-pack.json` | the winter-fixture `chart` auto-scale regression (§9.12a) and `show_bounds` |
| `commute-combo.yat-pack.json` | the `for_each` partial-results / `E_BIND` bindfail pair (`docs/RFC-foreach-partial-results.md`) |
| `sushiro-queue.yat-pack.json` | the ragged wait-time column layout regression, and one leg of the `--profile e1001` trio |
| `photo-frame.yat-pack.json` | the other leg of the `--profile e1001` trio (dither palette narrowing to black/white on a real decoded photo) |
