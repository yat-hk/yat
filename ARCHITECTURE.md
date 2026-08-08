# YAT — Technical Architecture

**Version:** 0.3 (pre-implementation design — serverless/on-device revision)
**Companion docs:** [PRD.md](PRD.md) · [ROADMAP.md](ROADMAP.md)

Target hardware: SeeedStudio reTerminal E1002 — ESP32-S3 (XIAO core module), 8 MB OPI PSRAM, 32 MB flash, ED2208 800×480 E Ink Spectra 6 panel (6 fixed inks, 4bpp, **full refresh only, ~25–30 s**, no partial refresh, temperature-compensated waveforms fed by onboard SHT4x), PDM mic, 3 buttons, PCF8563 RTC, battery ADC, SD slot, 2000 mAh battery.

Reference implementation studied: an earlier private hardware prototype (Arduino via pioarduino/PlatformIO + Seeed_GFX). Reusable assets identified there: serpentine Floyd-Steinberg 6-ink ditherer, WiFiManager QR captive portal (with HTTP pump on a core-0 task — required because a panel refresh blocks `loop()` for ~30 s), efont CJK glyph blitter, PDM→WAV→cloud-STT pipeline (incl. a 2712-entry simplified→traditional table), dependency-free SHT4x/PCF8563/battery drivers.

---

## 1. System overview — no server, anywhere

**Everything runs on the device.** There is no backend — not a hosted one, not a self-hosted one, not a free-tier Worker. The firmware contains a **spec engine** that executes declarative content packs: it fetches data from public APIs directly, extracts fields, renders a widget tree into the framebuffer, and sleeps. The YAT project hosts exactly one thing: a static web flasher site.

```
┌───────────────────────── reTerminal E100x ──────────────────────────────┐
│  deep sleep ──timer/button──▶ wake → NTP/RTC → which page is due?       │
│    ▼                                                                    │
│  SPEC ENGINE (firmware)                                                 │
│    fetch: HTTPS GET api.data.gov.hk / HKO / RSS …   (direct, no proxy)  │
│    extract: path expressions → named fields          (ArduinoJson)      │
│    hash: data unchanged? → skip render+refresh → sleep                  │
│    render: widget tree → 4bpp PSRAM framebuffer   (Noto Sans / Sans TC) │
│    ▼                                                                    │
│  ~30 s panel refresh → deep sleep (server-grade logic, zero servers)    │
│                                                                         │
│  storage: LittleFS = config.json + packs/*.yat-pack.json                │
│           NVS     = WiFi creds + named secrets (API keys) + error ring  │
│  setup mode (green button held): AP + captive portal for WiFi, then     │
│    the device's OWN http server on the LAN — the settings page (§4)     │
└─────────────────────────────────────────────────────────────────────────┘
   ▲ WebSerial (flash, once)   ▲ HTTP from a phone      ▲ HTTPS (OTA: phone
   static site (flash page,      on the home WiFi         checks, device
   settings UI, gallery)         to the device itself     fetches; §7)
```

Principles: **specs are data, the engine is firmware.** New content never requires a firmware change — packs install as JSON files. Firmware changes ship as OTA releases for engine/widget upgrades only.

## 2. The spec engine (firmware)

Per due page, the engine runs a fixed pipeline: **fetch → parse → extract → compute → hash-skip → bind → render** (source-level `when` and `min_refresh_min` gate the fetch stage; normative pipeline in PACK-SPEC §11.1).

### Fetch

- HTTPS client with a bundled CA store (curated roots covering data.gov.hk, HKO, GitHub, common CDNs + user-extendable via LittleFS) — no `setInsecure()` anywhere.
- Sources declared in the spec: URL (with `{{params.*}}` substitution), method, `format: json | rss | csv`. PACK-SPEC's `{{secrets.*}}` substitution and per-source `headers`/`body` are specified but not implemented — see the next bullet and §10 below.
- Secrets never reach a spec, categorically: `{{secrets.*}}` is refused in every position the engine parses it, unconditionally (`engine/src/text.cpp:568-570`) — not merely at an undeclared host, because no pack-facing construct can reference a secret at all. The only secrets the device holds are two system-level provider keys (STT, LLM), behind a closed two-name NVS allowlist (`stt_key`/`llm_key` — `firmware/src/yat_console.cpp:365`, `firmware/src/yat_portal.cpp:642-643`) that no pack-facing code path addresses. Even a future secret reference would have nowhere to travel: `FetchFn`, the one hook a declared source reaches, carries a source id and a URL and nothing else — no header or body parameter exists (`engine/include/yat/engine.h:134-136`). Pack-scoped secrets and `sent_to` domain enforcement are specified (PACK-SPEC §10, §12.3 rule 4) but not yet enforced on device — see the ROADMAP backlog.
- Sources resolving to a private, loopback, link-local, or shared/CGNAT address (PACK-SPEC §12.3 rule 2) are refused before the request is sent (`firmware/src/yat_net.cpp`'s `fetchPackSource`, error token `E_FETCH_BLOCKED`) — implemented for the on-device fetch path; the WASM/native-CLI targets do not fetch over a real network at all, so the guard has nothing to do there.

### Parse + extract

- **ArduinoJson with deserialization filters**: the filter document is derived from the spec's extract paths, so only the needed fields are parsed — large API responses (multi-hundred-KB stop lists) stay bounded in RAM. RSS/XML via a small streaming parser into a uniform item list; CSV likewise.
- **YAT path expressions** — a deliberately small, fully-specified subset of JMESPath implemented in C++ (full JMESPath has no mature C/C++ implementation; do not pretend otherwise): dot/quoted-member access, array index (negative OK), at most one `[?field=='literal']` filter with a per-element projection, and a small closed pipe-stage set. The normative grammar, canonical forms, and caps live in PACK-SPEC §6 — this summary is deliberately not an enumeration (it drifted once; it will not be maintained in parallel). If a pack needs more, the answer is a grammar RFC, not an escape to code.

### Hash-skip

Extracted-field hash compared to the last render's hash (RTC RAM + NVS). Unchanged → no render, no 30 s refresh, straight back to sleep. This remains the single biggest battery lever.

### Bind + render — the widget tree

The spec's `render` section is a **widget tree**, not HTML. The engine implements a fixed widget set drawing into the 4bpp framebuffer:

- **Layout:** `column`, `row` (flex-style weights, padding, gap), `spacer`, `divider`
- **Content:** `text` (size/weight/align, full CJK), `bignum` (display-size numerals), `list` (repeater bound to an extracted array, with a per-row template — iteration lives here, not in template logic), `icon` (built-in geometric set: weather glyphs, transit badges, UI marks — official HK warning-signal glyphs are firmware-reserved and deliberately absent from the pack catalog, PACK-SPEC §9.9/§12.3), `image` (PNG from URL → pngle streaming decode → existing FS dither), `qr`, `bar` (progress/meter)
- **Chrome:** standard header/footer injected by the engine (page name, data source, updated time, battery/stale glyphs) — packs get brand consistency for free; opt-out flag for takeover pages.

Text placeholders are `{{params.x}}` / `{{data.y}}` with a closed filter catalog (normative list in PACK-SPEC §8 — 13 filters at 0.3; not enumerated here to avoid drift). No loops, no conditionals in templates — conditionals exist as widget-level `when` visibility bindings (when-expression grammar in PACK-SPEC §6.5). Logic lives in extraction, `compute`, and binding; templates stay dumb.

Fonts: real proportional **Noto Sans / Noto Sans TC** bitmaps at the spec's sizes, generated into the engine itself (`engine/src/fonts_data_*.cpp`, ~1.36 MB of flash) and therefore shared byte-for-byte by the firmware, native and WASM targets. This replaced efont's 16×16 full-BMP table, which is no longer linked; the firmware's own screens go through the same `yat::fontGlyph`, so the cards and the packs share one set of faces and metrics.

### One engine, three targets

The engine (path evaluator, binder, widget renderer, ditherer) is written as a portable C++ core with thin platform shims, built for:
1. **Firmware** (the device),
2. **WASM** (the flasher site's *pixel-identical* in-browser preview),
3. **Native CLI** (`tools/preview` — render any spec to PNG on a laptop; contribution loop needs no hardware).

This is ambitious but load-bearing: it is what keeps previews honest and packs testable. Fallback if WASM proves painful early: CLI target first (native build is trivial), WASM by v1.0.

## 3. Scheduling & time

The device owns its clock: RTC holds time through deep sleep; NTP resync on wake (cheap). The scheduler evaluates, in firmware, the **active** page's cadence: the pack's `schedule.default.every_min`, tightened by any of its `windows` whose `from`/`to` and weekday mask match now (commute windows), overridden by a per-page `every_min` the settings page can set; then quiet hours (no wakes; the last page persists at zero power), then **auto-rotate** (`config.loop_min`, off/30/60/180) which caps the sleep so the next page is due on time. Wake sources: `esp_sleep_enable_timer_wakeup` + ext1 on KEY0/1/2 (GPIO3/4/5, RTC-capable, `ESP_EXT1_WAKEUP_ANY_LOW`). A failure ladder (5 → 15 → 60 min, honouring quiet hours from the second rung) replaces the cadence on any cycle that could not complete, and a takeover clamps every sleep to ≤15 min.

**Warning takeover** is a firmware behavior, not a pack: a built-in lightweight check of the HKO warning endpoint on every wake (tiny JSON); T3+/red/black rain preempts the playlist with the built-in takeover page and tightens the wake cadence.

## 4. Configuration & sync

- **On-device is the source of truth:** `config.json` + `packs/*.yat-pack.json` on LittleFS; WiFi + secrets in NVS. Schema zod-mirrored in the tooling, versioned `v` field, validated by firmware on load (bad config → keep last good, show status glyph).
- **Write paths:**
  1. **The device's own HTTP server (primary).** The green button held until it beeps puts the device into **setup mode**: WiFi provisioning first if the saved network will not come up, then an `ESP32 WebServer` on port 80, reachable on the home LAN (mDNS `yat-xxxx.local` plus the raw IP) *and* on the device's own AP at `192.168.4.1` — one socket bound to `0.0.0.0` serves both. It exposes `GET /api/{status,config,packs}`, `POST /api/{pack,apply,done,secret}` and `DELETE /api/pack`. The panel prints the address and a QR for it; a phone browser is the client. The window closes after 10 idle minutes (any `/api/*` hit resets it) or when the user taps Done, and the mode is refused below 3.50 V. Setup mode is the heaviest thing the firmware does, which is why it is deliberately entered, bounded, and self-closing.
  2. **The bootstrap/UI split.** The device serves ~1 KB of HTML that `import()`s the real interface from the static site (`YAT_PORTAL_UI_BASE`). The settings UI therefore improves without reflashing anybody's hardware, and the device carries no UI assets — at the cost of a compatibility obligation running from the site to every device in the field. An `http://` page fetching `https://` modules is allowed; the reverse (the site reaching into the device) is not, which is why the flow runs this way round. A phone with no internet gets the bootstrap's built-in fallback page, which still answers `/api/status` and lists pages.
  3. **WebSerial** — the line file protocol over USB (`YAT LS/GET/PUT/RM/STATUS/PAGES/USE/SAY/SECRET/PORTAL/REBOOT`), path-whitelisted to `/config.json` and `/packs/…`. Kept for development and rescue rather than as a user path: unframed `PUT`s with no flow control overflow the device's RX buffer when anything else touches the port. `YAT PORTAL` starts the portal above over the cable.
  4. **WiFiManager QR captive portal** — WiFi credentials only, on first boot, on the held-button gesture when the saved network fails, and after three consecutive connect failures. Improv-serial supplies the same credentials over USB from the flash page.
- **Not built:** BLE config mode (a GATT service mirroring the serial command layer) — dropped from v1 in favour of the HTTP portal, which reaches iOS, needs no pairing, and keeps the BLE stack out of the image. `sync_url` (hash-gated pull of config and packs from a static host) — deferred to v2.
- Battery/status telemetry has no server to land on. Three on-device surfaces replace it: the standard footer (battery + staleness), firmware-owned full-screen cards (offline, no data, low battery, setup refused, safe mode, help), and an **error ring in NVS** — the last eight failures with code, context and timestamp, surviving deep sleep, served by `GET /api/status` and `YAT ERRORS` and rendered as plain language in the settings page. E-ink holds its last image with the power off, so without this a fault is indistinguishable from a working device.

## 5. Voice (push-to-talk, all device-side except STT itself)

```
tap KEY0 (green) → ext1 wake → power mic (GPIO38), PDM capture
   ‖ parallel: WiFi connect started during the recording
→ fixed window (config voice.seconds, default 4, clamped 2..8)
→ POST WAV direct to STT API (key from NVS; ElevenLabs Scribe)
→ transcript → on-device normalize (OpenCC-derived s2t table, lowercase,
   strip punctuation) → alias match vs packs' aliases + per-page overrides
   (en / zh-Hant / jyutping)
→ miss AND an LLM key on file? one chat completion, shown the page list
   and the transcript, must answer with one configured page id (anything
   else, including NONE, reads as no match). DeepSeek, OpenRouter or
   NVIDIA — the device picks the endpoint off the key's own prefix
   ("sk-or-" OpenRouter, "nvapi-" NVIDIA, otherwise DeepSeek), so there is
   one paste field and no provider setting anywhere. config.json
   "llm_model" overrides the default model for whichever is active
→ matched page: render it now (engine) → refresh. No match → falling pair.
```

The mic is powered only for the capture window, and never at all without a key — the key check happens before the mic is enabled, so a keyless device does not record four seconds of a household for nothing.

**Tier 2 is fenced structurally, not by prompt discipline.** The reply is looked up in the configured pages by id, so a hallucinated id cannot become a page switch; untrusted strings (pack names, descriptions, aliases, the transcript itself) have control characters flattened before they enter the line-structured prompt, so a pack cannot smuggle extra page lines or a second "User said:" into it; and every failure path returns "no match" rather than an error, so a dead API degrades to keyword-only routing. It records an `llm_http` row in the error ring when the provider fails — from the household's side "it stopped understanding me" looks the same either way, and that row is what distinguishes a dead key from missing keywords.

Latency ≈ 2 s boot + 4 s record (WiFi overlapped) + 3–5 s STT + optionally ≤8 s routing + 2–4 s fetch/render + 25–30 s panel refresh ≈ **35–45 s button-to-visible**. Buzzer acks at record start/stop, match, no-match, and "matched but a takeover owns the panel". Voice is optional: no STT key → the arrows still cycle pages. v2: on-device Cantonese wake word (always-on mode for plugged-in devices); more STT/model providers.

## 6. Battery model (order-of-magnitude; measure in v0.1)

| Event | Cost |
|---|---|
| Full cycle: boot 1.5 s + WiFi 2–5 s + fetch/parse/render 3–6 s + refresh ~28 s | ~0.8–1.2 mAh |
| No-change wake (fetch + hash only, skip refresh) | ~0.15–0.25 mAh |
| Deep-sleep floor | S3 ≈ 15 µA; board quiescent **unmeasured — assume 50–200 µA → 1.2–5 mAh/day** |

Default schedule (hourly + commute windows, ~half of wakes skipped): **≈2 months**; conservative hourly-with-quiet-hours ≈ 3 months (matches vendor claim). Top hardware risk unchanged: unmeasured board sleep floor — v0.1 exit criterion.

## 7. OTA

**Status: built (v0.4), unexercised against a real release.** No version has
been tagged yet, so the live download-through-install path has never actually
run; every step below is implemented and covered by mock tests (33 of them, in
the website's test-portal.html, private codebase) rather than a device pulling
a genuine release.
The partition table was in place from the start regardless — changing it
later forces a USB reflash for everyone.

- **The phone checks; the device fetches.** The device never polls for
  updates, phones home, or reports its version anywhere on its own schedule —
  a shelf device quietly talking to a server we run is exactly what this
  product promises not to be. The website's device-portal.js module queries
  GitHub's public releases API from the phone, only while the settings page is
  open in front of somebody, and compares the answer against the device's own reported
  version. A newer release surfaces as a card: current version, new version, a
  link to the release notes, one **更新 Update** button.
- **The device is told a tag, never a URL.** `POST /api/update {tag}` is the
  whole request surface; `otaTagValid()` (`firmware/src/yat_ota.cpp`) rejects
  anything that is not exactly `^v[0-9]+\.[0-9]+\.[0-9]+$` before it is looked
  at twice. The device — not the phone — builds the release-asset URL from
  constants compiled into the firmware
  (`github.com/yat-hk/yat/releases/download/<tag>/firmware-<model>.bin`), so a
  LAN peer can pick *which* official release installs and never supply an
  arbitrary binary or host.
- **Both hosts in the redirect chain are certificate-pinned.** GitHub answers
  a release download with a redirect to its asset CDN; the redirect is
  followed by hand, not by the HTTP client, so the target host can be checked
  before it is trusted (must end in `.githubusercontent.com`), and both
  `github.com` and the asset CDN are verified against the bundle in
  `certs.h` (Sectigo Root E46 for github.com, ISRG Root X1 for the asset CDN)
  — no `setInsecure()` anywhere in this path.
- **Written to the inactive slot, verified before anything points at it.**
  `esp_ota_*` streams the download into whichever OTA partition is not
  currently running; `esp_ota_end()` re-reads what landed and runs
  `esp_image_verify()` — not a sha256 compared against a release manifest —
  before `esp_ota_set_boot_partition()` runs. An interrupted download (dropped
  WiFi, pulled power) leaves the running app completely untouched; the
  half-written bytes sit in the spare slot until the next attempt overwrites
  them.
- **Rollback confirms at the end of the first successful cycle, not at boot.**
  `CONFIG_APP_ROLLBACK_ENABLE` is on in the prebuilt pioarduino sdkconfig, so a
  freshly installed image boots `PENDING_VERIFY`, and a second reset while
  still pending reverts to the previous slot automatically. The app marks
  itself valid (`otaMarkAppValidIfPending()`, called from the same
  `lowLevelSleep()` funnel every cycle exits through) only once it has reached
  its first post-update sleep — "booted" is not the bar, "got all the way
  through a cycle" is, so an image that boots but crashes mid-render still
  rolls back on the next reset.
- **Known residual, reported rather than fixed:** the settings portal is
  unauthenticated on the LAN (a property of the whole portal, not of this
  route), so a LAN peer can direct the device to install an older, still
  genuine YAT release — a downgrade, never an arbitrary binary. Closing that
  needs anti-rollback efuses (one-way; would brick a device on any release
  ever pulled) or an authenticated portal, both bigger decisions than this
  feature.
- Partition table from first public release: `nvs / otadata / phy / ota_0 6 MB / ota_1 6 MB / littlefs ~19 MB` (6 MB fits app + engine; CJK fonts live in LittleFS, keeping OTA images small).
- GitHub Actions per-model build matrix on tag push — `firmware-<model>.bin` (OTA), merged full-flash image (flasher's esptool-js manifest), `firmware-<model>.uf2` (drag-drop fallback), plus per-model `manifest.json` with sha256s — has shipped since 2026-07-28. That manifest's sha256 is what the *web flasher* checks before a first-flash install; the on-device OTA path above verifies the image itself and never reads the manifest.
- First flash still needs a computer; every update after that is phone-only.
- Not built: an update check on an idle schedule (someone must open the
  settings page to be offered one) or an auto-install option.

## 8. Provisioning (web-flasher-first)

The computer appears once, for step 1. Steps 2 and 3 moved onto the device itself (§4); the site's setup page is instructions only and talks to nothing.

1. **Flash (computer, Chrome/Edge):** static site → model picker → **esptool-js over WebSerial, automatic bootloader entry** (DTR/RTS — the mechanism OpenDisplay's Toolbox proves on reTerminal E100x). Full-flash image at offset `0x0`, sha256 checked against the release manifest. Erase-then-flash for recovery. **UF2 drag-drop fallback** (double-tap reset → `XIAO-BOOT`, per Seeed's Firmware Hub) where WebSerial is unavailable. The page then polls for the device to come back up and hands off; nothing else on it is part of the normal path.
2. **WiFi (panel + phone):** green button held until the beep → WiFiManager AP `YAT-<last4 of MAC>`, QR-joinable, captive portal for the home 2.4 GHz network. Its HTTP/DNS pump runs on a core-0 FreeRTOS task, because a ~30 s blocking panel refresh on the loop task is exactly the window in which a freshly joined phone requests the captive-portal page. Bounded at 10 minutes, then sleep and retry the saved credentials on an escalating ladder (30 → 60 → 180 min, honouring quiet hours) rather than beaconing all night. Improv-serial over USB supplies the same credentials from the flash page for anyone who prefers a cable.
3. **Pages and settings (phone):** the same gesture, once WiFi is saved, opens the device's own settings server (§4). Packs, params, order, the STT and LLM keys, quiet hours, cadence, auto-rotate, beeps.
4. Device redraws → returns to its schedule. No accounts of any kind were created.

Improv-BLE (phone provisioning, as OpenDisplay does) is a v2 candidate. Web Bluetooth config is not — see §4.

## 9. Repository layout

```
The `firmware/lib/yat_*` split below was the plan; what shipped is one `src/main.cpp` holding everything device-specific (panel glue, HTTPS, voice, provisioning, the portal, the firmware-owned cards) against a portable `engine/` sitting at the repo root. Read the tree as intent, not as a directory listing.

```
yat/
  engine/                      # PORTABLE C++ core: path eval, binder, widget
                               #   renderer, dither, hash-skip, fonts, icons
                               #   (built for firmware / WASM / native CLI)
  firmware/                    # PlatformIO project
    src/main.cpp               # wake → schedule → engine → refresh → sleep,
                               #   plus voice, provisioning, the settings
                               #   server, and every firmware-owned card
    include/                   # CA bundle, embedded packs, portal bootstrap
    partitions_ota_32mb.csv
  packs/
    internal/                  # firmware-embedded packs (never user-installed)
    examples/                  # render-test.yat-pack.json (engine-conformance
                               #   pack). The 17 real-world packs live in the
                               #   sibling yat-hk/yat-packs repo (official/)
  skills/pack-developer/       # spec authoring + validation-matrix awareness +
                               #   preview tooling. The user-facing skill
                               #   (configure / diagnose) is not written yet
  tools/preview/               # native CLI build of yat_engine → spec → PNG
  docs/                        # SETUP.md, PACK-SPEC.md (grammar + widgets),
                               #   protocol notes, troubleshooting
  AGENTS.md  CLAUDE.md  BRAND.md  PRD.md  ARCHITECTURE.md  ROADMAP.md
```

Not shown above because it isn't part of this repository: the website
(https://yat.day), a separate, private codebase deployed independently. It
serves five pages — **FLASH** (model picker, esptool-js auto bootloader, UF2
fallback, liveness check, log viewer, Improv WiFi), **SET UP** (instructions
for the panel-and-phone half; connects to nothing), **GUIDE** (the same
journey, one photo per step), **GALLERY** (pack browser with real renders and
a live WASM render; browsing only) — plus the **device portal UI**
(device-portal.js and friends), served from the website's origin and imported
at run time by every flashed device (`firmware/include/portal_page.h`).

## 10. Pack spec (summary — normative reference is docs/PACK-SPEC.md)

**Design principle: the spec is written by AI agents, not humans.** Optimize for machine precision over ergonomics: no shorthand or sugar (one canonical way to express each thing), verbose-but-explicit widget trees, a published JSON Schema, and a validator (CLI + firmware) that returns exact, actionable error messages — agents converge fast on precise errors and flounder on vague ones. `docs/PACK-SPEC.md` is written as a formal reference for agent consumption (complete grammar, exhaustive examples, error catalog) and ships inside the developer skill. Human readability is welcome but never traded against unambiguity.

```jsonc
{
  "yat": 1,
  "id": "hko-weather",
  "name": { "en": "Weather", "zh-Hant": "天氣" },
  "aliases": { "en": ["weather"], "zh-Hant": ["天氣"], "jyutping": ["tin1 hei3"] },
  "params": { /* JSON Schema: district, lang… */ },
  "secrets": { "api_key": { "hint": "…", "sent_to": ["api.example.com"] } },
  "data": {
    "sources": [{
      "id": "current",
      "url": "https://data.weather.gov.hk/…?lang={{params.lang}}",
      "format": "json",
      "extract": { "temp": "temperature.data[?place=='{{params.district}}'].value | [0]" }
    }]
  },
  "render": {
    "widgets": [
      { "type": "column", "children": [
        { "type": "bignum", "value": "{{data.temp}}°" },
        { "type": "list", "bind": "data.etas", "row": [
          { "type": "text", "value": "{{item.route}} — {{item.mins}} min" } ] },
        { "type": "icon", "name": "rain", "when": "data.is_raining" }
      ]}
    ]
  },
  "schedule": { "default": { "every_min": 60 } }
}
```

Install = copy the JSON onto the device — `POST /api/pack` from its own settings page, or `YAT PUT` over serial. No reflash, no build, no deploy. Structural safety: declared URLs only, secrets by name with enforced `sent_to`, no executable code in packs.

**Grammar scope is validation-driven — see [docs/SPEC-VALIDATION.md](docs/SPEC-VALIDATION.md)**, a 20-use-case matrix simulated against the draft spec. Accepted into v1 from that exercise: `{{now}}` + date filters (`date_diff_days`, `weekday`, `date_fmt`) and `source.type: "inline"` (+ `pick_by_day`) for static/countdown pages. Queued v1.x (firmware OTA, spec-minor): `source.type: "mqtt"` (retained-read on wake — fits the sleep model, covers Home Assistant), `format: "ics"` (secret-URL calendars), `chart` widget (sparkline/bars), `match`/default container, `llm` transform. (Sequential source references were promoted into v1 at spec 0.3 — PACK-SPEC §5.7.) Explicitly out: OAuth flows (v2+), true push-to-sleeping-device (contradicts the power model), webpage screenshots (needs a browser somewhere). Spec v1 freezes only after every ✅ matrix row exists as a real spec file rendered by the engine.

**Native packs** (C++ in firmware) are the escape hatch of last resort — they are firmware contributions upstream (new widget types, new formats), not user-installable code. The widget set growing is how the platform absorbs needs the spec can't express.

**Community distribution:** a separate **community packs repo** (`yat-packs`) holds contributed spec files, validated in CI (schema check + engine render of a golden PNG per pack). The gallery on the website is built from this repo at deploy time — static all the way down. Contribution flow is PR-based, and the developer skill automates it: agent designs the spec → runs preview → opens the PR with the golden render attached.

### Hostile-pack threat model

"Packs are data, not code" removes arbitrary execution, but a malicious spec is still a real attack. Defenses, by vector:

1. **Secret exfiltration.** The spec describes a pack-scoped-secrets-with-`sent_to`-enforcement design (§10, §12.3 rule 4) that is **not implemented**. What ships instead is stricter by omission rather than by that design: **`{{secrets.*}}` is refused in every position the engine parses, unconditionally** (`engine/src/text.cpp:568-570`) — there is no legal grammar for a pack to reference *any* secret, pack-scoped or otherwise. The only secrets on the device are two system-level provider keys (STT, LLM) behind a closed two-name NVS allowlist that no pack-facing code path touches (`firmware/src/yat_console.cpp:365`, `firmware/src/yat_portal.cpp:642-643`). Even a hypothetical future secret reference would have no exfiltration channel to police: `FetchFn`, the only network hook a declared source reaches, carries a source id and a URL and nothing else — no header or body parameter exists (`engine/include/yat/engine.h:134-136`). A pack MAY still declare a `secrets` block (its shape is validated by the portal's `pack-sideload.js`), but the portal refuses to install it today rather than let the household rely on a reference that could not work (the website's device-portal.js sideload-consent flow, "the firmware cannot use a pack's key yet"). Pack-scoped secrets, `sent_to` enforcement, the per-secret consent UI, and gallery CI domain-family checks are all specified, not yet enforced on device — see the ROADMAP backlog.
2. **Substitution injection.** Template substitution is **single-pass and spec-literal-only**: `{{…}}` is resolved exclusively in strings that came from the spec file; fetched data is inserted as inert text and is *never* re-scanned for placeholders. No eval, no recursion.
3. **SSRF into the home LAN.** Sources resolving to a private, loopback, link-local, or shared/CGNAT address (`0.0.0.0/8`, `10/8`, `100.64/10`, `127/8`, `169.254/16`, `172.16/12`, `192.168/16` — PACK-SPEC §12.3 rule 2) are refused before the request is ever sent: `firmware/src/yat_net.cpp`'s `fetchPackSource()` resolves the source's host and range-checks the result, failing with `E_FETCH_BLOCKED` rather than connecting — implemented, not merely specified, as of this revision. There is no per-pack consent override today; the spec's MQTT/Home-Assistant carve-out is a v1.x source type that does not exist yet, so nothing currently needs one. Accepted residual, stated rather than hidden: the guard resolves the host once to decide, then pins the actual TLS connection to the address it just checked (SNI and certificate verification still run against the hostname, not the IP) — but a *later* fetch of the same source (next wake, a retry) re-resolves from scratch, so a very-short-TTL DNS answer could legitimately differ between wakes. Closing that fully would need a DNS-pinning proxy this device does not have, or a fixed host allowlist, neither of which fits a spec built around arbitrary public APIs.
4. **Resource abuse / battery DoS.** Engine caps, enforced not advisory: max sources per pack, per-source response byte cap, per-wake network time budget, minimum schedule cadence floor. Violations render the pack's slot as an error card rather than silently draining the battery.
5. **Parser exploitation (the real RCE surface).** All fetched bytes are hostile input to C++ parsers (ArduinoJson, RSS/CSV, pngle). Mitigations: hard size caps before parse, ArduinoJson filtered/bounded parsing, **fuzzing of the parser/extractor/renderer in CI** (native target makes this cheap), and OTA as the patch channel.
6. **Content spoofing.** The warning-takeover page and its signal iconography are **firmware-reserved** — no pack can render the takeover chrome or claim official-warning visual language. Gallery listing requires the standard footer (source attribution) enabled.
7. **Prompt injection via pack text.** Pack names/descriptions/aliases are untrusted strings aimed at *the user's agent* and *the gallery site*: the developer skill treats pack file contents as data (never as instructions), and the gallery HTML-escapes everything (static build, CSP, no inline script).
8. **PR supply chain.** `yat-packs` CI runs only the validator + renderer against PR content (specs are data — rendering them in CI is safe by construction); no PR-supplied code ever executes. Maintainer review gates merge; gallery "official" badge is repo-path-based, not author-claimed.

## 11. Top technical risks

1. **Engine complexity in C++** — path evaluator + binder + widget renderer is now the heart of the project and its largest engineering cost. Mitigations: deliberately tiny grammar, portable core with golden-file tests run on the native CLI target in CI, WASM preview keeping behavior visible.
1b. **Spec expressiveness** — if the grammar can't cover most real use cases, the platform caps out. Mitigation: the [SPEC-VALIDATION.md](docs/SPEC-VALIDATION.md) matrix (simulate before freeze; 80% pass bar), agent-first spec design (verbosity is free when agents write it), and a defined extension path (spec-minor additions via firmware OTA).
2. **Widget expressiveness vs brand aesthetic** — losing HTML/CSS means the widget kit + bitmap fonts must deliver BRAND.md's editorial look. Mitigation: invest in the font set (LittleFS has room) and the built-in icon/badge library; judge v0.3 output against BRAND.md explicitly.
3. **Board deep-sleep floor unmeasured** — battery story from months to weeks if bad. Measure v0.1.
4. **Device-side TLS to arbitrary APIs + GitHub OTA** — CA management, redirects, TLS handshake RAM. Mitigations: curated CA bundle + LittleFS extension, OTA built in v0.4 with certificate-pinned redirects and rollback (§7) — live end-to-end still unexercised pending the first tagged release — per-source failure = stale page with status glyph (never blank).
5. **API response sizes/format drift** (data.gov.hk, HKO) — ArduinoJson filters bound RAM; per-source byte caps; pack fixes ship as spec updates (no firmware release needed).
6. **Voice latency is physics** (~35–45 s to visible) — buzzer acks + honest framing; never promise instant.
