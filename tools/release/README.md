# Firmware release pipeline

Builds firmware, packages it three ways, and either uploads it as a workflow
artifact (dry run) or attaches it to a GitHub release (real tag). Driven by
[`.github/workflows/release.yml`](../../.github/workflows/release.yml).

## What gets built, per model (`e1002` today)

| File | What it is | How |
|---|---|---|
| `firmware-<model>.bin` | OTA app image | `.pio/build/<model>/firmware.bin`, copied as-is |
| `firmware-<model>.factory.bin` | Merged full-flash image (bootloader + partition table + `boot_app0` + app) | PlatformIO's espressif32 platform builds this automatically as a post-build step; the workflow falls back to a manual `esptool merge-bin` at the same offsets (`0x0`/`0x8000`/`0xe000`/`0x10000`) if it's ever absent |
| `firmware-<model>.uf2` | Drag-and-drop flashable image | `uf2conv.py` over the **factory** image (not the app-only `.bin`), so one UF2 copy lays down everything from flash offset `0x0` |
| `manifest-<model>.json` | `{name, size, sha256}` per artifact above, keyed by role (`app`/`factory`/`uf2`) | `make_manifest.py` |

The manifest is meant to be consumed by the future flasher site (ROADMAP
v0.5) — it can fetch the manifest first, verify a download's sha256, and
knows which file is the OTA image vs. the full-flash image without parsing
filenames.

## Triggers

- **`push` of a tag matching `v*`** — builds, packages, and publishes a
  GitHub release (via `softprops/action-gh-release`) with all artifacts and
  the manifest attached.
- **`workflow_dispatch`** (Actions tab → Run workflow) — same build and
  packaging, but uploads the artifacts as a workflow run artifact instead of
  publishing a release. This is the way to dry-run the whole pipeline without
  cutting a tag.

## Cutting a release

```sh
git tag v0.3.0
git push --tags
```

That's it — the tag push triggers the workflow, which builds `firmware/`,
assembles the artifacts above, and creates the GitHub release.

To test changes to this pipeline without publishing anything, run it via
`workflow_dispatch` from the Actions tab (or `gh workflow run release.yml`)
and check the uploaded artifact bundle.

## Model matrix

`firmware-build` already runs as a `strategy.matrix.model` job; it just has
one entry (`e1002`) today. Adding a model (e.g. `e1001`, ROADMAP v2) means
adding it to the matrix list — every step below it is already
model-parameterized.

## Vendored: UF2 conversion tool

`uf2conv.py` and `uf2families.json` are vendored unmodified from
[microsoft/uf2](https://github.com/microsoft/uf2) (MIT license — full text in
[`LICENSE-uf2.txt`](LICENSE-uf2.txt)). The workflow calls `uf2conv.py`
directly with `-f 0xc47e5767` (the ESP32-S3 family ID from
`uf2families.json`) and `-b 0x0` (the factory image's start offset).

To refresh them from upstream:

```sh
curl -sL -o tools/release/uf2conv.py \
  https://raw.githubusercontent.com/microsoft/uf2/master/utils/uf2conv.py
curl -sL -o tools/release/uf2families.json \
  https://raw.githubusercontent.com/microsoft/uf2/master/utils/uf2families.json
```

## Local verification

The steps below were run locally against an existing `firmware/.pio/build/e1002/`
(from `cd firmware && pio run`) to sanity-check the packaging steps outside CI:

```sh
python3 tools/release/uf2conv.py -b 0x0 -f 0xc47e5767 -c \
  -o firmware-e1002.uf2 firmware/.pio/build/e1002/firmware.factory.bin

python3 tools/release/make_manifest.py --model e1002 --version dev-<sha> \
  --output manifest-e1002.json \
  app=firmware-e1002.bin factory=firmware-e1002.factory.bin uf2=firmware-e1002.uf2
```

Confirmed: the `.uf2` is ~2x the factory `.bin` size (each 256-byte payload
occupies a 512-byte UF2 block), and the manifest's sha256 values match an
independent `shasum -a 256` over the same files. The manual `esptool
merge-bin` fallback command in the workflow was also verified to produce a
byte-identical `firmware.factory.bin` to the one PlatformIO builds
automatically, using the offsets PlatformIO's own build script uses
(`~/.platformio/platforms/espressif32/builder/main.py`).
