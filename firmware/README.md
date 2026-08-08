# YAT firmware — reTerminal E1002 / E1001

v0.2: one wake cycle, a config-driven pack loaded from LittleFS, then back to
deep sleep on a schedule the pack and `config.json` compute together. Builds
on the v0.1 walking skeleton, whose job was to make the v0.1 measurements
possible; v0.2's job is to make the device configurable without a reflash.
v0.3 removes the last compile-time secret: WiFi credentials move to NVS, and
a fresh or reset device provisions itself (see "WiFi provisioning" below)
instead of needing one baked in. It also makes the device listen: a KEY0 tap
records a few seconds of speech, transcribes it, and switches to the page
whose aliases it names (see "Tap-to-talk voice" below).

## Models

Two devices, one carrier board. Everything above the panel is the same
hardware — same ESP32-S3 module, same three buttons, same buzzer, PDM mic,
SHT4x and battery divider on the same pins — and the two Seeed_GFX setup
headers give `TFT_CS/DC/BUSY/RST` the same pin *numbers*; only the silkscreen
names in their comments differ. So the split is genuinely just the panel:

| | E1002 | E1001 |
|---|---|---|
| Panel | ED2208, Spectra 6 | UC8179, mono |
| Size | 800×480 | 800×480 |
| Colour | black, white, red, yellow, green, blue | black and white |
| Sprite | 4bpp (192 KB) | 1bpp (48 KB) |
| `BOARD_SCREEN_COMBO` | 521 | 520 |
| Engine profile | `yat::kProfileE1002` (Spectra6) | `yat::kProfileE1001` (Mono) |
| Release asset | `firmware-e1002.bin` | `firmware-e1001.bin` |

E1002 is the launch model and the default env. On the E1001 the engine clamps
every ink — role-named or spelled out — to black or white, and `src/yat_hw.h`'s
`INK` table clamps again underneath it so the firmware's own cards (which paint
`yat::Ink::Red`/`Blue` straight onto the canvas, never through a role) come out
as marks rather than invisibly white.

`src/yat_common.h` derives everything model-specific from the single
`YAT_MODEL_*` build flag — the wire name, the engine profile, the OTA asset
name, the refresh budget — and `src/yat_hw.h` fails the build if that flag and
`BOARD_SCREEN_COMBO` ever name different panels.

## Build & flash

```sh
pio run                                          # build the default env (e1002)
pio run -t upload                                # flash over USB
pio device monitor                               # serial log @ 115200

pio run -e e1001                                 # build the mono model
pio run -e e1001 -t upload                       # flash it
```

No `secrets.h` needed — a freshly flashed device has no WiFi credentials in
NVS, so it boots straight into provisioning (see below). `include/secrets.h`
is now only a *dev convenience*: `cp include/secrets.h.example include/secrets.h`
and fill in your WiFi if you'd rather skip the portal/QR dance while
iterating — on first boot with empty NVS it's seeded in once, then ignored
forever after (`include/secrets.h.example`'s header comment has the details).
It's still gitignored and ships with placeholder values so a fresh clone
compiles either way.

The board also exposes a debug UART on GPIO44/43 (115200); every log line goes to
both it and USB CDC, so the device can be watched while running on battery with
USB disconnected.

## What the firmware does

One pass through `setup()`, then deep sleep. `loop()` is only a safety net.

1. Print the wake reason — timer, ext1/KEY0/KEY1/KEY2, or power-on — plus
   battery (volts and a crude percent, see "Battery" below) and free heap and
   PSRAM. Below 8% the cycle stops right here — a firmware-owned low-battery
   card renders and the device sleeps 6 h with no WiFi and no fetch at all.
2. Mount LittleFS (label `littlefs`). First boot seeds `/config.json` (config
   v2, see below) and `/packs/hko-now.yat-pack.json` from the embedded copies
   (`include/hko_now_pack.h`); after that the filesystem is authoritative —
   the embedded copy is only a fallback (missing/corrupt FS, or every
   configured page's pack failing to load). Load `config.json`'s `pages[]` +
   `quiet_hours` + `voice`; invalid/missing config falls back to built-in defaults
   (one page, `hko-now`) without touching the file, so it stays inspectable
   over the serial protocol. A v1 file (`page`/single-page schema) is
   auto-migrated to v2 in place — see "Config v2 — pages" below.
3. Read the SHT4x over I2C (GPIO19/20, one-shot `0xFD`) and hand temperature and
   humidity to `epaper.setTemp/setHumi`. This runs early because the panel uses
   it for waveform compensation, not for display.
4. If this wake was a KEY0 *tap*, run the tap-to-talk voice flow (record →
   ElevenLabs Scribe → alias match → maybe switch the active page) before
   anything decides which pack to load, so a matched page is what this cycle's
   one panel refresh draws. See "Tap-to-talk voice" below. Any failure is an
   error tone and a log line; the cycle continues either way.
5. Connect WiFi (STA, 15 s timeout). Failure → draws the offline card (see
   "Offline / no-data card" below) if this is the first such cycle in a row,
   then deep sleeps 5 min and retries either way. The voice flow starts this
   connect early (during the recording) when it runs, so this step is
   usually already done by the time it is reached.
6. SNTP sync against `pool.ntp.org` with `TZ=HKT-8`, 5 s budget.
7. Fetch HKO's `warnsum` and decide whether a typhoon signal ≥ T3 or a
   red/black rainstorm warning is in force — see "Warning takeover" below.
   If so, this cycle loads the embedded `warning-takeover` pack instead of
   step 8 below.
8. Load the ACTIVE page's pack (`pages[page_idx].pack` + its `params`) from
   LittleFS into `yat::Engine`, wiring both of the engine's pluggable host
   interfaces before `fetchAndExtract()`/`render()` run: `LittleFsStateStore`
   (`yat::StateStore`) and `PngleImageDecoder` (`yat::ImageDecoder`) — see
   below. If the active page's pack is missing/unreadable/fails to parse, try
   the next page (wrapping, at most once all the way around) and promote
   whichever page's pack actually loads to active; if every page fails, fall
   back to the embedded `hko-now` pack outright. (Skipped entirely when step 7
   preempted it.)
9. Fetch its one HTTPS source via `WiFiClientSecure` + `HTTPClient`, pinned to
   the multi-root CA bundle (`YAT_CA_BUNDLE`) in `include/certs.h`. Redirects are off.
   There is no `setInsecure()` anywhere.
10. Extract. If every source failed and there was no stale snapshot to serve
    (see "Offline / no-data card" below), draw the offline card — on the
    first such cycle only — and sleep 5 min. Otherwise hash the extracted
    data: if `dataHash` equals the value stored in NVS (`Preferences`
    namespace `yat`, key `hash`) — and the warning-takeover state didn't just
    flip, and the device isn't coming back online after an offline streak —
    log `hash-skip` and sleep *without rendering*; skipping the ~30 s panel
    refresh is the single biggest battery lever in the design.
11. Otherwise render through `EPaperCanvas` (a `yat::Canvas` over the 4bpp
    sprite) and `EfontProvider` (efont full-BMP, 21696 glyphs) — after
    `eng.setBatteryPercent()` so the standard-chrome footer draws the battery
    glyph (see "Battery" below) — call `epaper.update()`, store the new hash,
    and sleep (clamped to ≤15 min while takeover is active). If any source
    served a stale §11.3 snapshot this cycle, log `[state] stale since
    HH:MM` right after rendering.

## Config v2 — pages

`/config.json` holds 1..16 pages instead of one:

```json
{
  "v": 2,
  "pages": [
    { "id": "weather", "pack": "hko-now", "params": {} },
    { "id": "news", "pack": "news-pack", "params": {}, "aliases": ["news", "新聞"] }
  ],
  "quiet_hours": ["23:30", "06:30"],
  "voice": { "seconds": 4 },
  "loop_min": 60
}
```

- `id` — a short identifier, unique per page (`YAT USE`, aliases, and log
  lines all refer to it). `pack` is the pack file's basename under `/packs/`
  (same as v1's `page.pack`). `params` is passed to `yat::Engine::load()`
  unchanged. `aliases` is an optional per-page override for voice matching
  (`YAT SAY`, below): if a page declares a non-empty `aliases` array here, it
  replaces the pack's own `aliases.en`/`aliases["zh-Hant"]`/`aliases.jyutping`
  (PACK-SPEC §2) entirely for that page; otherwise the pack file's three
  arrays are read and merged automatically.
- **v1 → v2 auto-migration.** A `config.json` still in the old
  `{"v":1,"page":{"pack":...,"params":...}}` shape is wrapped as `pages[0]`
  (`id` = the pack name) and written back as v2 on the very next boot that
  reads it — nothing to do by hand. `quiet_hours` carries over unchanged.
  A fresh/empty filesystem seeds v2 directly, with the embedded `hko-now`
  pack as `pages[0]` (`id: "weather"`).
- **Active page.** Which page renders is `Preferences` namespace `yat`, key
  `page_idx` — an index into `pages[]`, clamped into range at load (so
  shrinking the page list, e.g. via a `YAT PUT`/`RM`, can't leave it
  pointing past the end). KEY1/KEY2 (below), `YAT USE`/`YAT SAY` (below), a
  matched voice capture and `loop_min`'s own advance are the only things that
  change it. Every one of them goes through `savePageIdx()`, which also stamps
  NVS `loop_at` — the epoch the page now on the panel became active, which is
  where auto-rotate measures its interval from. So a page chosen by hand gets a
  full interval before the rotation moves off it.
- `loop_min` — **auto-rotate**: hold each page this many minutes, then step to
  the next one without anybody pressing a button. Absent or `0` is off; the only
  other accepted values are `30`, `60` and `180`, and anything else is snapped
  to a rung (upward within the ladder) with a log line rather than rejected.
  There is deliberately nothing shorter: every advance is a full 30 s refresh
  whether or not the data moved, paid in battery and in the panel's finite rated
  refresh count. See "Scheduler" below for how it meets a pack's own cadence and
  quiet hours; reported by `YAT STATUS` and `GET /api/status` as `loop_min`.
- `voice.seconds` — how long a KEY0 tap records, default 4, clamped to 2..8
  (out-of-range values are clamped with a log line, not rejected — this knob is
  not worth costing someone their pages over). Reported by `YAT STATUS` as
  `voice_seconds`. The whole object is optional; everything else about voice
  lives in NVS, not here.

## State persistence (§11.3 stale-serve / §5.1 min_refresh_min)

`LittleFsStateStore` (`src/main.cpp`) implements `yat::StateStore` over the
already-mounted LittleFS: one file per key under `/state/<packid>.<sourceid>`,
holding the engine's opaque `{"t":epoch,"fields":{...}}` blob verbatim. Keys
are schema-restricted to `[a-z0-9._-]` (PACK-SPEC §3/§5) so they're
filename-safe as-is, but `sanitize()` defensively substitutes anything outside
that set anyway — this store has no other way to reject a malformed key.
Wired via `eng.setStateStore(&store)` whenever LittleFS mounted successfully
this cycle (never wired if `g_fsReady` is false, so a mount failure degrades
to plain v0.2 behavior — every fetch failure yields fresh nulls — rather than
touching a filesystem that isn't there). With it wired, a fetch/parse failure
now serves the last-good snapshot (with the stale marker) instead of blanks,
and a source with `min_refresh_min` set stops refetching every wake within
that interval.

## Image decoding (§9.10 `image` widget)

`PngleImageDecoder` (`src/main.cpp`) implements `yat::ImageDecoder` using
[pngle](https://github.com/kikuchan/pngle) (`kikuchan98/pngle`, MIT — added to
`lib_deps`), a small streaming PNG decoder. It composites decoded pixels onto
a white background (the panel has no notion of transparency) and defensively
rejects an IHDR claiming more than 800×480 pixels *before* allocating the
output buffer — `engine/src/render.cpp` re-checks that same cap after
`decode()` returns, but a highly-compressible PNG can claim far larger
dimensions than its ≤204800-byte fetched size would suggest. Wired via
`eng.setImageDecoder(&decoder)` unconditionally (no FS dependency). The
decoded RGB buffer is a `std::vector<uint8_t>` sized up to 800×480×3 ≈ 1.15
MB — too large for the internal heap alongside everything else live during a
render. This board's `qio_opi` sdkconfig has `CONFIG_SPIRAM_USE_MALLOC=1` and
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (only allocations under 4 KB are
forced into internal RAM), so `malloc`/`new` — and therefore `std::vector` —
routes large allocations like this one to PSRAM automatically; no custom
allocator needed. Confirmed from the framework's compiled-in sdkconfig, not
yet from an on-device allocation trace (see checklist below).

At a few points during the cycle (after WiFi connect, after fetch, after
render) and always right before sleeping, the firmware opens a 10 s serial
console window (see "Serial file protocol" below).

Deep sleep wakes on a timer — computed by the on-device scheduler from the
ACTIVE page's pack `schedule` plus `config.json`'s `quiet_hours` (see below)
— or on KEY0/KEY1/KEY2 (GPIO3/4/5) via ext1. All three still force a render
(bypassing hash-skip). Per PRD §4.3:

| Button | Action |
|---|---|
| KEY0 (tap) | Force render (mic capture + STT wiring for voice page-switching arrives in a later task; the keyword matcher itself is already reachable over serial — see `YAT SAY` below) |
| KEY0 (hold ≥ 5 s) | Re-enter WiFi provisioning (QR portal) |
| KEY1 | Page forward: `page_idx + 1`, wrapping, + force render |
| KEY2 | Page backward: `page_idx - 1`, wrapping, + force render |

KEY1/KEY2 persist the new `page_idx` to NVS immediately, so it survives a
sleep even if the render that follows fails partway.

## Scheduler

Effective cadence = the minimum `every_min` among the pack's
`schedule.windows` whose day-of-week and `[from, to)` interval contain the
current local time, else `schedule.default.every_min` (PACK-SPEC §10).
`config.json`'s `quiet_hours` always overrides: inside quiet hours the device
sleeps straight through to the end; if the schedule's own cadence would
otherwise wake it up in the middle of quiet hours, that wake is skipped and
sleep is extended straight through instead. Every decision is logged as
`[sched] ...` right before `[sleep] ...`.

`loop_min` (auto-rotate) sits between the two. It can only pull a wake
**earlier** — a page refreshing every 5 min keeps refreshing every 5 min under a
30 min rotation, while a page refreshing once a day is interrupted by it — and
the rotation itself happens at the top of a timer-wake cycle, before the pack
load, so an advance costs that cycle's refresh rather than one of its own.
Three things stop it, each logged as `[loop] ...`:

- **quiet hours**: no overnight rotation at all. The panel holds its last image
  at zero power, so there is nothing to gain by turning pages at 3 a.m.
- **a warning takeover**: the playlist is preempted for as long as the signal
  stands, so rotating behind it would land the household on an arbitrary page
  the moment the warning clears.
- **a button, `YAT USE`, a voice match or a portal apply**: not a stop but a
  restart — see `loop_at` above.

Both suspensions resume at the first wake past them, which finds the interval
long overdue and advances immediately. A single-page device never rotates.
An advance also bypasses hash-skip: the stored hash describes the page that was
on the panel, and two pages can hash alike.

## Serial file protocol

A simple line protocol over `Serial`, active during the console windows
described above — the foundation the BLE/WebSerial toolbox (v0.5) will share
a command layer with:

| Command | Effect |
|---|---|
| `YAT LS` | List `/config.json` and `/packs/*` with byte counts |
| `YAT GET <path>` | Print `OK <bytes>` then the raw file content |
| `YAT PUT <path> <bytes>\n<raw bytes>` | Write a file (JSON-validated before write) |
| `YAT RM <path>` | Delete a file |
| `YAT STATUS` | JSON: fw version, model, battery mV, last hash, next wake in seconds, page count, active page index + id, voice readiness, voice capture length |
| `YAT PAGES` | One line per configured page — `<idx> <id> <pack>`, `*` prefixed on the active one — then `OK` |
| `YAT USE <id-or-index>` | Set the active page (by `id`, or by numeric index), persist to NVS, `OK <idx> <id>`; renders now if this cycle hasn't loaded a pack yet, otherwise next cycle. `ERR no such page` if it doesn't match either. |
| `YAT SAY <text>` | Run the whole on-device voice decision (`voiceDispatch()`, "Voice keyword matcher" and "Voice intents" below) against `<text>` — no microphone, and deliberately no LLM tier, so the answer is deterministic and costs nobody an API call. A page match switches the active page exactly like `YAT USE` (persist to NVS, force a render) and replies `MATCH <idx> <id>`. A todo intent mutates the list, persists `config.json`, makes the list the active page, and replies `TODO ADD <items> <open>` / `TODO DONE <item-idx> <items> <open>` / `TODO CLEAR <removed> <items> <open>`, where the trailing pair is the list's length and its open count *after* the change. Everything else — no page named, a verb that could not be carried out, an item two open entries could answer to — replies `NOMATCH`, matching what the device does audibly: one "didn't catch that" tone for every refusal, with the reason on the `LOGF` line beside it. |
| `YAT SECRET stt <value>` | Store the STT provider key in NVS (`value` of `-` clears it); replies `OK`. `ERR usage: SECRET stt <value-or-->` for any other secret name or an empty value. |
| `YAT TAKEOVER 0\|1\|2` | Debug override for the warning-takeover policy ("Warning takeover" below): `0` off (real `warnsum` check resumes), `1` simulate T8 (`TC8NE`), `2` simulate Black Rain (`WRAINB`). `ERR usage: TAKEOVER 0\|1\|2` otherwise. |
| `YAT REBOOT` | `ESP.restart()` |

`PUT`/`RM`/`GET` are guarded by a path whitelist (`/config.json`,
`/packs/*`) — the protocol cannot touch anything else on the filesystem.
`STATUS`'s JSON now also includes `"pages":N`, `"page":idx`, `"page_id":"<id>"`,
`"voice_ready":true|false` (`true` iff the `YAT SECRET stt` key is set),
`"voice_seconds":N` (the effective, already-clamped capture length),
`"loop_min":N` (the auto-rotate interval as the parser read it — `0`, `30`, `60`
or `180`, whatever the file literally says, so a device holding a hand-edited
`45` reports the `60` it will really rotate at),
`"takeover":true|false` and `"signal":"<code>"` (empty string when inactive;
a real signal code, or that code plus `" (SIMULATED)"` when forced by `YAT
TAKEOVER` — see "Warning takeover" below), and `"model":"e1002"|"e1001"` (this
build's `YAT_MODEL_NAME` — see "Models" above; `GET /api/status` reports the
same field, and it is what tells a flasher or the setup site which panel it is
talking to before it picks a release asset or previews a pack).

## Voice keyword matcher (PACK-SPEC §2 `aliases`)

`YAT SAY <text>` (above) is the text half of voice page-switching, and the
tap-to-talk flow below is the audio half: both end in the same
`matchPageByText()` call, so any phrase can be tested over serial without
speaking to the device.

**Alias cache.** Built once per boot (`buildPageAliasCache()`, right after
`config.json`'s `pages[]` loads), one entry per configured page: a page's own
`aliases` array in `config.json` wins if non-empty, else its pack file's
`aliases.en` + `aliases["zh-Hant"]` + `aliases.jyutping` (PACK-SPEC §2) are
read from LittleFS and merged. A pack file that's missing, unreadable, or not
valid JSON just means that page has no aliases this boot (logged once, not
once per `YAT SAY`). Cap: 24 normalized aliases per page.

**Normalizer** (`voice::normalizeUtf8`): ASCII lowercased; ASCII punctuation,
all spaces, and common fullwidth/CJK punctuation (`，。！？、：；「」『』（）`)
stripped; every other codepoint passed through the same simplified →
traditional table `engine/src/text.cpp`'s `s2t` filter uses
(`engine/third_party/s2t_table.h`, reused here via the same
include-directly-from-third_party/ pattern already used for `qrcodegen.h`,
since text.cpp's own lookup helpers are file-local). Applied identically to
every cached alias (once, at cache-build time) and to the live text on every
`YAT SAY` call — so a simplified-Chinese transcript still matches a pack's
`zh-Hant` (traditional) alias.

**Matcher** (`matchPageByText`): normalize the input; a page is a candidate if
any of its normalized aliases is a substring of the normalized input (this
direction only — "切去天氣嗰頁" containing alias "天氣" hits; aliases are
never matched as a superstring of the input). Best candidate = longest
matching alias, counted in codepoints, not bytes (so "天氣" and "weather"
compare as 2 and 7, not 6 and 7); ties broken by lowest page index. Empty
normalized input, or no candidate at all, returns no match (`YAT SAY`'s
`NOMATCH`).

Three worked examples (see `yat-packs/official/sushiro-queue.yat-pack.json`
in the sibling repo for the `sushiro` alias):

| Input | Alias | Normalizes to | Result |
|---|---|---|---|
| `天氣` | `天氣` (zh-Hant) | both -> `天氣` (unchanged: no ASCII/CJK punctuation, no codepoint has a simplified form) | substring match at offset 0 -> **MATCH** |
| `check sushiro queue` | `sushiro` (en) | input -> `checksushiroqueue` (spaces stripped, already lowercase); alias -> `sushiro` (unchanged) | `sushiro` is a substring of `checksushiroqueue` -> **MATCH** |
| `简体输入天气` (simplified) | `天氣` (zh-Hant) | input -> `簡體輸入天氣` (each simplified codepoint — 简/体/输/气 — mapped to its traditional form via `s2tLookup`; 入/天 pass through unchanged, already identical in both scripts); alias -> `天氣` (unchanged) | alias `天氣` is a substring of the *converted* input's last two characters -> **MATCH** (this is the case the s2t step exists for: the pack's alias is always zh-Hant, but a live transcript may come back simplified) |

## Voice intents: the household todo list (PRD §4.4)

Everything above answers *which page*. This is the only thing voice can **do**,
and it exists only on a device where some configured page uses the pack id
`todo` (`TODO_PACK_ID`). With no such page `todoPageIndex()` is `-1`,
`voiceDispatch()` never builds a command, and every transcript takes byte-for-
byte the path it took before this existed.

**Grammar** (tier 1, no key needed). A verb at the very front of the
*normalized* transcript — same normalizer as the alias matcher, so 「记住」
arrives as 「記住」 — with the payload sliced out of the **original** text via
`voice::normalizeWithSourceMap()`, so it keeps the spaces normalization strips
(`add buy milk` stores `buy milk`, not `buymilk`).

| Verbs | Payload | Refuses (no-match tone, nothing changes) |
|---|---|---|
| `加` · `記住` · `add` | the item, trimmed and clipped to 48 **characters** | empty text; the list already at 20 (`TODO_MAX_ITEMS` — PACK-SPEC §3.2/§12.1 caps array params at 20, so voice refuses at the number the settings page also refuses at); an item with that exact text already on it (done or open); an utterance that also matches a page alias — see below |
| `搞掂` · `完成` · `done` | a phrase, fuzzy-matched against the **open** items only, substring in either direction | zero candidates; **two or more** candidates; an item already done |
| `清理` · `清咗` · `clear` | none | nothing on the list is marked done |

Two rules are load-bearing rather than stylistic. **DONE never guesses** —
zero or two candidates get the same "didn't catch that" tone as an
unrecognized page, because the failure that matters here is not a missed
command but the *wrong* item ticked off, and a household that has to check the
panel after every 「搞掂」 has lost the feature even when it works. **Voice never
deletes an open item** — `清理` removes done items and nothing else; there is
no branch in `todoApply()` that erases an open one.

**Ordering** (`voiceDispatch()`). `matchPageByText()` runs first and
unconditionally, which is what makes the dormancy claim checkable rather than
asserted. An ADD whose utterance *also* names a page loses to the page — `加`
is one character and starts plenty of ordinary words (`加拿大`), and a page that
stopped answering to its own name is a worse failure than a stray list entry.
DONE and CLEAR are not second-guessed that way. A verb that was heard but could
not be carried out is **not** retried as a page switch: the household asked for
something to happen to their list, and changing the page instead would be a
different action wearing the same beep.

**Mutation** (`savePageParams()` in `yat_config.cpp`). A read-modify-write of
the *parsed* `/config.json`: locate the page by id, replace its `params`,
re-serialize, write through the same `littlefsWriteFile()` (temp file, size
check, atomic rename) the portal's Apply uses, then mirror the new params into
`g_pages[i].paramsJson` — only after the bytes are committed — so this cycle's
refresh draws them. Deliberately not a regeneration from `DeviceConfig`, which
only knows the keys this firmware parses and would silently drop anything a
newer portal or a hand edit put in the file. The serialized config is checked
against `CONTENT_PORTAL_MAX_BODY` before the write: a full list is ~3.5 KB of a
64 KB ceiling, and the check is there because this is the one writer that grows
the file with nobody watching.

**Malformed params** — an `items` that is not an array, an entry with no string
`text`, params that are not an object — switch intents off for the boot, logged
once, never fatal. The page still renders however the engine decides, page
switching is unaffected, and `todoOpenItems()` reports nothing to the LLM tier.

**Tier 2** replaces the router's single call rather than adding one. With the
todo pack installed, that same completion carries the page list *and* the
numbered open items, and must answer with exactly one of `PAGE:<id>`,
`TODO:ADD:<text>`, `TODO:DONE:<n>`, `TODO:CLEAR`, `NONE`. Done is answered **by
number** — the model never re-types an item's text, so the near-miss class
cannot occur on this tier; the number indexes the very vector that built the
prompt, and one outside it is refused rather than clamped. `max_tokens` is 160
here rather than the page-only 8: `TODO:ADD:` is ~5 tokens and the text after
it can be 48 characters, which a byte-level BPE can charge up to 3 tokens each
for. An answer that hit the cap (`finish_reason == "length"`) is refused
outright rather than stored truncated. Everything that fails validation returns
`None`, which lands in the same no-match branch a device with no key has always
used.

## Tap-to-talk voice (PRD §4.4)

Tap KEY0, wait for the beep, say a page keyword in Cantonese, English or
Mandarin. The page you asked for is on screen about half a minute later —
**the e-ink panel takes ~30 s to refresh, which is panel physics, not
software.** The beeps are the fast acknowledgment; the framing is *"ask, put it
down, it'll be there."*

```
[tap KEY0]  ──▶  beep (high)         speak now — recording, voice.seconds long
                 beep (lower)        recording done, working on it
                 beep-beep (rising)  matched → switching page
                 or  bip-bip (mid)   no STT key set yet — nothing configured, not a fault
                 or  bzzz (low)      no match / mic or network problem / STT error
~30 s later      the page is on screen
```

| Cue | Notes (`beep()` on the GPIO45 buzzer) |
|---|---|
| High chirp, 1319 Hz 80 ms | Start of the capture window. The LED also comes on and stays on for the whole capture. |
| Lower chirp, 880 Hz 80 ms | Capture finished, mic powered off. Upload and transcription happen after this. |
| Rising double, 1047 + 1319 Hz | Something was recognised and has already been done: a page alias matched and the active page is switched and persisted, or a todo intent ("Voice intents" above) changed the list and `config.json` is already written. Both then render this cycle, which is the real confirmation. |
| Soft double, 659 Hz 60+60 ms | No STT key configured yet (`beepNoKey()`, docs/UX-NONTECH.md §8 fix 6) — not a fault, just nothing set up. The only tone that plays without recording anything: it's checked before the mic is ever powered. |
| Low buzz, 330 Hz 300 ms | Every real failure, deliberately one sound for all of them: mic/I2S failure, WiFi didn't come up, STT error, or nothing matched. The log line says which; the screen is left alone. |

The buzzer is real hardware on **GPIO45**, driven by plain Arduino `tone()`
(LEDC underneath) — the same helper `e1002-test/src/selftest` uses for its boot
melody. No LED-only fallback was needed.

**Flow** (`runVoiceCapture()` in `src/main.cpp`, called from `setup()` right
after the provisioning decision and before any pack is loaded):

1. **STT key first.** Read `stt_key` from NVS. Empty → the soft neutral
   double-chirp (`beepNoKey()`), *not* the error tone — a fresh device with
   no key configured yet is not broken, and KEY0 is the most prominent
   button on it (docs/UX-NONTECH.md §8 fix 6) — then log `[voice] no STT key
   (YAT SECRET stt <key>)` and fall straight back to the normal cycle.
   Checked before the mic is powered, so a device with no key never records
   anything at all.
2. **WiFi, non-blocking.** `WiFi.begin()` is kicked off *before* recording, so
   association overlaps the capture instead of following it.
3. **Record.** Mic power (GPIO38) HIGH, PDM RX at 16 kHz mono 16-bit on
   GPIO42/41, first 100 ms dropped while the PDM front end settles, then
   `voice.seconds` of audio straight into a PSRAM buffer, then mic power LOW.
   The log prints the byte count and a peak-to-peak level — a flat line there
   means the mic produced nothing even though the reads succeeded.
4. **Wait for WiFi**, bounded so the whole connect still fits the same 15 s
   budget the normal path uses, measured from `begin()`. Not up in time → error
   tone, the clip is dropped, and the normal cycle's own connect (and its retry
   backoff) takes over.
5. **Transcribe.** One multipart `POST` to
   `https://api.elevenlabs.io/v1/speech-to-text` with `model_id=scribe_v2` and
   the key in an `xi-api-key` header, over `WiFiClientSecure` pinned to
   `YAT_CA_BUNDLE` (`certs.h` anchor 7 is the GTS WR3 intermediate that host
   chains through — no `setInsecure()` here either), 20 s response budget. The
   response is parsed with an ArduinoJson *filter*, so only `text` and
   `language_code` are ever materialized out of Scribe's word-level output.
6. **Match and switch.** The transcript goes through the same
   `matchPageByText()` the `YAT SAY` command uses — which normalizes and
   simplified→traditional-converts both sides, so no separate conversion of the
   transcript is needed. A hit persists the new `page_idx` to NVS, sets the
   force-render flag, and double-beeps; a miss beeps low. Either way the log
   carries the transcript and the verdict:
   `[voice] heard "睇天氣" (yue) -> MATCH weather (page 0)`.
7. **Fall through** into the normal cycle, which loads and renders whatever
   page is now active. A KEY0 wake is a button wake, so the hash-skip can never
   swallow that render.

**Setting the key.** It lives in NVS (`Preferences` namespace `yat`, key
`stt_key`), never in the firmware image and never on the filesystem:

```sh
python3 ../tools/serial/yat.py secret stt sk_your_elevenlabs_key   # set
python3 ../tools/serial/yat.py secret stt -                        # clear
python3 ../tools/serial/yat.py status                              # "voice_ready":true
```

Get the key from elevenlabs.io → your profile → API keys. Without one the
device is not broken, just quieter: KEY1/KEY2 still cycle pages by hand and a
KEY0 tap answers with the soft neutral double-chirp above, not the error
tone.

**Privacy.** Nothing is listening between taps — no wake word, no always-on
mic, and the mic's power rail is only energized inside step 3 above. Audio
leaves the device exactly once per button tap, to ElevenLabs and nowhere else,
and is never written to the filesystem: it lives in one PSRAM buffer that is
freed on every exit path. The transcript is used to pick a page and is only
printed to the debug log.

**Memory.** One `ps_malloc` per capture, laid out as the multipart body itself
(`[head][WAV header][PCM][tail]`) so the audio is recorded in place and never
copied into a second buffer: ~128.3 KB at the 4 s default, ~256.3 KB at the 8 s
maximum, out of ~8 MB of PSRAM. It is `free()`d on all five exit paths (capture
failure frees inside `voiceCaptureBody()`; WiFi timeout, STT failure, no match
and match all free in `runVoiceCapture()`). The 100 ms discard buffer is 320
bytes of stack rather than the reference demo's 3.2 KB static, so cycles that
never record pay nothing for it.

## Warning takeover (PRD §4.2/§5.1, docs/UX-FLOWS.md G15)

The product's headline feature: every wake, right after WiFi connects (and
before any page's own pack loads), the firmware fetches HKO's `warnsum`
endpoint directly (`fetchWarnsumDecision()` in `src/main.cpp`, reusing the
same `httpsFetch()` every pack source goes through) and decides whether to
preempt the whole playlist with a full-screen warning page.

**Policy** — active iff either condition holds (checked against the real API
shape: a `"WTCSGNL"`/`"WRAIN"` object with a `"code"` field):

| Condition | Codes that activate | Codes that do NOT activate |
|---|---|---|
| Typhoon signal ≥ T3 | `TC3`, `TC8NE`/`TC8SE`/`TC8SW`/`TC8NW`, `TC9`, `TC10` | `TC1`, `CANCEL`, or `WTCSGNL` absent |
| Rainstorm warning | `WRAINR` (red), `WRAINB` (black) | `WRAINA` (amber), or `WRAIN` absent |

Once active, this cycle loads the embedded `warning-takeover` pack (below)
**instead of** the configured active page's pack — the normal per-page
LittleFS load loop is skipped entirely, the same way the embedded `hko-now`
fallback bypasses it when every configured page fails. Hash-skip is bypassed
whenever the active/inactive flag **flips** this wake (activation and
deactivation both force an immediate render — the household should never
wait a cadence tick to find out the signal changed); while takeover simply
*persists* across wakes with an unchanged signal, the takeover pack's own
data hash still governs normally, so an unchanged T8 does not redraw the
panel every 15 minutes for nothing. Sleep is then clamped to at most 15
minutes (`clampForTakeover()`), overriding both the loaded pack's own
`schedule` and `config.json`'s `quiet_hours` — reaction time is safety-
critical and takes priority over both. Last known state (`active`, `signal`)
persists in NVS (`Preferences` namespace `yat`, keys `takeover`/
`takeover_sig`) so a reboot mid-typhoon does not lose it, and a `warnsum`
fetch/parse failure logs and keeps that persisted state rather than guessing
— it never blocks the cycle, and it never silently drops an active takeover
back to the normal playlist over one bad fetch.

**The internal pack.** `packs/internal/warning-takeover.yat-pack.json` is an
ordinary pack from the engine's point of view — schema-valid, rendered by
`yat::Engine` through standard chrome, previewable with `yat-preview` exactly
like any real-world pack (those now live in the `yat-hk/yat-packs` sibling
repo). What makes it different is that it is
**firmware-internal**: compiled straight into the image
(`include/warning_takeover_pack.h`, generated the same way
`hko_now_pack.h` is from `hko-now.yat-pack.json`, now sourced from
`../yat-packs/official/hko-now.yat-pack.json`), never written to
LittleFS, never listed in a pack gallery, and never user-installable — the
firmware loads its embedded C string directly (see the pack-load branch in
`setup()`) rather than reading it from `/packs/`. This is also why it is
exempt from the pack-spec rule that reserves warning-signal iconography and
"warning takeover chrome" to firmware (`schema/yat-pack.schema.json`'s `icon`
catalog comment, PACK-SPEC §9.1/§9.9): it doesn't need an exemption from
itself — it *is* the firmware's own warning display, just written as a pack
so it can be authored and previewed the same way every other page is. Its own
`data.sources[0]` fetches `warnsum` again, independently of the policy check
above (same idempotent ~1-2 KB GET, capped at 8 KB) — the policy check only
ever decides *whether* to load it.

The pack renders all nine real signal states correctly (T1, T3, T8 any
direction, T9, T10, amber/red/black rain) plus a calm "no warning in force"
fallback, using stacked `when`-guarded sections (PACK-SPEC §6.5) so one file
covers every case; firmware's activation policy above is simply more
conservative than what the pack can display (T1 and amber rain render fine
if ever loaded, e.g. from the native preview, they just never trigger a
takeover on-device). Preview both checked-in fixtures:

```sh
cd tools/preview && make -s
./yat-preview ../../packs/internal/warning-takeover.yat-pack.json \
  --doc warnsum=fixtures/warning-takeover.warnsum.json --out /tmp/wt-none.png    # real "no warning" response
./yat-preview ../../packs/internal/warning-takeover.yat-pack.json \
  --doc warnsum=fixtures/warning-takeover.warnsum-t8.json --out /tmp/wt-t8.png  # hand-authored T8NE
```

`warning-takeover.warnsum.json` is a real, unedited capture of
`https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=warnsum&lang=tc`
(genuinely `{}` — no warnings were in force when it was captured);
`warning-takeover.warnsum-t8.json` is hand-authored from the same real shape
(HKO Open Data API Documentation §"Weather Warning Summary") with `WTCSGNL`
edited to a `TC8NE` signal.

**Regenerating the embedded header** after editing the source pack — rewrite
`include/warning_takeover_pack.h` in full (same raw-string-literal shape as
`hko_now_pack.h`; there is no checked-in generator script for either).
`hko_now_pack.h`'s source pack now lives in the sibling `yat-packs` repo
(`../yat-packs/official/hko-now.yat-pack.json`) — the command below is the
warning-takeover shape; swap the path and the two generated-C-string names
(`WARNING_TAKEOVER_PACK` → `HKO_NOW_PACK`, output file → `hko_now_pack.h`) to
regenerate that one instead:

```sh
python3 - <<'PY'
pack = open('packs/internal/warning-takeover.yat-pack.json').read().rstrip('\n')
header = '''#pragma once
// GENERATED from packs/internal/warning-takeover.yat-pack.json -- do not hand-edit.
// Regenerate with the command in firmware/README.md's "Warning takeover" section.
//
// Firmware-internal pack (never on LittleFS, never user-installed) for the
// warning-takeover feature (PRD §4.2/§5.1, docs/UX-FLOWS.md G15) -- see
// firmware/README.md "Warning takeover" for the policy and design rationale.

static const char WARNING_TAKEOVER_PACK[] =
    R"yat(
''' + pack + ')yat";\n'
open('firmware/include/warning_takeover_pack.h', 'w').write(header)
PY
```

**On-device testing without a real signal.** `YAT TAKEOVER 0|1|2` (serial
protocol, below) forces the activation decision for testing on real
hardware: `0` clears the override (back to the real `warnsum` check next
wake), `1` simulates a T8 typhoon signal (`TC8NE`), `2` simulates a Black
Rainstorm Warning (`WRAINB`). The override persists in NVS
(`takeover_sim`) so it survives the sleep between setting it and the next
wake picking it up, and every simulated cycle logs `[takeover] SIMULATED
...` and reports the signal as e.g. `"TC8NE (SIMULATED)"` in `YAT STATUS` —
a test run can never be mistaken for a real signal in the log or over the
wire.

## Offline / no-data card (docs/UX-FLOWS.md G5/G12, docs/UX-NONTECH.md §8 fix 7)

Two cycle-ending conditions used to sleep blind, leaving the panel showing
whatever it last held with no indication anything was wrong:

- **The network never came up** — `wifiConnect()` fails in `setup()`.
- **Every source failed and there was no stale snapshot to serve** —
  `eng.fetchAndExtract()` still returns `true` (this isn't a pack/grammar
  bug), but `err` is non-empty, `eng.anyStale()` is false, and every field in
  `eng.data()` is null (`engineDataAllNull()`) — nothing came back, and
  there's nothing old to fall back on either.

Both draw the same firmware-owned bilingual card (`drawOfflineCard()`),
reusing the provisioning screen's `prov::` drawing helpers (`drawText`,
`drawQR`) plus a new `prov::drawIcon` for the §9.9 icon catalog, so it looks
like the rest of the device's own chrome rather than a blank or garbled
panel: `上唔到網 / Can't reach the internet`, the `wifi` catalog icon, then
`檢查下屋企 WiFi。如果搬咗屋或者換咗路由器：撳住最頂粒掣 5 秒，就可以重新設定
/ Check your home WiFi. Moved house or new router? Hold the TOP button 5
seconds to set up again.`

**Deliberately excluded: stale-serve.** A cycle where WiFi is up, a source's
fetch fails, but a prior snapshot exists (`eng.anyStale()` true) is *not*
offline by this definition — the page renders normally with the existing
`stale HH:MM` footer marker (docs/UX-FLOWS.md G18), the correct, calmer
signal for "old but real data". The offline card must never replace that.

**State machine** (`trackOfflineCycle()`, NVS `Preferences` namespace `yat`,
key `offline_n`) — the same first-cycle-only pattern `g_takeoverChanged` uses
for the warning-takeover page above:

- **First offline cycle** (`offline_n` 0 → 1): draw the card, spend the
  ~30 s refresh, sleep 5 min.
- **Every later cycle in the same streak**: suppress the redraw entirely —
  `offline_n` keeps incrementing, but nothing is drawn — and just sleep
  5 min. The card is already on screen; there's nothing new to say.
- **Recovery** (`offline_n` → 0): the normal page's hash-skip check is
  bypassed for this one cycle (`offlineRecovered`), so the real page replaces
  the card immediately instead of waiting for the data to happen to change.

## Battery (docs/UX-FLOWS.md G25/G26, docs/UX-NONTECH.md §8 fix 7)

`batteryVolts()` (on-board 1:2 divider over `PIN_BAT_ADC`, gated by
`PIN_BAT_EN`) is read once per wake and mapped to a percent with a straight
line, `batteryPercent()`: 3.3 V → 0%, 4.2 V → 100%, clamped to 0..100. This is
an honest, crude estimate for a single-cell Li-ion/LiPo with no fuel-gauge
chip — not a real discharge-curve model — good enough for a footer glyph and
a cutoff, not for a precise mAh estimate.

- **Every wake**, `eng.setBatteryPercent(pct)` is called before `eng.render()`
  so the standard-chrome footer draws the battery glyph
  (`engine/src/render.cpp` + `engine/src/icons.cpp`'s `battery` icon,
  previously wired on the engine side — G25/§11.4 — but never actually
  called from firmware, so it drew nothing on a real device until now).
- **Below 8%** (`LOW_BATTERY_PCT`), the cycle stops immediately — before
  `buildApCreds()`, the KEY0 gesture read, LittleFS, WiFi, or any pack load —
  so nothing that would finish the battery off gets a chance to run.
  `drawLowBatteryCard()` renders a firmware-owned bilingual card (`低電量 /
  Low battery`, the `battery` catalog icon and the percent in red, `請充電 /
  Please charge`), then the device sleeps 6 h (`LOW_BATTERY_SLEEP_MIN`) with
  no WiFi and no fetch. Buttons still physically wake the device early (deep
  sleep always re-arms ext1) — a press just re-checks the battery and
  redraws rather than doing anything else, so an intentional KEY0-hold
  doesn't get to burn the last of the battery in AP mode either.

## WiFi provisioning (ARCHITECTURE §8, PRD §4.1/§4.3)

Credentials live in `Preferences` namespace `yat` (keys `wifi_ssid`/
`wifi_pass`), the same namespace `hash` and the epoch fallback already use.
`setup()` checks, in order, before ever attempting to connect:

| Trigger | Detection |
|---|---|
| First boot / no credentials | `wifi_ssid` empty in NVS |
| KEY0 held ≥ 5 s | this wake's cause is ext1 + KEY0, and KEY0 is still low 5 s later (`readKey0GestureAtWake()`). The same sampling loop is what tells a hold from a tap: released before 5 s is a *tap*, which runs the voice flow above instead — one poll, two gestures, no double-reading of the pin. |
| 3 consecutive connect failures | NVS counter `wifi_fails`, reset to 0 on any successful connect or newly saved credentials |

Any of the three drops into `runProvisioningMode()`, which brings up two
independent ways to supply credentials — whichever finishes first wins, then
the device reboots into the normal cycle:

- **WiFiManager captive portal.** AP `YAT-<last4 of MAC>`, password an 8-char
  string derived from the eFuse MAC (stable across reboots, so a printed
  sticker stays valid). The panel shows a bilingual (zh-Hant/EN) page with two
  QR codes — one to join the AP (`WIFI:T:WPA;S:...;P:...;;`), one for
  `http://192.168.4.1` — drawn directly with `qrcodegen` + efont onto the
  `EPaperCanvas`, no pack involved. The portal's HTTP/DNS pump
  (`WiFiManager::process()`) runs on a FreeRTOS task pinned to core 0, ported
  from the e1002-test prototype (private)'s `src/wifidemo/main.cpp`: an e-paper
  refresh blocks the main task for ~30 s, which is exactly the window in
  which a freshly joined phone requests the captive portal page.
- **Improv-serial** (improv-wifi.com), for the future web flasher site over
  WebSerial — no AP dance at all. Implemented with
  `jnthas/Improv-WiFi-Library`; `setCustomConnectWiFi()` routes a received
  `WIFI_SETTINGS` command through the same NVS save as the portal path.
  Active over `Serial` whenever the device is awake and listening — every
  normal cycle's console windows, not just during provisioning — alongside
  the YAT file protocol above; `pumpConsoleByte()` demuxes the two by peeking
  one byte (Improv packets start with the literal `I` of the `IMPROV` magic,
  which a `YAT ` line never does).

## OTA update (ARCHITECTURE §7, `firmware/src/yat_ota.cpp`)

Firmware update over the household's own WiFi, driven entirely from the
portal's `loop()` — no second task, no callback, just `otaPump()` called on
every turn the same way `server.handleClient()` is.

- **State machine** (`OtaState` in `yat_ota.h`): `Idle → Drawing → Downloading
  → Verifying → Ok`, or `Error` from any working state. `otaArm(tag)`
  validates the tag (`^v[0-9]+\.[0-9]+\.[0-9]+$`, checked by hand — no regex
  engine on this target) and requires WiFi to already be connected before
  moving off `Idle`. The panel gets the "updating" card in `Drawing`, and the
  portal calls `otaCardDrawn()` once it has actually drawn it — that call is
  what advances the state to `Downloading` and lets `otaPump()` start
  fetching.
- **The URL is built on-device, never accepted from the request.**
  `openAssetStream()` composes
  `https://github.com/yat-hk/yat/releases/download/<tag>/firmware-<model>.bin`
  from constants in the file, so a request can only choose *which* tagged
  release, never a host or path. `<model>` is this build's own
  `YAT_MODEL_NAME`, so an E1001 can never be handed the E1002 image. GitHub's one redirect is followed by hand
  (`splitRedirect()`), rejecting anything carrying userinfo (`@`), a port, or a
  host not ending in `.githubusercontent.com` before a second TLS session is
  opened against it.
- **`esp_ota_*` directly, not `Update.h` or `esp_https_ota`** — see the
  comment above `openFlashSlot()` for the reasoning: `Update.h` never runs
  `esp_image_verify()`, and `esp_https_ota` owns its own blocking transfer
  loop, which would stop the portal answering `GET /api/update` for the whole
  download.
- **Rollback** rides the framework's own `CONFIG_APP_ROLLBACK_ENABLE` (already
  on in the prebuilt pioarduino esp32s3 sdkconfig — no custom sdkconfig
  needed). `otaMarkAppValidIfPending()`, called from `lowLevelSleep()`, is the
  only place the app confirms itself, and only after a full cycle reaches
  sleep — not at boot — so a crash mid-render on the freshly installed image
  still rolls back on the next reset.
- **Known residual, not fixed here:** the portal has no auth, so a LAN peer
  can request an older, still-genuine release (a downgrade) — it cannot
  supply an arbitrary binary. See the file header comment for why that is
  reported rather than closed in this change.
- **Unexercised against a real release.** Covered by 33 portal-side mock
  tests (the website's test-portal.html, private codebase); no version has
  been tagged yet, so nothing has pulled a genuine GitHub release end to end.

## Measurement checklist (v0.1 exit criteria)

These are the reason this firmware exists. All still TODO — nothing below has
been measured, and no number here should be quoted until it has.

- [ ] **Deep-sleep floor current (µA)** — the top hardware risk. ARCHITECTURE §6
      assumes 50–200 µA for the board and admits it is unmeasured; the battery
      model is guesswork until this number is real.
- [ ] **mAh per full cycle** (fetch + render + ~30 s refresh) vs **mAh per
      skipped wake** (fetch + hash only). The ratio is what justifies hash-skip.
- [ ] **Fetch + parse + render timings** against a real HKO response — the log
      already prints each stage in ms; collect them across a day of real
      responses, not one sample.
- [x] **Loop-task stack high-water** (first hardware number, 2026-08-04): with
      `SET_LOOP_TASK_STACK_SIZE(32 KB)`, a full cold-boot cycle rendering
      temp-trend (chart widget, 55 ms render) left **23 820 bytes free** — the
      real workload used ~8.9 KB, nowhere near the 43–69 KB the native-preview
      measurement suggested (host builds carry far fatter frames). A QR-drawing
      page adds a known ~8 KB leaf on top; even so 32 KB has ≥2× margin. Do NOT
      trim below 32 KB without re-measuring a QR screen + TLS in one cycle.
      Captured over the CH340 with a `-DYAT_LOG_MIRROR_UART` build (LOGF's
      JTAG port never enumerates through this hub).
- [ ] **Peak RAM, heap side** — `setup()` logs free heap at boot and at the end
      of a rendering cycle; collect across a day of real cycles.
- [ ] **48 h unattended on battery, hourly, no wedge.**
- [ ] **State store round-trip on real hardware.** Force a fetch failure (e.g.
      block the HKO host) on a device with at least one prior successful
      cycle, and confirm the page renders last-good data plus the stale
      footer glyph rather than blanks, and that `[state] stale since HH:MM`
      appears in the log. Then confirm a pack source with `min_refresh_min`
      set skips the network entirely within that window (`[fetch]` line
      absent, data still fresh).
- [ ] **`image` widget end-to-end on the panel**, not just `yat-preview`: a
      pack with a real `image` source, decoded via `PngleImageDecoder` and
      dithered onto the actual 6-color panel. Confirm it against a range of
      PNG shapes (paletted, RGBA, small vs. near-800×480) since only
      `yat-preview`'s stb_image path has been exercised so far.
- [ ] **PSRAM allocation trace for the image decode buffer.** The sdkconfig
      check above (`CONFIG_SPIRAM_USE_MALLOC`/`_ALWAYSINTERNAL`) says a ~1.15
      MB `std::vector` should route to PSRAM automatically, but that is a
      build-config read, not a measurement — confirm with
      `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` before/after a large-image
      decode on-device, and watch for fragmentation across repeated cycles.
- [ ] **Provisioning, all three triggers, on real hardware** (compile-only so
      far — see caveats below): erase NVS/flash fresh and confirm first-boot
      provisioning; hold KEY0 5 s from a normal running state and confirm
      re-entry; force 3 connect failures (e.g. saved SSID out of range) and
      confirm the counter trips provisioning instead of a 4th blind retry.
- [ ] **Portal QR end-to-end**: scan QR1 with a phone, confirm it joins the
      `YAT-XXXX` AP, confirm the captive portal page actually pops up (or
      scan QR2 as the fallback), submit real credentials, confirm the device
      reboots and comes up connected. Repeat with a refresh in flight to
      exercise the core-0 portal pump task.
- [ ] **Improv-serial over WebSerial/a serial terminal**: identify, get device
      info, send credentials, confirm NVS save + connect + reboot; confirm a
      concurrent `YAT STATUS` line during the same console window still gets
      a clean YAT response (no cross-talk between the two protocols).
- [ ] **KEY0 tap vs hold, on hardware, first.** Everything else about voice
      depends on this one bit being read correctly, and it is the one change
      that can regress provisioning. Tap KEY0 and confirm the log says
      `KEY0 released after N ms — tap (voice capture)`; hold it 5 s and confirm
      the provisioning screen still comes up. Watch specifically for a *tap*
      that logs a 5 s hold — that is the RTC-mux-vs-digital-read hazard
      `readKey0GestureAtWake()` calls `rtc_gpio_deinit()` to avoid, and it
      would show up as taps dropping into provisioning.
- [ ] **Buzzer cues.** Confirm all four are audible and distinguishable at arm's
      length: start chirp, stop chirp, rising double (match), low buzz (error).
- [ ] **Mic capture quality.** With `voice.seconds` at its default, check the
      `[voice] captured N/N bytes` and `[voice] p-p level` lines: a near-zero
      peak-to-peak means the mic or its power rail is the problem, not the STT.
      Then confirm the mic really is unpowered between taps (GPIO38 low).
- [ ] **Cantonese / English / Mandarin match**, one phrase each, against a
      multi-page config: confirm the `[voice] heard "..." -> MATCH <id>` line,
      the double-beep, and that the *matched* page (not the previously active
      one) is what the refresh draws. Then say something deliberately unrelated
      and confirm the low tone with the screen unchanged.
- [ ] **Voice failure paths.** Clear the key (`yat.py secret stt -`) and confirm
      an immediate error tone with no recording at all; then set a deliberately
      wrong key and confirm the HTTP error is logged and beeps low rather than
      wedging the cycle.
- [ ] **`record ∥ WiFi` overlap, measured.** Compare `[voice] wifi up in N ms`
      against the capture length in the same log: if N is consistently below the
      capture time, association is fully hidden behind the recording and the tap
      costs nothing extra. If it is not, that gap is the number to optimize.
- [ ] **PSRAM before/after a capture** (`[mem]` lines around a voice cycle) to
      confirm the ~128 KB body buffer is actually released — repeated taps
      across one wake are not possible, but repeated tap-wakes must not
      accumulate.
- [ ] **Warning takeover, on real hardware.** There is no way to force a real
      T8 signal or black rainstorm warning to test against, so use `YAT
      TAKEOVER 1` (simulate T8) and confirm: the panel redraws immediately
      (not on the next cadence tick) showing the embedded warning-takeover
      page; `YAT STATUS` reports `"takeover":true,"signal":"TC8NE
      (SIMULATED)"`; the device wakes again within 15 minutes regardless of
      `config.json`'s `quiet_hours`; a second wake with the simulation still
      set does NOT redraw the panel (hash-skip on the unchanged simulated
      signal); `YAT TAKEOVER 0` then confirms the panel redraws again showing
      the normal active page, immediately. Repeat with `YAT TAKEOVER 2`
      (Black Rain) for the closures-line branch. Then, separately, confirm a
      real `warnsum` fetch failure (e.g. block the HKO host) logs and keeps
      the prior state rather than wedging or silently deactivating.
- [ ] **Offline/no-data card state machine.** Force a WiFi failure (e.g. a
      wrong saved password) and confirm the offline card renders on the
      *first* failed cycle only (`[offline] offline -- consecutive=1 (first
      -- drawing card)`), that subsequent failed cycles suppress the redraw
      (`consecutive=2`/`3`, no `[epd] init (offline card)` line), and that
      restoring WiFi forces the normal page to render immediately even if
      the hash happens to match (`[hash] unchanged, but back online after an
      offline streak`). Separately, confirm a stale-serve cycle (WiFi up, a
      source blocked, a prior snapshot on record) does **not** trigger the
      card — the page should render normally with the `stale HH:MM` footer
      marker instead. Repeat forcing the "every source failed, no stale
      snapshot" path directly (a source with no prior snapshot, blocked).
- [ ] **Low-battery gate.** With a bench supply, drop the ADC-measured
      voltage below the ~8% threshold (~3.37 V) and confirm the low-battery
      card renders, the device sleeps ~6 h with no WiFi/fetch (`[power]
      battery ... (N%)` logged, N < 8), and a button press wakes it early and
      redraws rather than doing nothing. At a normal battery level, confirm
      `eng.setBatteryPercent()` actually draws the standard-chrome footer
      glyph (wired on the engine side earlier but never called from firmware
      before this).
- [ ] **Auto-rotate on real hardware.** With three pages configured and
      `"loop_min": 30`, confirm across a few hours that the panel is on a
      different page each half hour (`[loop] every 30 min — advancing to page
      N`), that a page whose pack asks for a shorter cadence still refreshes on
      its own cadence in between without rotating (`[loop] N min still to run`),
      and that a KEY1 press gives the page it lands on a full 30 minutes before
      the next advance. Then set `quiet_hours` around the current time and
      confirm `[loop] quiet hours ... — not rotating overnight` with the panel
      untouched until morning. Finally, `YAT TAKEOVER 1` and confirm
      `[loop] warning takeover holds the panel` and that clearing it lands on a
      sensible page rather than mid-rotation. Battery cost is the thing to
      watch: rotation converts hash-skipped wakes into full 30 s refreshes, so
      pair this with the mAh-per-cycle measurement above before recommending
      any interval as a default.
- [ ] **Keyless-KEY0 tone.** With `stt_key` cleared (`yat.py secret stt -`),
      tap KEY0 and confirm the two-tone `beepNoKey()` chirp plays — not the
      error buzz — with `[voice] no STT key` in the log; then set a key and
      confirm a genuine failure (e.g. a deliberately wrong key) still plays
      the error buzz.

## UX audit fixes (docs/UX-FLOWS.md §11)

Implemented, compile-only verified (`pio run` succeeds; no upload, no serial,
device untouched) — pending on-device test:

- [x] **#1 — G7 + G14, the post-setup ghost screen** — implemented, pending
      on-device test. `saveWifiCreds()` now clears the NVS `hash` key so a
      re-provisioned device's first cycle can never hash-skip against a
      previous owner's snapshot; `g_coldBootWake` forces a render whenever the
      wake cause is `ESP_SLEEP_WAKEUP_UNDEFINED` (power-on/brownout/post-
      provisioning/post-OTA), regardless of hash match.
- [x] **#3 — G9, the provisioning one-way door** — implemented, pending
      on-device test. Before the `wifi_fails>=3` trigger commits to
      provisioning, `setup()` now retries the saved credentials once in the
      same boot. `runProvisioningMode()` sets `setConfigPortalTimeout(600)`;
      when the portal times out with nobody having completed it, the device
      deep-sleeps 30 min and retries the saved credentials on the next wake
      instead of spinning in AP mode forever.
- [x] **#4 — G16, the unlabeled header clock** — implemented, pending
      on-device test. `engine/src/render.cpp`'s standard-chrome header now
      draws `更新 HH:MM / upd HH:MM` instead of a bare clock, same position
      and size — see `docs/UX-FLOWS.md` §7b. All "standard" chrome goldens
      (`hko-now`, `render-test`, `family-board`) regenerated accordingly.
- [x] **#5 — G4, 2.4 GHz never stated** — implemented, pending on-device
      test. The provisioning e-paper screen and the WiFiManager portal
      (`setCustomHeadElement`) both now show "只支援 2.4GHz WiFi · 2.4 GHz
      networks only" near the network-picking UI.
- [x] **#6 — G5 + G12 (docs/UX-NONTECH.md §8 fix 7), the offline/no-data
      card** — implemented, pending on-device test. `drawOfflineCard()`
      renders a firmware-owned bilingual card — reusing the `prov::` drawing
      helpers plus a new `prov::drawIcon` — on the first cycle where WiFi
      never comes up, or every source fails with no stale snapshot
      (`trackOfflineCycle()`, NVS `offline_n`). Later cycles in the same
      streak suppress the redraw; coming back online forces a normal-page
      render even if the hash matches. Stale-serve is deliberately excluded —
      see "Offline / no-data card" above.
- [x] **#8 — G25 + G26 (docs/UX-NONTECH.md §8 fix 7), battery glyph +
      low-battery behavior, firmware half** — implemented, pending on-device
      test. `eng.setBatteryPercent()` is now actually called every wake
      (previously wired only on the engine side, drawing nothing on a real
      device); below 8% the cycle is skipped entirely for a firmware-owned
      low-battery card and a 6 h sleep with no WiFi/fetch. See "Battery"
      above.
- [x] **docs/UX-NONTECH.md §8 fix 6, firmware half — keyless KEY0 no longer
      sounds broken** — implemented, pending on-device test. A KEY0 tap with
      no STT key configured now plays `beepNoKey()` (two short mid tones)
      instead of `beepError()`'s angry buzz; every genuine voice failure
      (mic, WiFi, STT, no match) still uses the error tone. See "Tap-to-talk
      voice" above.

## Known gaps

- **Time does not survive deep sleep.** Every wake re-syncs SNTP, which keeps
  WiFi up longer than necessary. ARCHITECTURE §3 wants the PCF8563 RTC to hold
  the clock across sleeps, with NTP only as a periodic correction. Not wired up
  here, and it will change the per-cycle energy number.
- **Bundle is compiled in, not on LittleFS.** `certs.h` now covers six roots
  (Hongkong Post, GlobalSign R3, ISRG X1, DigiCert Global Root G2, GTS R1/R4)
  as a concatenated PEM buffer — enough for HKO plus common pack sources
  (hn.algolia.com, open-meteo.com, open.er-api.com...). ARCHITECTURE §2's
  user-extendable curated store on LittleFS (for roots outside this set) is
  still ahead.
- **No BLE.** The BLE GATT half of the toolbox (ARCHITECTURE §4) is still
  ahead; v0.3 ships the serial half of the shared command layer only. (The
  buzzer question is settled: it is a real passive buzzer on GPIO45, driven by
  `tone()` — see "Tap-to-talk voice" above.)
- **Voice needs a network and a key, and knows it.** No STT key, no WiFi, or a
  Scribe error all end in the same low error tone; the device never queues a
  clip to send later, so a tap made while the router is down is simply lost.
  Retrying is one more tap.
- **The 20 s STT response budget is a guess.** The reference demo allowed 60 s
  and a 5 s clip came back in far less; 20 s is the first thing to raise if
  real-world captures start timing out (`STT_RESPONSE_TIMEOUT_MS`).
- **Warning takeover is compile-only verified so far.** `pio run` succeeds
  with the policy check, the embedded pack, and `YAT TAKEOVER`'s simulation
  path, but none of it has run on the actual device yet — see "Warning
  takeover" above and the measurement checklist below for what's still
  outstanding on real hardware (a real T8 or black rainstorm cannot be forced
  for testing; `YAT TAKEOVER 1|2` exists for exactly this reason).
- **No pack-file backup.** `/config.json` falls back to built-in defaults on
  corruption without touching the file. A broken pack file no longer strands
  the cycle — the active-page fallback loop tries the next page, then the
  embedded `hko-now` pack — but the broken file itself is never repaired
  automatically; fixing it still takes another `PUT`/`GET`/`RM`.
- **Provisioning and voice are compile-only verified so far.** `pio run`
  succeeds with the `WiFiManager`/Improv-serial code and with the tap-to-talk
  flow, but none of it has run on the actual device yet (no upload, no serial,
  per the mid-soak measurement in progress) — see the checklist items above for
  what's still outstanding on real hardware. Every individual piece of the voice
  path is a port of code proven on this exact board in
  the e1002-test prototype (private) (buzzer, PDM recorder, Scribe upload); what
  has never run is the *combination*, and specifically the KEY0 tap-vs-hold
  split and the recording/association overlap.
- **Console checkpoints, not a true interrupt.** "Any received byte extends
  the console window" is implemented as checks at cycle boundaries (after
  WiFi connect, after fetch, after render, and always before sleep) rather
  than true preemption — a single-threaded `setup()` can't be interrupted
  mid-TLS-handshake or mid-panel-refresh without a bigger architecture change.
- **The offline card, the low-battery gate, and the keyless-KEY0 tone are
  compile-only verified so far.** `pio run` succeeds; none of the three has
  run on the actual device yet (no upload, no serial). See "Offline /
  no-data card", "Battery", "Tap-to-talk voice" above and the measurement
  checklist below for what's still outstanding.
- The panel refresh is ~30 s and blocking on the E1002; that is the hardware,
  not the code. **The E1001's refresh time has not been measured.** Its
  `PANEL_REFRESH_S` (`src/yat_common.h`) is deliberately still the E1002's
  number, because the only way to be wrong that breaks anything is to claim a
  refresh finishes sooner than it does. The `[epd] refresh %lu ms` log line
  prints the real duration of every refresh; one wake on an E1001 settles it.

## Layout

| Path | |
|---|---|
| `platformio.ini` | `[common]` plus one env per model — `e1002` (default), `e1001` |
| `partitions_ota_32mb.csv` | `nvs / otadata / ota_0 6M / ota_1 6M / phy / littlefs ~19.9M` |
| `src/main.cpp` | the whole cycle, plus the canvas and font shims |
| `include/certs.h` | generated — TLS anchors, see the header comment to regenerate |
| `include/hko_now_pack.h` | generated from `../yat-packs/official/hko-now.yat-pack.json` (sibling repo) |
| `include/warning_takeover_pack.h` | generated from `packs/internal/warning-takeover.yat-pack.json` (see "Warning takeover" to regenerate) |
| `include/secrets.h{,.example}` | dev-only WiFi seed for NVS (see "WiFi provisioning") |

The spec engine is not vendored here — `platformio.ini` pulls
`../engine` in as a local library via `symlink://`, so
`pio run` always builds the current engine sources.
