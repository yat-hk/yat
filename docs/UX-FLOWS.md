# YAT — UX flow audit

**Audited:** firmware `0.3.0-dev` at commit `b89c3c9`, engine at the same commit, docs as of 2026-07-28.
**Method:** every user-facing scenario walked step by step against the *implementation*, not the design docs. Every behavioural claim below cites `file:line`. Where the PRD promises something the code does not do, that is called out rather than assumed to be "coming".
**Scope note:** this file was originally an audit artifact that changed no code and no other document. That held until several §11 fixes were implemented as follow-ups — #1, #3, #4 and #5 first, then #2, #6, #7, #8 and part of #10 — verified against the current firmware/engine source and cited inline as a **Resolved** note on each underlying finding (G4, G5, G7, G9, G12, G14, G15, G16, G21, G23, G24, G25, G26), rather than leaving those findings to read as live catastrophes. Their §11 rows are marked in the Status column. Still open: #9 (the button system) and the USB-powered 60 s console-window half of #10 (G22).

---

## 0. Baseline: what the device actually is today

Before the narratives, the facts everything else hangs off.

### 0.1 The timing budget (the thing that dominates every flow)

Measured from the code path in `firmware/src/main.cpp` `setup()` (:1367–1563), with panel refresh per ARCHITECTURE §6:

| Stage | Cost | Source |
|---|---|---|
| USB-CDC wait | **2.0 s, every wake, always** | `:1383` — `while (!Serial && millis()-t0 < 2000)`. On battery `Serial` is never ready, so this is a flat 2 s tax on every single wake. |
| KEY0 5 s-hold probe | +0–5 s on any KEY0 wake | `:1282` |
| LittleFS + config | ~0.2 s | `:1410–1417` |
| SHT4x | ~0.05 s | `:1421` |
| WiFi associate | 2–5 s typical, **15 s to fail** | `:92`, `:954` |
| SNTP | 0.3–5 s | `:93`, `:971` |
| Fetch + extract | 1–3 s typical, **15 s to fail** | `:1006–1013` |
| Render | 0.5–2 s | `:1534` |
| **Panel refresh** | **~28–30 s, blocking** | `:1549` |
| Pre-sleep console window | **10 s, every wake, always** | `:97`, `:783` |

**Consequences the whole report leans on:**

- **Button press → any visible change ≈ 45 s.** (2 + wifi 3 + sntp 1 + fetch 2 + render 1 + refresh 30 + nothing else visible). Add 5 s if the button is KEY0 and held.
- **A hash-skip wake is ~18 s awake, of which 10 s is a serial console window waiting for a USB cable that is not plugged in.** More than half the awake energy of a "cheap" wake is spent listening to nobody.
- **Buttons only work while the device is asleep.** ext1 is armed in `lowLevelSleep()` (`:803–818`) and read once at wake (`:850`). Nothing polls the buttons during the 45–57 s awake window. A press during a refresh is silently, completely lost — and the only "I am awake" signal is an 80 ms LED flash at boot (`:1371–1374`), long over by then.

### 0.2 What exists vs. what the PRD describes

| PRD promise | Reality | Where |
|---|---|---|
| Web flasher site (Flash / Toolbox / Gallery) | **Does not exist.** No `site/` directory. ARCHITECTURE §9 lists it; the repo has `engine/ firmware/ packs/ tools/ skills/ docs/ schema/`. | repo root |
| Release artifacts to flash | **Exist** — `.bin`, merged factory image, `.uf2`, `manifest.json` on tag push | `.github/workflows/release.yml` |
| Playlist of pages, KEY0 cycles | **One page. Period.** `DeviceConfig` holds a single `packId` (`:353–357`); `config.json` is `page.pack` singular (`:284–289`). No playlist, no rotation, no cycling. | `main.cpp` |
| KEY0 cycle / KEY1 voice / KEY2 refresh | **All three do the identical thing**: `g_buttonWake = k0 \|\| k1 \|\| k2` (`:859`) → force one render of the one page. KEY1 additionally logs a string to serial nobody sees (`:857`). | `main.cpp:857–859` |
| Warning takeover (T3+/red/black rain) | **Not implemented.** No HKO warning poll, no preemption, no takeover page anywhere in firmware. | absent; firmware/README.md "Known gaps" concurs |
| `sync_url` remote config | **Not implemented.** No occurrence in firmware. | absent |
| BLE config mode | **Not implemented.** | absent |
| Voice | **Not implemented** (v0.4). | absent |
| Standard chrome shows "battery/stale glyphs" | **Stale glyph: yes. Battery glyph: no.** Chrome draws pack name + clock (header), source host + optional `stale HH:MM` + `YAT 日` (footer). No battery anywhere. The battery *icon artwork exists* and is unused. | `engine/src/render.cpp:890–927`; `engine/src/icons.cpp:62` |
| `docs/SETUP.md`, user skill (setup/configure/diagnose) | **Do not exist.** Only `skills/pack-developer/`. | `docs/`, `skills/` |
| Host-side tool to talk the YAT serial protocol | **Does not exist.** `tools/` = `icons/ preview/ release/ wasm/`. | `tools/` |

### 0.3 What the device shows out of the box

The seeded default is `hko-now` (`main.cpp:284–289`, `:318–323`): **current temperature for 沙田, humidity, and a red 高溫注意 line when temp ≥ 33. Hourly.** That is the entire product a gifted household receives. Not weather + warnings, not bus, not news — one number for Sha Tin, whether or not the recipient lives in Sha Tin.

---

## 1. First unboxing — maker, USB, own device

**Persona:** Wing, software engineer, has PlatformIO, bought an E1002 to hack on.

**Walkthrough.**

1. Wing reads README.md. It links PACK-SPEC, AGENTS.md, the simulator. It does not tell him how to flash a device or what the buttons do. He opens `firmware/README.md` and finds `pio run -t upload`.
2. `git clone`, `cd firmware`, `pio run -t upload`. First build pulls the toolchain and efont (~738 KB of PROGMEM, `main.cpp:48`) — several minutes. It works; no `secrets.h` needed (`main.cpp:1433` seeds only if filled in).
3. Device reboots. **Panel is blank/whatever it held.** No creds in NVS → `runProvisioningMode()` (`:1436–1444`). Firmware calls `epaper.begin()` and renders the QR page — **~30 s of blank screen with no LED, no feedback** (`:1327–1334`; the LED heartbeat at `:1356` only starts *after* the refresh).
4. Provisioning screen appears: bilingual "設定 WiFi / Set up WiFi", a reason line in small blue text, two 240 px QR codes, and `網絡 Network: YAT-XXXX  密碼 Password: XXXXXXXX` (`:1179–1221`). This screen is genuinely good.
5. Wing has a serial monitor open, so he could use Improv — except **there is no Improv client**. improv-wifi.com's web tool would work in Chrome, but nothing in the repo tells him that. He scans QR1 with his phone instead.
6. Phone joins `YAT-XXXX`, captive portal pops (pumped on core 0, `:1296–1325` — correctly, this is the one thing the design got exactly right). He picks his SSID, types the password, submits.
7. Browser shows WiFiManager's stock *"Saving Credentials — Trying to connect ESP to network. If it fails reconnect to AP to try again"* (`WiFiManager/wm_strings_en.h:75`). Nothing further ever arrives.
8. Device connects, `saveWifiCreds`, `ESP.restart()` (`:1341–1348`). ~40 s later the temperature page appears. **Total: ~10 min including build.** Under the PRD's 15 min bar, for a maker.

**Gaps.**

- **G1 (major, docs).** README.md has no flash instructions, no first-boot description, no button reference. The one document a first-time visitor reads ends at "point an agent at the repo". A maker who is not also a firmware developer stalls at step 2.
- **G2 (major, docs).** The Improv path is implemented (`:1230–1250`, `:1397–1401`) and completely undiscoverable. One sentence — "or open improv-wifi.com in Chrome with the device plugged in" — converts a working feature from invisible to usable.
- **G3 (paper-cut, firmware).** 30 s of dead blank panel before the provisioning screen exists, with no LED. Smallest fix: start the LED heartbeat *before* `epaper.update()`, not after (move the blink out of the wait loop at `:1336`).

---

## 2. First unboxing — GIFTED device, phone only, Chinese SSID, 20-char password

**Persona:** Mrs. Chan, 58, Sha Tin. Her nephew flashed a device, tested it on his own WiFi, wrapped it. Home network SSID is `陳家 5G` / `陳家`, password is a 20-character string with `!` and `@` in it, written on the router sticker. She owns an iPhone and no computer.

**Walkthrough.**

1. She unwraps it. **The screen already shows Sha Tin's temperature — from her nephew's flat, frozen at whatever time he last tested.** Nothing indicates it is stale or that setup is needed. (Nephew's WiFi is out of range; the device won't even try until its next scheduled wake.)
2. She plugs in USB-C to charge. Nothing visible happens.
3. Some time in the next hour, a timer wake fires. `wifiConnect` fails after 15 s. `wifiRecordFailure()` → fail #1. Sleeps 5 min (`:1446–1449`, `RETRY_MIN=5` at `:90`). **The screen does not change. No render happens on this path at all** — the failure branch never reaches `epaper.begin()`.
4. t+5 min: fail #2. t+10 min: fail #3. **Fifteen minutes of a device that looks perfectly fine and is doing nothing.**
5. t+15 min: `fails >= 3` (`:1436`) → `runProvisioningMode("連線失敗 3 次", "3 consecutive WiFi failures")`. 30 s blank, then the QR screen.
6. She scans QR1. iPhone joins `YAT-XXXX`, iOS captive-portal sheet opens. WiFiManager's scan list appears. `<meta charset='UTF-8'>` is emitted and `htmlEntities()` passes UTF-8 through untouched, so **`陳家` renders correctly** — a genuine pass.
7. **`陳家 5G` is not in the list** (2.4 GHz radio only). `陳家` is. She has no idea these are the same router, and nothing on screen or in the portal mentions 2.4 GHz. She may well pick the wrong one from a neighbour's list.
8. She taps `陳家`, types the 20-character password on a phone keyboard, in an `<input type=password>` with a "Show Password" checkbox (`wm_strings_en.h:66`). Field max is 64 — fine.
9. Submit. iOS shows "Saving Credentials — Trying to connect". `connectWifi()` runs **blocking, on the core-0 portal task** (`WiFiManager.cpp:890`), which means the AP stops answering for up to 15 s and iOS is likely to bounce her off `YAT-XXXX` back to cellular. She never sees a result either way.
10. If the password was right: device reboots, and — see G7 below — **it may show "設定 WiFi / Set up WiFi" for up to an hour after successful setup.**

**Gaps.**

- **G4 (blocker, firmware + docs).** Nothing anywhere tells a phone-only user the network must be **2.4 GHz**. This is named in PRD §4.1 as "the #1 predictable support issue" and is unmitigated in the product. Smallest fix: one line on the provisioning screen (`drawProvisioningScreen`, after `:1206`) — `只支援 2.4GHz WiFi · 2.4 GHz networks only` — plus `g_wm.setCustomHeadElement()` to put the same string above the scan list.

  **Resolved 2026-08-05:** the notice is drawn on the provisioning e-paper screen (`firmware/src/yat_screens.cpp:174–175`, constant `WIFI_24GHZ_NOTICE`) and injected as a head element above the captive portal's scan list (`firmware/src/yat_provision.cpp:88`).
- **G5 (blocker, firmware).** **15 minutes of total silence between "device cannot reach WiFi" and any on-screen acknowledgement**, during which the screen shows convincing-looking stale content. Smallest honest fix: on the *first* WiFi failure, render a small offline marker into the existing chrome rather than skipping the render entirely. That requires a render path that does not depend on a successful fetch — currently there isn't one (see G12).

  **Resolved 2026-08-05:** see G12 below — `drawOfflineCard()` is exactly that render path, and it is called from the WiFi-failure path too (`firmware/src/main.cpp:681`).
- **G6 (major, firmware).** No feedback loop on password entry. The user gets WiFiManager's stock "trying to connect", and the device never says anything on the panel or in the portal. Smallest fix: `g_wm.setSaveConnectTimeout()` + a custom `/status`-style page is the proper fix; the *cheap* fix is to make the portal's success state visible on the panel (see G8).
- **G7 (blocker, firmware) — the re-provisioning ghost screen.** After the portal succeeds the device reboots into a normal cycle. `prevHash` in NVS still holds the hash from the *nephew's* last successful render. HKO's rounded temp/humidity for the same district very plausibly hash identically → `hash-skip` at `:1515` → **the device sleeps without rendering, and the panel keeps showing "設定 WiFi / Set up WiFi" even though setup succeeded.** For up to one full schedule interval (default 60 min). The user reasonably concludes it failed and starts over. **Fix is one line:** clear the `hash` key inside `saveWifiCreds()` (`:901–909`), or set `g_buttonWake = true` on a post-provisioning boot. This is the highest severity-to-effort ratio item in the entire report.

  **Resolved 2026-08-05:** `saveWifiCreds()` now clears the NVS `hash` key on every credential save, with the reasoning spelled out inline (`firmware/src/yat_net.cpp:54–60`), so the first post-setup render can never hash-skip against a stale owner's data.
- **G8 (major, firmware).** Entering provisioning destroys the last-good page (`:1330` unconditionally overwrites the panel), and success is never confirmed on the panel — the QR screen persists until the next successful render. A "已連線 / Connected — loading your page" interstitial before `ESP.restart()` would cost one more 30 s refresh and remove the entire ambiguity. Given that the alternative is an hour of ambiguity, that is a good trade.

---

## 3. Moving flat / new router

**Persona:** same device, new address, new SSID.

**Walkthrough.** Identical to §2 steps 3–5: three 15 s failures at 5-minute spacing, screen frozen and plausible-looking throughout, then at **t ≈ 15.5 min** the provisioning screen. So the device does self-recover into provisioning — eventually, silently, and only after destroying the displayed page.

**How does the user discover KEY0-hold?** They do not. Checked exhaustively:

- Nothing is printed on screen at 1, 2, or 3 failures. The panel is not touched on the WiFi-failure path at all (`:1446–1449` goes straight to `deepSleepMinutes`).
- `drawProvisioningScreen` (`:1179–1221`) does not mention KEY0, because by the time it is drawn the user is already in provisioning.
- README.md: no button documentation. `firmware/README.md` and PRD §4.3 have it — neither is in a gifted household.
- No sticker, no card, no label. The buttons are unlabelled on the hardware.

**Gaps.**

- **G9 (blocker, firmware) — the provisioning one-way door.** `runProvisioningMode()` is `[[noreturn]]` with an unbounded `for(;;)` (`:1309–1362`), runs `WIFI_AP_STA` at full power with no deep sleep, and WiFiManager's `_configPortalTimeout` defaults to 0 and is never set by the firmware (`WiFiManager.h:545`). Combined with the fact that the fails≥3 trigger **never clears the stored credentials and never retries them** (`wifiRecordSuccess()` at `:928` is only reachable after a `wifiConnect()` that is no longer attempted): *a 16-minute router reboot while nobody is home permanently converts the device into a provisioning-mode brick.* It will not recover when the router comes back. It will also flatten a 2000 mAh battery in roughly a day at AP-mode current. Smallest fix: (a) on the fails≥3 trigger, attempt the saved credentials once before entering the portal; (b) `g_wm.setConfigPortalTimeout(600)` and, on timeout, `deepSleepMinutes(30, ...)` and try the saved credentials again on the next wake.

  **Resolved 2026-08-05:** both halves landed, so the "permanently converts the device into a provisioning-mode brick" outcome this finding describes can no longer happen. Before the fails≥3 trigger commits to tearing down the last-good page, the firmware now retries the saved credentials once in the same boot (`firmware/src/main.cpp:554–568`, comment tagged `G9`); only if that retry also fails does it fall through to provisioning. And the portal itself is now bounded — `g_wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S)` at 600 s (`firmware/src/yat_provision.cpp:363`, constant at `firmware/src/yat_common.h:69`), not the unbounded `for(;;)` this finding found. A transient outage — a router reboot, a brief power cut — now self-heals on the next automatic wake instead of stranding the device in AP mode.
- **G10 (major, firmware + website).** The KEY0-hold-5 s gesture is undiscoverable by construction. Smallest fix that does not require new UI: put it on the provisioning screen *and* in the footer of the normal chrome? No — the honest smallest fix is a printed card / a "what the buttons do" section in README.md and on the eventual site, plus the on-screen offline state from G5 carrying the text `長按 KEY0 5 秒重新設定 / hold KEY0 5s to reconfigure`.
- **G11 (major, firmware).** KEY0-hold is only sampled when KEY0 *caused the wake* (`:1283` requires `ESP_SLEEP_WAKEUP_EXT1`). During the 45–57 s the device is awake each cycle, and during the entire provisioning-mode loop, holding KEY0 does nothing. There is no feedback that a hold registered — the user holds a button for 5 s and then waits another 35 s before anything happens on screen.

---

## 4. Wrong password typed in the portal

**Traced exactly.**

1. User submits the form. `handleWifiSave` sets `connect = true` and immediately returns the stock "Saving Credentials / Trying to connect ESP to network. If it fails reconnect to AP to try again" page (`WiFiManager.cpp:1885–1899`, `wm_strings_en.h:75`).
2. On the next `process()` tick on core 0, `processConfigPortal()` runs `connectWifi(_ssid, _pass, ...)` **blocking** with `_connectTimeout = 15 s` (set by the firmware at `:1315`) (`WiFiManager.cpp:890`).
3. Association fails. `_shouldBreakAfterConfig` is false (default, never set), `_configPortalIsBlocking` is false (`:1314`), so control falls through: **the portal stays up, `process()` returns non-`WL_CONNECTED`, `g_portalSuccess` stays false, and the firmware's `for(;;)` keeps waiting.**
4. **The panel never changes. The browser page never changes. The LED keeps blinking at exactly the same 500 ms rate it was blinking before.** There is no signal of any kind distinguishing "wrong password" from "still working on it" from "device is dead".
5. Retry path: the user must notice their phone left the `YAT-XXXX` network (it likely did, during the blocking connect attempt), rejoin it, re-open `192.168.4.1` (or rescan QR2 — which is why QR2 exists, and it does help here), and try again.

**Does it count toward `wifi_fails`?** **No.** `wifiRecordFailure()` is only called from the normal-cycle path at `:1447`. A portal-side failed connect increments nothing. The device is already in provisioning, so the counter is moot — but this also means the portal will accept unlimited wrong attempts and never escalate or explain.

**How many minutes until the portal returns?** It never left. That is the good news and the bad news: retry is instant, but there is nothing to tell the user a retry is needed.

**Gaps.**

- **G6** (above) covers the missing failure signal — and this scenario is where it hurts most. Smallest honest fix: on a failed portal connect, redraw the provisioning screen's reason line as `密碼錯誤，請再試 / Wrong password — try again` (a 30 s refresh, but the user is standing there and it is the only channel that cannot be missed). Hook: `WiFiManager::setSaveParamsCallback` / check `getLastConxResult()` in the `for(;;)` loop at `:1338`.
- **G13 (paper-cut, firmware).** The blocking 15 s connect attempt on the portal task stalls DNS+HTTP, which is exactly the situation the core-0 task was created to avoid. `g_wm.setConnectTimeout(15)` at `:1315` is the knob; 8 s halves the stall with no real downside on a 2.4 GHz home network.

---

## 5. Hostile network shapes

| Shape | What actually happens | Severity |
|---|---|---|
| **5 GHz-only household** | SSID absent from the portal scan list. User can type it manually (`wm_strings_en.h:66` provides an SSID field, max 32 chars). Association fails. Infinite portal, no explanation. Dead end. | **Blocker** — G4 |
| **Corporate / hotel captive portal** | Association *succeeds*, so `wifiRecordSuccess()` fires and `wifi_fails` resets to 0. Then the HTTPS fetch is intercepted → `http.GET()` returns non-200 or TLS fails → `deepSleepMinutes(RETRY_MIN, "fetch/extract failed")` (`:1491–1494`). **No render, no message, no escalation — the device retries silently every 5 minutes forever with a frozen screen.** On a first-run device there is no stale snapshot either, so `fetchAndExtract` hard-fails before the engine can serve anything. | **Major** — G12 |
| **WPA3-only router** | Untested. ESP32-S3 under IDF 5 supports WPA3-SAE for STA, so it plausibly works, but nothing in this repo has exercised it and PMF-required configurations are a known failure mode. If it fails it fails as §2/§3 — 15 min of silence, then the portal, then the portal forever. | **Unknown — needs a hardware test** |
| **Hidden SSID** | Works: the portal has a manual SSID text input. `WiFi.begin(ssid, pass)` on a hidden network associates normally. Nothing tells the user the manual field is the answer when their network is missing from the list — and this is indistinguishable from the 5 GHz case from the user's side. | Paper-cut (folded into G4's fix) |

- **G12 (major, firmware).** Every fetch-layer failure — captive portal, DNS hijack, HKO outage, expired CA, wrong clock — produces the same outcome: **no render at all**, so the panel silently keeps whatever it last showed. The stale-serve machinery (`engine/src/extract.cpp:439`, footer glyph at `render.cpp:915–923`) only produces a visible signal when (a) WiFi associated, (b) the source has a prior successful snapshot, and (c) the render path is reached. On a fresh device none of that holds. Smallest fix: a firmware-drawn "cannot reach the internet" status card, reusing `drawProvisioningScreen`'s existing text/QR helpers (`main.cpp:1068–1146`) — the drawing primitives are already there and pack-independent.

  **Resolved 2026-08-05:** `drawOfflineCard()` (`firmware/src/yat_screens.cpp:240–250`, "上唔到網 · Can't reach the internet") is now called on the failure paths this finding names (`firmware/src/main.cpp:681`, `:934`, `:939`) — the no-render gap is closed.

---

## 6. Interrupted provisioning

**Power dies mid-provisioning.** Credentials are only written on success (`:1241`, `:1342`). A power loss before that leaves NVS untouched → next boot re-enters provisioning cleanly. **No gap.** The one real risk is that provisioning mode *itself* is what drained the battery (G9).

**Phone walks out of AP range mid-portal.** The portal is stateless between requests; the user rejoins and continues. QR2 (`http://192.168.4.1`) exists precisely for the "captive sheet closed" case and is the right call. **No gap** — except that the QR is on the device's screen, which is fine, and there is no way to re-show it without walking back to the device, which is also fine.

**Power dies mid-panel-refresh** (any cycle, not just provisioning). E-ink left mid-waveform → visible ghosting/banding. On recovery the device boots, fetches, and if the data hasn't changed, `hash-skip` fires (`:1515`) → **no re-render → the garbled image stays** until either the data changes or someone presses a button. Same root cause as G7.

- **G14 (paper-cut, firmware).** Fold into G7's fix: any boot whose wake cause is `ESP_SLEEP_WAKEUP_UNDEFINED` (power-on, brownout recovery, post-provisioning restart, post-OTA) should force a render. One condition added at `:1515`.

  **Resolved 2026-08-05:** `reportWakeReason()` sets `g_coldBootWake = true` on `ESP_SLEEP_WAKEUP_UNDEFINED` (`firmware/src/yat_sched.cpp:731–733`), and the hash-skip check now bypasses whenever that flag is set (`firmware/src/main.cpp:978`).

---

## 7. Daily life

### 7a. Warning-takeover morning

**Persona:** T8 hoists at 06:40. Mrs. Chan looks at the device.

**What she sees:** Sha Tin's temperature. That is all. There is no warning takeover in the firmware — no HKO warning endpoint poll, no playlist preemption, no takeover page, no tightened cadence. PRD §4.2 calls this "the single most 'Hong Kong' feature" and PRD §5.1 calls the takeover page "the demo that sells the product". **It does not exist in any form.** The engine cannot substitute: official warning iconography is deliberately firmware-reserved (PACK-SPEC §9.9/§12.3, ARCHITECTURE threat model #6), so no pack can fill the hole either.

- **G15 (blocker for the PRD's promise, firmware).** Warning takeover is the product's headline feature and is 0% implemented. It is also the one feature that would justify a household tolerating everything else in this report. Smallest honest first cut: poll `https://data.weather.gov.hk/weatherAPI/opendata/opendata.php?dataType=warnsum` on each wake (one small JSON, already inside the CA bundle's coverage per `firmware/README.md`), and when a T3+/red/black code is present, render a firmware-owned full-screen page and clamp the next sleep to 15 min. This is a self-contained ~150-line addition that needs no engine or spec change.

  **Resolved 2026-08-05:** implemented close to as specced. A `warnsum` fetch runs each wake (`firmware/src/yat_net.cpp:519–560`, hitting the exact endpoint named above) and drives a firmware-owned takeover page and tightened wake cadence (`firmware/src/main.cpp`).

### 7b. Quiet hours — "the screen is frozen, is it broken?"

**Walkthrough.** 23:30–06:30 by default (`:288`). At 23:30 the scheduler computes a sleep straight through to 06:30 (`:501–514`) and the panel holds its last image at zero power — correct, and exactly the design. But at 07:15 the household looks at it and sees, in the header, **the clock reading `23:2x`** — because the header clock is the *render* time (`render.cpp:898–902`), and nothing re-rendered.

This is worse than "frozen": the device presents as a **wall clock that has stopped**. That reading is available at a glance from across a room and is unambiguous — and it is wrong. And it is not confined to quiet hours: every hash-skip (`:1515–1521`, the single biggest battery lever in the design) leaves a stale clock on screen. A working, correctly-behaving YAT spends most of its life displaying a wrong time in large type in the top-right corner.

- **G16 (blocker, engine).** The header clock reads as "now" and means "last render". Smallest fix: label it — draw `更新 07:12 / upd 07:12` instead of a bare `07:12`, at the same position, in the same size. Two lines in `render.cpp:898–902`. This is a small change to the most-looked-at pixels on the device.

  **Resolved 2026-08-05:** the header now prints `更新 HH:MM / upd HH:MM` (`engine/src/render.cpp:1801` — the file has grown since this audit, but the fix is the one specced here).
- **G17 (paper-cut, firmware).** Nothing indicates quiet hours are in effect. A one-glyph moon in the footer during quiet hours would resolve "is it asleep or dead" — but note the footer is only redrawn on a render, so this can only be set on the *last* render before quiet hours begin, which is exactly when the scheduler knows it (`:505`). Worth doing; the plumbing (engine → chrome flag) does not exist yet, so it is not as small as it looks.

### 7c. Stale-data day (router offline while the user is at work)

**Walkthrough.** Router dies at 10:00. Device wakes at 11:00: 15 s WiFi timeout, fail #1, no render. 11:05 fail #2. 11:10 fail #3. **11:15: the provisioning screen replaces the weather page**, and per G9 the device stays there, radio at full power, until the user comes home — by which time the battery has taken a serious hit and the device requires manual re-provisioning even though the router recovered at 10:30.

The intended experience — "show last good data with a stale-since glyph, never a blank or error screen" (PRD §5) — **does not occur in this scenario at all**, because stale-serve lives inside the *extract* stage and the WiFi failure aborts three stages earlier.

When stale-serve *does* fire (WiFi up, API down, prior snapshot exists), what a non-technical person sees is: `data.weather.gov.hk    stale 09:15    YAT 日` — the last two words in 8×16 px, in English only, in red (`render.cpp:915–923`). Mrs. Chan cannot read it, cannot see it from three metres, and would not know what it means if she could.

- **G18 (major, engine).** The stale marker is English-only and rendered at the smallest available size on a bilingual Hong Kong product. Smallest fix: `舊資料 09:15 · stale 09:15` at scale 1 is still small but at least readable to the intended user; better is a red dot or the existing `alert` icon glyph (`engine/src/icons.cpp` catalog is closed but already contains suitable names) at scale 2 in the footer.
- **G5 / G9 / G12** are what actually govern this scenario, and all three are firmware.

---

## 8. The button UX as a system

Three unlabelled physical buttons on the hardware. What each one does today:

| Button | PRD §4.3 says | Firmware 0.3.0-dev does |
|---|---|---|
| KEY0 | Cycle to next page | Force a render of the only page. There is no next page. |
| KEY1 | Push-to-talk voice | Force a render of the only page, plus one serial log line (`:857`). |
| KEY2 | Force refresh | Force a render of the only page. |
| KEY0 held 5 s | Re-enter provisioning | Works — if the device was asleep, and if the user waits ~35 s for confirmation (`:1282–1294`). |

**The user's actual experience of pressing any button:** an 80 ms LED flash, then **~45 seconds of nothing**, then the panel flashes through its refresh sequence, then the same content reappears — because in the common case the data has not changed and the *only* visible difference is the header clock advancing. There is no beep (no buzzer wired — `firmware/README.md` "Known gaps"), no LED activity during the wait, and no on-screen "working…" state, because a state like that would itself cost a 30 s refresh.

**Does anything on-screen ever teach them?** No. There is no help page, no first-run card, no button legend in the chrome, and no documentation the household will ever see. A household member's honest mental model after a week is "the three buttons make it blink and think for a minute, and then nothing happens."

- **G19 (major, product — firmware + docs).** Three buttons, three identical behaviours, zero labels, zero feedback, zero teaching surface, and one of them (KEY0-hold) is destructive. Smallest honest fixes, in order: (1) **give the buttons distinct jobs even with one page** — KEY2 = refresh, KEY1 = nothing until voice ships, KEY0 = refresh + the *only* button that arms the re-provision hold; (2) **acknowledge the press within 200 ms** with an LED pattern that stays on for the whole awake window rather than an 80 ms flash at boot (`:1371–1374` → hold the LED on until `lowLevelSleep`); (3) ship a printed card, since there is no cheap on-screen channel.
- **G20 (major, firmware).** Presses during the 45–57 s awake window are silently discarded. A user who presses and sees nothing presses again — during the refresh — and that press is also discarded. Smallest fix: poll the three GPIOs in `consoleWindow()`'s idle loop (`:740–748`) and set a "render again next cycle" flag; not perfect, but it converts a lost press into a delayed one.

---

## 9. Installing the Sushiro pack, mid-technical owner, no coding agent

**Persona:** Kevin, product manager, comfortable with a terminal, does not use Claude Code. He wants `yat-packs/official/sushiro-queue.yat-pack.json` (8816 bytes) on his device.

**The honest walkthrough — this is the only path that exists today.**

1. He finds the pack in the `yat-packs` `official/` gallery repo. Nothing in README.md tells him how to install it. He greps and eventually finds `firmware/README.md`'s "Serial file protocol" table.
2. He plugs in USB and opens `pio device monitor`. He sees nothing — **the device is in deep sleep** and will be for up to an hour.
3. He presses a button. Device wakes, logs appear. He now has a race: the console window only opens at four checkpoints (`maybeEnterConsole` after WiFi connect `:1451`, after fetch `:1496`, after refresh `:1551`, and unconditionally before sleep `:783`), and each is 10 s (extended by activity, `:740–748`).
4. He types `YAT LS`. If his keystroke lands before the WiFi-connect checkpoint, the window opens at ~5 s in. If not, he waits ~45 s for the pre-sleep window. Nothing tells him this; he will assume the device is broken at least once.
5. `YAT PUT /packs/sushiro-queue.yat-pack.json 8816` then **8816 raw bytes with no framing, no ack, no checksum**, within a 15 s read timeout (`:636`). `pio device monitor` cannot send a file. **The repo ships no host-side tool for this** (`tools/` has none). Kevin must write his own pyserial script — and get the byte count exactly right, in UTF-8 bytes, not characters, or he gets `ERR short read` after a 15 s stall (`:639–642`).
6. Assume he succeeds: `OK 8816 written`. **The pack is on the device and completely inert.** `config.json` still says `"pack": "hko-now"`.
7. So he must also `YAT GET /config.json`, hand-edit it, and `YAT PUT /config.json <n>` with the new byte count — and `sushiro-queue`'s params (branch, etc.) have to be authored by hand from the pack's JSON Schema, because there is no param form anywhere outside `simulator.py`.
8. `YAT REBOOT`. ~40 s later the page appears — or the engine rejects the pack and he gets `[engine] load failed:` on serial, `deepSleepMinutes(5, "pack load failed")` (`:1484–1487`), **and a screen still showing the weather page with nothing to indicate anything went wrong**. Note the embedded-pack fallback at `:1478` covers `hko-now` only; a broken custom pack has no fallback (`firmware/README.md` concurs), so the device retries the failing pack every 5 minutes indefinitely.

**Realistic assessment:** ~45–90 minutes for a competent developer who has to write the transfer script, and effectively impossible for anyone else. The PRD's "installing = copying the file onto the device" is true only in the sense that the *device side* is a copy; the *user side* is unimplemented.

- **G21 (blocker for anyone but the author, tooling).** No host-side serial client. **This is the single highest-leverage fix in the report**: a ~150-line `tools/serial/yat.py` with `ls / get / put / rm / status / reboot / install <packfile>` (where `install` does PUT + config rewrite + reboot as one command) turns an impossible task into one command, needs no firmware change, and is a prerequisite for every configuration story until BLE ships.

  **Resolved 2026-08-05:** `tools/serial/yat.py` exists, with `ls/get/put/rm/status/reboot/use/install`, a `tools/serial/README.md`, and a test suite (`tools/serial/test_yat.py`).
- **G22 (major, firmware).** The console window is a race the user cannot see or win reliably. Smallest fix: when USB is connected (`Serial` truthy at `:1383`), keep the console window open for 60 s instead of 10 s — a plugged-in device is not on battery, so the cost is zero. This also fixes the 10 s-always-on-battery waste noted in §0.1.
- **G23 (major, firmware).** A bad custom pack is an unrecoverable-looking state: silent on screen, retrying forever. Smallest fix: on `pack load failed`, fall back to `hko-now` (the embedded copy is right there at `:1467`) and render, rather than sleeping blind.

  **Resolved 2026-08-05:** when every configured page fails to load, the firmware now falls back to the embedded `hko-now` pack and renders it, logging `"all N page(s) failed to load — falling back to embedded hko-now pack"` (`firmware/src/main.cpp`, around the page-load loop), rather than sleeping blind.
- **G24 (paper-cut, firmware).** Installing a pack requires editing a second file. `YAT USE <packid>` as a seventh command — rewriting `page.pack` in place — removes the hand-edit entirely.

  **Resolved 2026-08-05:** `YAT USE <id-or-index>` is implemented (`firmware/src/yat_console.cpp:279–317`), matching by page id or numeric index, persisting the choice, and forcing a render.

---

## 10. Battery death and recovery

**As the battery dies.** `batteryVolts()` (`:268–275`) is read three times per cycle and used for exactly two things: a log line (`:1388`, `:1556`) and the `YAT STATUS` JSON (`:680–687`). **No threshold anywhere. No low-battery page, no cadence backoff, no safe shutdown, no glyph.**

What the household sees: the device works normally, then one day the screen simply stops changing. It is visually indistinguishable from quiet hours (§7b), from a hash-skip, from a WiFi outage, and from a dead device. The stopped clock in the header (G16) is the only clue and it is a misleading one.

Worse: the most likely moment for a brownout is the ~30 s panel refresh, the single highest-current operation in the cycle. So the *last* thing a dying YAT does is often to leave a half-drawn, banded image on the panel — permanently, since e-ink holds it at zero power.

**After recharge.** Plugging in USB-C powers the board and it boots. Then, per G7/G14: wake cause is `UNDEFINED`, `g_buttonWake` is false, and if the data happens to hash the same as the last successful render, **it hash-skips and never redraws** — so the garbled brownout image survives the recharge. The user must press a button (which they have not been taught) or wait for the data to change.

- **G25 (blocker for a household device, firmware + engine).** No battery indicator on a battery-powered product, despite PRD §5 ("battery/stale glyphs"), ARCHITECTURE §4 ("the standard footer shows battery + staleness on-screen") and PRD §8's risk mitigation ("on-screen battery glyph from day one"). The **artwork already exists and is unused** (`engine/src/icons.cpp:62` defines `battery`). Smallest fix: `Engine::setBatteryPercent()` plumbed from `batteryVolts()` at `:1388`, drawn in the footer next to the brand at `render.cpp:924`. Small, and it closes a promise made in three documents.

  **Resolved 2026-08-05:** `Engine::setBatteryPercent()` is called every cycle (`firmware/src/main.cpp:764`, fed from `batteryPercent(battVolts)` at `:317`), and the footer glyph draws (`engine/src/render.cpp:1471`, `:1868`).
- **G26 (major, firmware).** No low-battery behaviour of any kind. Smallest fix: below ~3.5 V, render a firmware-owned "低電量 / Low battery — please charge" page once, then stop taking scheduled wakes (buttons still wake). This preserves a readable final state instead of a random half-refresh, and stops the device from burning its last capacity on refreshes nobody will see.

  **Resolved 2026-08-05:** a firmware-owned "低電量 Low battery" card renders once below the voltage floor (`firmware/src/yat_screens.cpp:599–614`).
- **G14** covers the failure to redraw after recovery.

---

## 11. Top 10 ranked fixes

Ranked by (household impact × PRD-promise damage) ÷ effort. Effort is one engineer's working days.

| # | Gap | Fix | Where | Effort | Status |
|---|---|---|---|---|---|
| 1 | **G7 + G14** — device shows "Set up WiFi" for up to an hour *after* successful setup; brownout garbage survives recovery | Clear the NVS `hash` key in `saveWifiCreds()`; force a render when wake cause is `UNDEFINED` | firmware, `main.cpp:901`, `:1515` | **0.25 d** | ✅ fixed (pending on-device test) |
| 2 | **G21** — no host-side serial client; pack install is effectively impossible | `tools/serial/yat.py` with `ls/get/put/rm/status/reboot/install` | tooling, new file | **1 d** | ✅ shipped (`tools/serial/yat.py`) |
| 3 | **G9** — a 16-minute router outage bricks the device into an infinite, battery-draining AP mode | Retry saved credentials before the fails≥3 trigger; `setConfigPortalTimeout(600)` + sleep-and-retry on timeout | firmware, `main.cpp:1309`, `:1436` | **0.5 d** | ✅ fixed (pending on-device test) |
| 4 | **G16** — the header clock reads as "now", means "last render"; a healthy device displays a wrong time in large type | Prefix it (`更新 07:12 / upd 07:12`) | engine, `render.cpp:898` | **0.25 d** | ✅ fixed (pending on-device test) |
| 5 | **G4** — nothing tells a phone-only user the network must be 2.4 GHz (PRD's own "#1 predictable support issue") | One line on the provisioning screen + `setCustomHeadElement` in the portal | firmware, `main.cpp:1206`, `:1316` | **0.25 d** | ✅ fixed (pending on-device test) |
| 6 | **G5 + G12** — every network/fetch failure produces *no* on-screen signal; the panel keeps convincing stale content | A firmware-owned status card (offline / cannot reach source / hold KEY0 to reconfigure), drawn on the first failure, reusing `prov::` helpers | firmware, `main.cpp:1068–1221` | **1.5 d** | ✅ fixed (pending on-device test) |
| 7 | **G15** — warning takeover, the product's headline feature, is 0% implemented | `warnsum` poll on each wake + firmware-owned takeover page + cadence clamp | firmware | **3–4 d** | ✅ fixed (pending on-device test) |
| 8 | **G25 + G26** — no battery glyph, no low-battery behaviour, on a battery product; artwork already exists | `setBatteryPercent()` → footer glyph; low-voltage page + wake suspension | firmware + engine | **1 d** | ✅ fixed (pending on-device test) |
| 9 | **G19 + G20 + G11** — three unlabelled buttons with identical behaviour, no press feedback, presses lost while awake | Hold the LED on for the awake window; poll buttons in the console idle loop; distinct roles; printed card | firmware + docs | **1 d** | |
| 10 | **G22 + G23 + G24** — the serial console is a race; a bad pack retries silently forever; installing needs two files | 60 s window when USB-powered; fall back to `hko-now` on pack load failure; add `YAT USE <packid>` | firmware | **0.75 d** | ⚠️ partial — G23/G24 fixed, G22 (60 s window when USB-powered) still open |

Honourable mentions below the line: **G1/G2** (README has no flash or Improv instructions — 0.5 d, docs, and the cheapest credibility win available); **G8** (post-provisioning "Connected" interstitial); **G18** (bilingual, legible stale marker); **G13** (shorten the portal's blocking connect); **G17** (quiet-hours indicator); **G3** (LED during the first provisioning refresh).

---

## 12. The three gaps to fix before a non-technical person touches the device

Not the three biggest — the three that make handing the device to a household **actively harmful to trust**.

1. **G9 — the provisioning one-way door.** Everything else in this report degrades the experience. This one *destroys the device* from the household's point of view: a routine router reboot, unattended, turns a working display into a flat battery that demands a technical rescue. A device that cannot survive its own home network going away for a quarter of an hour is not a device you can leave with someone. *(0.5 d)*

2. **G7 — the post-setup ghost screen.** The very first thing a gifted user does is provision it, and the device's response is to keep displaying "設定 WiFi / Set up WiFi" for up to an hour. There is no recovery from that first impression: they will conclude it failed, retry, fail identically, and put it in a drawer. It is a one-line fix and it guards the only moment where the product gets to make a first impression. *(0.25 d)*

3. **G5 + G12 — silent failure with plausible content.** A YAT that has lost the network looks *exactly* like a YAT that is working. Every principle in BRAND.md and PRD §5 ("fail visibly but calmly", "never a blank or error screen", "Trustworthy") depends on the device admitting when it is not fresh, and today it admits nothing in the most common failure mode. A display that confidently shows yesterday's information is worse than a blank one. *(1.5 d)*

Combined: **~2.25 engineer-days** for the three that matter most. G4 (2.4 GHz) is a near-miss for this list and is 0.25 d — take it too.

**Resolved 2026-08-05:** all four items in this section — G9, G7, G5+G12 and G4 — are fixed in the current firmware; see the **Resolved** note under each finding above (§2, §3, §5) for the exact citations. The section is kept rather than deleted because it correctly identified the highest-priority work at the time.

---

## 13. Where the PRD and the implementation have silently diverged

These are cases where the *code* is arguably right and the **PRD should be amended**, rather than the code chased. Flagged for the next PRD revision.

1. **"KEY0 cycles pages" presumes a playlist that does not exist and is not scheduled.** `config.json` expresses exactly one page (`main.cpp:284–289`, `:353–357`), and no milestone in ROADMAP.md adds multi-page config — v0.4 is voice, v0.5 is OTA/site/skill. Yet PRD §4.2 ("a playlist of pages"), §4.3 (KEY0 cycle), §4.4 (voice *page switching*) and §4.5 (playlist modes `schedule`/`rotation`/`single`) all assume it. **Either multi-page config becomes an explicit milestone, or §4.2–§4.5 should be rewritten around a single page with voice/buttons selecting among installed packs.** Note the second reading makes voice (§4.4) incoherent — "switch to the page you named" needs pages — so this is a real product decision, not a doc edit. It should be made deliberately and soon, because v0.4's exit criterion ("≥90% match rate on the four v1 pages") is unreachable without it.

2. **PRD §4.3's button table describes an aspiration, not a contract.** With one page, "cycle" and "force refresh" are the same operation, and KEY1 is dead until v0.4. The table should say what 0.3 does and mark the rest as planned, because it is currently the only button documentation that exists and it is wrong in two rows out of four.

3. **"The owner configures; the household only ever touches buttons and voice" (PRD §2) is the correct principle and is currently unachievable** — not because of the household half, but because *the owner has no configuration surface either*. No site, no BLE, no user skill, no serial tool. The only configuration path in existence is hand-writing serial protocol frames. The PRD should say plainly that until the Toolbox ships (v0.5), configuration requires a coding agent with a USB cable, and that gifting a device is therefore not yet supported. Right now §2 reads as though gifting works today.

4. **"Rendered on the engine's standard chrome: automatic footer with data source, updated time, battery/stale glyphs" (PRD §5) is wrong on two of four items.** The footer has source and stale; "updated time" is in the *header* and is unlabelled (G16); battery is absent (G25). Either implement both (recommended — G25 is 1 d and the icon exists) or correct §5.

5. **PRD §4.2's "reaction time = the current wake interval" for warning takeover understates it by ~30 s** and, more importantly, describes a feature that does not exist. The per-wake HKO poll is the right design and should stay in the PRD — but it should be marked unimplemented, because ROADMAP v0.2 lists "warning-takeover check on every wake" as *delivered scope* and it was not delivered (`firmware/README.md` "Known gaps" is honest about this; ROADMAP.md is not).

6. **PRD §4.1's 15-minute setup target should be scoped to makers explicitly.** The measured maker path (§1) is ~10 minutes and clears the bar. The gifted-household path (§2) is ~20 minutes *when everything works*, and unbounded when it does not. The success metric "≥80% unassisted" is currently unmeasurable and, on the evidence here, would not be met by non-makers. Scope the metric to the audience the PRD already names as primary.

---

*End of audit as originally written. Fixes #1, #3, #4, #5, #2, #6, #7 and #8 from §11 (and the G23/G24 half of #10) were subsequently implemented across `firmware/src/main.cpp`, `firmware/src/yat_net.cpp`, `firmware/src/yat_provision.cpp`, `firmware/src/yat_sched.cpp`, `firmware/src/yat_screens.cpp`, `firmware/src/yat_console.cpp`, `engine/src/render.cpp`, and `tools/serial/yat.py` — see the Status column in §11 and the **Resolved 2026-08-05** notes on each underlying finding. #9 and the USB-window half of #10 (G22) remain open.*
