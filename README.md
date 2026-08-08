# YAT 日

**Your day, quietly displayed.** An e-ink display that fetches and draws
everything itself — Observatory weather and warnings, bus and MTR arrivals,
headlines — built for everyday life in Hong Kong.

<!-- Six real engine renders, composed by tools/preview. A photograph of the
     device on a shelf belongs here too, once one exists. -->
![Six YAT pages: 9-day forecast, family notice board, commute board, air quality, photo frame, restaurant queue times](https://yat.day/assets/hero.png)

YAT turns a ~US$99 off-the-shelf device (SeeedStudio reTerminal E1002) into a
calm information display: glanceable, bilingual, and running for weeks on a
battery.

Three things separate it from other e-ink dashboards:

- **There is no server. Anywhere.** Not hosted, not self-hosted, not a hub in
  your house. A spec engine inside the firmware calls public APIs directly over
  your WiFi and renders on the device. No account, no cloud, no subscription, no
  telemetry — the project hosts nothing but static files, so there is nothing of
  ours to go down or go away.
- **A page is one JSON file.** A **pack** declares where to fetch and what to
  draw; installing one is copying a file — no reflash, no build, no deploy.
  Write one by hand against the spec, or describe it to a coding agent, which is
  what the spec was shaped for.
- **Bilingual, 廣東話 first.** Traditional Chinese and English throughout, and
  the device's own screens are written in Cantonese rather than translated out
  of English. It knows what a T8 signal and a black rainstorm mean.

**What you need:** a reTerminal E1002; a computer running desktop Chrome or Edge
and a USB-C *data* cable, once, to flash it; then a phone and 2.4 GHz WiFi for
everything after that. There is nothing to sign up for.

**Start here:** [docs/SETUP.md](docs/SETUP.md) takes a sealed box to the first
page on the panel. From there: the [yat-packs](https://github.com/yat-hk/yat-packs)
repo for what it can show, [docs/PACK-SPEC.md](docs/PACK-SPEC.md) to write a
page of your own, or [AGENTS.md](AGENTS.md) if you are pointing an AI agent at
this repo.

## Status: early development, but the whole path works

Someone has gone from a sealed box to their own chosen pages on the panel,
without a terminal. The setup flow is three moves, and the device's screen
leads each one:

1. **Flash** — [yat.day/tools.html](https://yat.day/tools.html) installs the
   current release over USB-C from Chrome or Edge (WebSerial + esptool-js,
   automatic bootloader entry). This is the only step that needs a computer,
   once per device.
2. **Wi-Fi, from a phone** — hold the green button until it beeps (usually
   5–10 seconds — the device only starts counting once it has woken, so don't
   count seconds yourself); the panel draws a QR for the device's own
   `YAT-xxxx` network, and its captive portal asks for your home 2.4 GHz
   network.
3. **Pages, from a phone** — the same gesture then puts
   `http://yat-xxxx.local` (plus a QR, plus the numeric address) on the panel.
   The device serves the settings page itself and pulls this repo's pack
   library into it, so choosing and configuring pages needs no cable and no
   account. Setup closes itself after ten idle minutes; the same button hold
   reopens it, any time, forever.

Working today, verified on hardware:

- **Pack Spec 0.3-draft** ([docs/PACK-SPEC.md](docs/PACK-SPEC.md)) — the
  declarative pack format, with a strict [JSON Schema](schema/yat-pack.schema.json)
  and [17 real example packs](https://github.com/yat-hk/yat-packs) against live HK
  APIs; 16 of them install straight from the gallery
- **Portable engine** ([engine/](engine/)) — one C++ core, three targets:
  device firmware, native CLI, and WebAssembly, verified pixel-identical
  ([tools/wasm/](tools/wasm/))
- **Local simulator** — `cd tools/preview && make && python3 simulator.py`
  → browse packs, tweak params, preview renders at http://localhost:8737
- **Firmware** ([firmware/](firmware/)) — wake → fetch → render → 6-color
  panel refresh → deep sleep, with hash-skip (unchanged data never burns a
  refresh), button wake, warning takeover, and an OTA-ready partition layout
- **One-tap updates from the phone** — the settings page checks GitHub for a
  newer release (the device itself never phones home), and a tap has the
  device download and install it over your own WiFi, rolling back
  automatically if anything goes wrong. Rehearsed end-to-end on hardware.
  First flash still needs the computer; every update after that doesn't.
- **Tap-to-talk** — a short tap on the green button records ~4 s, transcribes
  it, and matches the words against pack `aliases` to switch pages. It needs a
  speech-to-text key of your own, pasted into the settings page; with no key
  the microphone is never powered and the button just chirps.

**Not done yet:** the v0.1 battery measurements (sleep current, mAh per
refresh) are still not recorded, so every battery-life number here is an
estimate. Building from source instead of the release asset still works too
([tools/release/](tools/release/) has the commands). See
[ROADMAP.md](ROADMAP.md).

## Developing a pack

Packs are written *by AI agents* by design — the spec is machine-precise, and
this repo is self-guiding: point any coding agent (Claude Code, Codex, …) at
the repo root and ask for the page you want. See [AGENTS.md](AGENTS.md) and
[skills/pack-developer/SKILL.md](skills/pack-developer/SKILL.md).

```
$ cd yat && <your coding agent>
> "Build me a pack showing GMB minibus arrivals for route 69 in Sai Kung"
```

Humans are welcome too: [docs/PACK-SPEC.md](docs/PACK-SPEC.md) is the
normative reference, and `tools/preview/yat-preview` renders any pack to a
PNG without hardware.

## Documents

| | |
|---|---|
| [PRD.md](PRD.md) | Product requirements — what YAT is and is not |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical design — engine, firmware, security model |
| [ROADMAP.md](ROADMAP.md) | Versioned milestones with exit criteria |
| [docs/PACK-SPEC.md](docs/PACK-SPEC.md) | The normative pack format |
| [docs/SPEC-VALIDATION.md](docs/SPEC-VALIDATION.md) | The use-case matrix that forged the spec |
| [docs/UX-FLOWS.md](docs/UX-FLOWS.md) | Every screen and sound the device can produce, and when |
| [BRAND.md](BRAND.md) | Brand and design language |

The website ([yat.day](https://yat.day) — flash page, setup guide, gallery, and
the portal UI the device serves) is a separate, private codebase, not part of
this repository.

## License

[GPL-3.0](LICENSE) covers this repository's contents (firmware, engine, packs,
and tooling) — free to use, study, and modify; anything you distribute built on
it must be open under the same terms, with source and attribution, and buyers
of any derived hardware must be able to install modified firmware (GPLv3 §6).
Vendored components keep their own permissive licenses: see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The website
([yat.day](https://yat.day)) is a separate, private codebase not covered by
this license.

The **YAT name and logo are not licensed** — code reuse under the GPL does not
grant the right to present a product under this name. For commercial licensing
outside GPL terms, contact the author.

Copyright (C) 2026 Alan Ho.
