# YAT — agent guide

YAT is an open-source, serverless e-ink display platform for Hong Kong
(SeeedStudio reTerminal E1002). Pages are **packs**: declarative JSON documents
executed by a C++ engine in the device firmware. There is no backend anywhere.

## If you were asked to develop, modify, or preview a pack

Follow `skills/pack-developer/SKILL.md` — it is the complete workflow
(spec references, validation command, fixture + preview commands, current
engine support matrix, design rules). Read it in full before writing anything.

## Repo map

| Path | What |
|---|---|
| `docs/PACK-SPEC.md` | Pack Spec (normative, agent-first) |
| `schema/yat-pack.schema.json` | JSON Schema — every pack must validate |
| `docs/SPEC-VALIDATION.md` | Use-case matrix + gap vocabulary |
| `packs/examples/` | `render-test.yat-pack.json`, the engine-conformance pack, only — real-world packs moved to [github.com/yat-hk/yat-packs](https://github.com/yat-hk/yat-packs) |
| `packs/internal/` | Firmware-embedded packs (welcome, warning-takeover) — never user-installed, stay in this repo |
| `engine/` | Portable C++ spec engine (firmware / native / WASM) |
| `tools/preview/` | Native renderer `yat-preview`, `simulator.py` (web UI :8737), `run-tests.sh` (goldens), `fixtures/` |
| `firmware/` | PlatformIO project for the device (`pio run`) |
| `PRD.md` / `ARCHITECTURE.md` / `ROADMAP.md` | Product + technical design |

The website (https://yat.day) is a separate, private repository — it is not
part of this checkout and there is nothing to look for under `site/` here.

## Ground rules

- Packs are data, never code. If the spec can't express something, report the
  gap (see SPEC-VALIDATION vocabulary) — do not invent syntax.
- Never commit real credentials. `firmware/include/secrets.h` is gitignored;
  pack secrets are declared by name only.
- Engine/spec changes are RFC-gated; pack development needs neither.
- Run `tools/preview/run-tests.sh` before and after touching `engine/`.

## Releases and repo model

Development happens here, in the open. History starts at the v0.5.0 public
beta: everything before that was a private working repo whose commits are not
published, which is why the log begins where it does rather than at the
project's first day. Practical consequences:

- A tag `vX.Y.Z` fires `.github/workflows/release.yml`: firmware artifacts
  (`firmware-e1002.bin` / `.factory.bin` / `.uf2` + sha256 manifest) publish to
  the GitHub release. Devices offer that release through their own settings
  page (the phone checks; the device downloads only from this repo's releases).
- The website (https://yat.day) is a separate, private codebase — not part of
  this repository. When a change here to `engine/` or `packs/` affects the
  gallery or the WASM demo, the maintainer rebuilds the site's committed
  artifacts; that isn't something a contributor in this repo needs to do.
- Outside contributions are welcome as ordinary PRs here; the maintainer
  applies them to the private working tree and credits the PR in the release
  commit that ships them.
- Firmware's version constant lives in `firmware/src/yat_common.h`
  (`FW_VERSION`); it must agree with the release tag being cut.
