# YAT — the non-technical bar, browser-only

**Audited:** working tree at `f34fc74`, 2026-07-28. Website (then living in this repo's `site/`, now a separate private codebase deployed to https://yat.day): `tools.html`, `tools.js`, `gallery.html`, `gallery.js`, `generate.py`. Also firmware (`firmware/src/main.cpp` `0.3.0-dev`), engine (`engine/src/render.cpp`), packs (`packs/examples/*`), schema (`schema/yat-pack.schema.json`), host tool (`tools/serial/yat.py`).

**The bar** (PRD §2, amended 2026-07-28): *plug in over USB → the website flashes it → set up WiFi and pages in the browser → put it on a shelf.* A terminal, a JSON file, a serial command or a phone-hotspot portal as the **primary** path is a gap by definition.

**Relationship to [UX-FLOWS.md](UX-FLOWS.md):** that audit walked the maker bar against firmware and found G1–G26. It predates `site/` and `tools/serial/` existing, and several of its fixes have landed. This document does not repeat it. Its subject is the **delta the new bar creates**: what the browser can do today versus what a person with no technical background needs it to do. Where a UX-FLOWS gap is still open *and* is the thing May actually hits, it is cited by number, not re-derived.

**Live build note.** A parallel agent is building the surface §3 specifies, and it changed twice during this audit. As of 22:09 on 2026-07-28: the website's `yat-serial.js`, `pack-form.js`, `device-setup.js`, `test-fake-device.js` and a rewritten `tools.html` (steps 4 and 5) exist (this code lived in this repo's `site/` at the time; the website is now a separate private codebase); `tools.js`, `gallery.js` and `generate.py` are untouched, so **the gallery still has no install button** (§2 S4). §3 is therefore the spec *and* the acceptance check — **§11 audits what has landed** and supersedes §1's walk from step ⑥ on. §1–§2 remain the record of the journey the bar was set against.

**Persona.** 阿姨 May, 55, Sha Tin. WhatsApp and Google Maps daily. Owns a laptop she uses for YouTube and internet banking; has never opened Terminal and does not know it exists. Reads Chinese comfortably, English signage only. Wants: 沙田天氣, 969 巴士幾時到, 壽司郎仲要等幾多枱. Her nephew told her about YAT and sent her the link. **He is not in the room.** That last clause is the whole test — every "ask your agent" in this repo is a wrong answer.

---

## 1. The golden path, walked

Predicted screen by screen. Strings are quoted from source, not paraphrased.

**① She opens the site.** `index.html` is genuinely good for her: the hero is Chinese-first, §04「你需要什麼」names the four preconditions in plain language (device, *data* cable, 2.4 GHz WiFi, desktop Chrome/Edge), and the closing line — 「沒有帳戶要開」 — is exactly the reassurance she needs. She clicks 「安裝韌體 Flash a device」.

**② Tools page, step 1.** Model radio (E1002 preselected, E1001 disabled with 「韌體矩陣仲未加入」), then the release box. Today `loadRelease()` returns `kind:"none"` because no tag is cut, and `renderRelease()` writes (`tools.js:404–406`):

> **仲未有任何發佈 — 快將推出。**
> *No firmware has been released yet… — or build the firmware yourself from source with PlatformIO.*
> Releases · firmware/

`ui.btnFlash.disabled = true`. **The journey ends here today**, with an instruction to build from source with PlatformIO. This is temporal — the first tag fixes it — but the copy in the empty state is written for a maker and should not be the last thing a non-technical visitor reads. (§8, fix 8.)

**③ Assume a release exists. She clicks 安裝 Install.** Chrome's port picker opens with no explanation of what a "serial port" is; she picks the one entry. esptool-js syncs at `romBaudrate: 115200`, then **switches to `baudrate: FLASH_BAUD = 921600`** (`tools.js:22`, `:491`).

`firmware/platformio.ini:45–46` says, about this exact board:

```ini
; 921600 fails on the carrier's USB-UART bridge ("chip stopped responding")
upload_speed = 115200
```

The project has already measured that this hardware does not survive 921600, and the flasher asks for 921600 anyway. Predicted screen: the progress label flips to 「失敗 failed」, the log shows esptool-js's `chip stopped responding`-family error (`tools.js:523`), the step reverts to `ready`, and none of the five rows in the 「出問題的時候」table (`tools.html:200–206`) describes what happened. She has no way to know the cause is a constant in a JavaScript file. **This must be verified on hardware before the first release; the fix is one character-run.**

**Resolved 2026-08-05:** verified on hardware, and landed at a different (better) number than first proposed. `FLASH_BAUD` is now `460800` (the website's `tools.js:47`, private codebase), not the `115200` this finding suggested reverting to — the comment records why: "921600 fails on the carrier's USB-UART bridge... but 460800 is reliable — verified with a full 3 MB write — and matches firmware/platformio.ini's upload_speed" (also bumped to `460800`, `firmware/platformio.ini:75`). So flashing is both fixed and ~4× faster than the 115200 fallback would have been.

**④ Flash succeeds (at 115200).** 「燒錄完成。裝置正在重啟。」, then after 1.5 s `openSession()` reopens the port at 115200 and step 2 unlocks. Good.

**⑤ WiFi.** The 2.4 GHz warning is prominent and correctly worded. She clicks 掃描 Scan, picks her SSID, types the password, clicks 送出憑證. The device is in `runProvisioningMode()` (fresh flash, no creds — `main.cpp:2496–2504`, reason 「首次設定」), which pumps Improv from its `for(;;)` (`:2334`), so it answers. On success the site says:

> 已連上「陳家」。憑證已寫入裝置。 · *Joined "陳家". Credentials saved on the device.*

step 2 gets its ✓, and the device reboots 300 ms later (`main.cpp:2343–2347`). `saveWifiCreds()` clears the NVS hash (`:1506`), so the first post-setup cycle definitely renders — UX-FLOWS G7 is genuinely fixed.

**⑥ And now — the stranding.** What is on May's screen, and what can she click?

- Step 3 is titled **「序列日誌 Serial log」**, described as 「115200 baud 的原始主控台」.
- A black console filling with `=== YAT v0.3.0-dev ===`, `[wifi] …`, `[engine] …`, `[epd] refresh (~30 s, blocking)…`.
- A text box with placeholder `YAT STATUS` and a 送出 Send button.
- A six-row table of protocol commands including `YAT PUT <path> <bytes>` 「寫入（先驗證 JSON 才落盤）」.
- The footer, and nothing else. **There is no step 4. There is no mention of pages anywhere on the site outside the gallery.**

Roughly 40 s later the panel shows 現時天氣 for 沙田 — because `DEFAULT_CONFIG_JSON` (`main.cpp:330–338`) seeds exactly one page, `{"id":"weather","pack":"hko-now","params":{}}`, whose own default district is 沙田. **May's first wish is granted by coincidence.** If she lived in 屯門 there would be no browser path to change it.

Her other two wishes have no path at all:

- **969 (Citybus, TSW ↔ Wan Chai)** needs `commute-combo` installed and configured with `ctb_stop` — *"6-digit Citybus stop ID from the rt.data.gov.hk v2 route-stop API. Ask your agent to look it up."*
- **Sushiro** needs `sushiro-queue` installed with `latitude` / `longitude` in decimal degrees.

Neither pack is on the device, and nothing in the browser can put it there. The site's own protocol table advertises `YAT PUT`, but **the site's console box cannot perform a PUT**: `sendForm` calls `session.sendLine()` (`tools.js:749–756`), which appends `\n` and returns (`:214–216`), while the firmware immediately calls `Serial.readBytes()` for exactly the declared byte count (`main.cpp:1086–1092`). There is no code path in the browser that writes a file body. The one command that would change what the device shows is documented on the page and unimplementable from it.

**Time to give up: about four minutes after the WiFi succeeds.** She has a working weather clock she cannot change, and a black terminal she was never meant to see.

---

## 2. The stranding map

Ranked by how early it stops her × how completely.

| # | Where she stops | What she sees | Cause | Tag |
|---|---|---|---|---|
| **S1** | Step 1, today | 「仲未有任何發佈」 + "build the firmware yourself with PlatformIO" | No release tagged; empty-state copy addresses makers | site / release |
| **S2** | Mid-flash | 「失敗 failed」, unrecognised error, no matching troubleshooting row | `FLASH_BAUD = 921600` (`tools.js:22`) vs `platformio.ini:45` | **site, 1 line** — ✅ **Resolved 2026-08-05:** `FLASH_BAUD` is `460800` (the website's `tools.js:47`, private codebase), matching `platformio.ini:75`, hardware-verified |
| **S3** | **After WiFi succeeds — the main event** | A serial console. No page chooser exists anywhere | Site has three steps; PRD §4.1 step 5 ("starter pages") is unbuilt | site |
| **S4** | Gallery | 16 beautiful cards, each ending in a `JSON` link to GitHub | `card()` (`gallery.js:41–98`) builds head/shot/desc/chips/meta — no install button, no device connection, no form | site |
| **S5** | Anything voice | Nothing on the site mentions voice; the only path is `YAT SECRET stt <key>` (`main.cpp:1244`) | No key field, no signup guide | site + product |
| **S6** | A pack that needs an ID | "Ask your agent to look up stop IDs" (`commute-combo`), decimal degrees (`sushiro-queue`), 「HKO temperature station… as used by the tc feed」(`hko-now`) | Param descriptions written for agents/makers | packs |
| **S7** | Safari / iPad / phone | 「此瀏覽器不支援 WebSerial」→ UF2 drag-and-drop → 「掃描它連上裝置自己的熱點門戶」 | Fallback path is maker-grade end to end | site + product |
| **S8** | A week later, device offline on the shelf | Nothing. The panel keeps showing a plausible old page | No on-device error card exists (only `drawProvisioningScreen`, `main.cpp:2141`) — UX-FLOWS **G5/G12 still open** | firmware |
| **S9** | She presses the big button to see if it's alive | An angry 330 Hz buzz for 300 ms | KEY0 tap = voice capture; no STT key → `beepError()` (`main.cpp:1899–1908`) | firmware |

S3 and S4 are the same hole seen from two pages: **the device-setup surface for pages does not exist.** Everything else is a paper cut by comparison, and §3 is the spec for filling it.

A note on how close this is: `YAT PAGES / USE / SAY / SECRET / PUT / GET / LS / RM / REBOOT` all exist in firmware (`main.cpp:1282–1307`), all reachable over the same WebSerial port the site already owns, and `tools/serial/yat.py` already proves the exact write sequence works (its `do_install()` does PUT → config read-modify-write → `PAGES`). **Every protocol piece is done. The missing thing is browser UI.** The site's own protocol table (`tools.html:179–188`) lists only six of the ten commands and omits all four that touch pages — even the maker-facing documentation on the page under-describes the firmware it talks to.

---

## 3. Step ③.5 — 「揀你想睇乜」 / *Choose what it shows*

The concrete spec for the missing surface. It slots into `tools.html` **between WiFi and the serial log**, renumbering the log to step 4 and demoting it into a `<details>` — May should never see a console unless she goes looking.

### 3.0 Preconditions and the awake problem

The console window is 10 s (`CONSOLE_WINDOW_MS`, `main.cpp:129`) but **any received byte resets the deadline** (`consoleWindow()`, `:1326–1330`). So the browser can hold the device awake indefinitely with a heartbeat — send `YAT STATUS` every ~5 s while the pages panel is open. **No firmware change is required for the happy path** (device just provisioned, therefore awake and cycling).

For a device that has been asleep on a shelf, serial cannot wake it (`tools/serial/README.md`, "Console window / wake poke"). The panel must therefore open with a plain instruction and a live indicator, not a silent timeout:

> 撳一下裝置右邊嗰個掣，等佢醒。
> *Press the right-hand button on the device to wake it — we'll notice within a few seconds.*

Recommend **KEY1** (page-forward, `main.cpp:88`), not KEY0: a KEY0 tap is now the voice gesture and error-buzzes on a device with no STT key.

State machine for the panel: `未連接` → `等緊裝置醒` (heartbeat sending, no reply) → `已連上` (STATUS parsed) → `寫入中` → `完成`.

### 3.1 Screen A — 你部機而家有咩 / *What's on your device now*

Built from `YAT PAGES` + `YAT GET /config.json`, thumbnails matched by pack id against `gallery-data.json`'s `preview` field.

```
③ 揀你想睇乜                                    ✓ 已連上你部裝置
   Choose what it shows                            Connected

   ┌──────────┐  現時天氣  Weather Now          ● 而家顯示緊
   │ [preview]│  沙田                            改設定  刪走  即刻睇
   └──────────┘

   ＋ 加多一版                     [ 由頁面庫揀 → ]
```

Row actions map to: 改設定 → Screen C prefilled from `config.pages[i].params`; 刪走 → `YAT RM` + config rewrite (with a 確定? confirm); 即刻睇 → `YAT USE <id>` (instant, no reboot, `main.cpp:1174`) plus the honest line 「電子紙要大約 30 秒先轉到」.

### 3.2 Screen B — 揀一版頁面 / *Pick a page*

The gallery cards, unchanged, plus **one button per card**: 「安裝到裝置 Add to my device」. Two states only:

- Device connected (session shared from `tools.js`) → enabled, opens Screen C in place.
- Not connected → the button reads 「先連接裝置 Connect your device first」and links to `tools.html#step-pages`, returning to the same card afterwards.

Packs that cannot work for her must say so on the card rather than failing later: `stock-ticker` declares a `secrets.api_key` (`sent_to: ["finnhub.io"]`) and **the engine rejects `secrets.*` outright** (`engine/src/text.cpp:568`) — nothing on the device can resolve it. Card badge: 「需要開發者設定 Maker setup required」, install disabled.

### 3.3 Screen C — 填返啲資料 / *Fill in the details*

This is the form `tools/preview/simulator.py:63–89` already builds, made human. The schema is *designed* for it — `depends_on`, `implies`, `enum_titles`, `format:"date"` all carry `"Gallery-only"` comments in `schema/yat-pack.schema.json`, and PACK-SPEC §3.1/§3.2/§3.3 specify form behaviour normatively. Rendering rules:

| Param shape | Control | Notes |
|---|---|---|
| `enum` + `enum_titles` ≤ 5 | radio group | zh-Hant label large, en small beneath. Never show the stored value (`TPK`, `c_expressnews_clocal`, `10`) |
| `enum` + `enum_titles` > 5 | searchable select | same |
| `boolean` | toggle, `title` phrased as a question | |
| `integer`/`number` with `minimum`/`maximum` | stepper + plain range 「3 至 10 條」 | never a bare number box |
| `string` + `format:"date"` | native date picker | `countdown.date_1` already declares it |
| `array` of scalars with `items.enum` | checkbox list, `minItems`/`maxItems` as bounds | PACK-SPEC §3.2: multi-select is normative. `news-sections.sections` |
| `array` of objects | repeatable rows, one mini-form each, ＋/－ buttons, `maxItems` enforced | `commute-combo.kmb_stops`, `family-board.notices` |
| `depends_on` | field hidden until the boolean is on | |
| `implies` | target auto-set **and locked**, with 「跟住上面自動填好」 | |
| optional + `default` | prefilled, never blank | PACK-SPEC §3.3(1): install materialises defaults |

**Hard rules.** No JSON text input anywhere (the simulator's `<input type=text value='["AAPL","MSFT"]'>` at `simulator.py:79` is the single thing that must not be ported). No field whose correct value is an opaque ID she must obtain elsewhere — see §4. Every label and helper bilingual — which today requires a schema change, §9.

Above the fields, the truthful machine-derived summary PRD §4.6 promises, in her register:

> 呢一版會向 **data.weather.gov.hk** 攞資料。除此之外唔會send你任何嘢去任何地方。
> *This page reads from data.weather.gov.hk. Nothing of yours is sent anywhere else.*

(`gallery-data.json` already carries `hosts` per pack.)

### 3.4 Screen D — 想點叫佢 / *What to say to it*

Prefilled removable chips from the pack's `aliases` (`{en, zh-Hant, jyutping}` — every pack in the `yat-packs` gallery has all three), plus 「＋ 加一個叫法」free text. Writes `config.pages[i].aliases`, which `yat.py install --aliases` already merges the same way.

When no STT key is set (`YAT STATUS`'s `voice_ready:false`, `main.cpp:1149`), show the section greyed with:

> 而家仲未設定聲控，呢啲字暫時用唔到 —— 但可以先填定。
> *Voice isn't set up yet, so these words don't do anything yet — you can still fill them in.*

Never hide it: this is where the voice feature becomes discoverable at all.

### 3.5 Screen E — 睇下先 / *Have a look first*

Best-effort, never blocking. The WASM engine is already shipped and wired (`gallery.js:126–190`, `assets/engine/yat-engine.js`). Packs with `inline` sources (`family-board`, `countdown`) render exactly. Packs needing a live API will usually be CORS-blocked in the browser: fall back to the pack's golden preview with 「呢張係示範圖，未計你頭先填嘅嘢」 rather than an error. Do not gate 安裝 on the preview.

### 3.6 Screen F — 寫入裝置 / *Write it to the device*

A five-line progress list in plain language, mapping to the sequence `yat.py do_install()` already proves:

| Shown to May | Wire |
|---|---|
| 傳緊頁面過去… | `YAT PUT /packs/<packid>.yat-pack.json <n>` + exactly *n* bytes, immediately (`main.cpp:1086`), ≤65536 (`:1071`) |
| 讀返部機而家嘅設定… | `YAT GET /config.json` → `OK <size>` + *size* raw bytes |
| 加咗你揀嘅頁面… | merge `{id, pack, params, aliases}` into `pages[]`, preserving unknown keys |
| 寫返落去… | `YAT PUT /config.json <n>` + bytes (device JSON-validates before writing, `:1097–1101`) |
| 重新開機… | `YAT USE <id>` then `YAT REBOOT` |

Ending, which is the *whole point of the product* and must be said out loud:

> 搞掂。部機而家自己重新開機，大約 40 秒之後畫面就會轉。
> 之後你可以拔咗條線，擺佢上櫃。佢會自己更新，唔使再理。
> *Done. It's restarting; the screen changes in about 40 seconds. Unplug it and put it on a shelf — it updates itself from now on.*

### 3.7 Code changes this needs

| File | Change | Size |
|---|---|---|
| the website's `tools.js` | `SerialSession`: `command(line)` returning the first non-`[`-prefixed reply line (the firmware's `LOGF` interleaving quirk, `tools/serial/README.md`); `getFile(path)` with a **byte-count read mode** (today `_text()` dumps everything to the log, `:283–287`); `putFile(path, bytes)` writing header + body with no gap; 5 s keepalive timer | ~150 lines |
| the website's `pages.js` *(new)* | Screens A–F, form builder from a params schema | ~450 lines |
| the website's `tools.html` | New `<section id="step-pages">` as step 3; serial log demoted to step 4 inside `<details>`; protocol table updated to all ten commands | ~60 lines |
| the website's `gallery.js` | Per-card 安裝到裝置 button + shared form module | ~60 lines |
| the website's `generate.py` | **Emit the raw `params` schema and `secrets` block per pack.** Today `param_chips()` flattens each param to `{key, label, hint:"18 options", required, default}` and the card truncates at five — the enums, `enum_titles`, bounds, `depends_on` and `implies` are all thrown away, so the site as shipped *cannot* build a form from its own data file | ~30 lines |

(This table names files under this repo's former `site/`, now the separate private website codebase.)

No firmware change is required for ③.5 itself. (§8 fix 5 is a robustness improvement, not a prerequisite.)

---

## 4. Which packs would confuse her — named

The good news first, because it inverts the expected finding: **the packs already carry bilingual `enum_titles`** — `tides` (長洲, 大埔滘…), `aqhi` (中西區, 銅鑼灣（路邊）…), `headlines-simple` (本地（中文）…), `sushiro-queue.fav_store` (尖沙咀加連威老道店…), `gmb-minibus.region` (港島/九龍/新界), `news-sections.sections`. Those are exactly right for May, and the site currently throws all of them away (`generate.py` `param_chips` → `hint = "18 options"`). Fixing the pipeline surfaces work that is already done.

What is genuinely wrong, pack by pack:

| Pack | Param | Description as written | Why it fails May | Fix |
|---|---|---|---|---|
| **`hko-now`** | `district` | *"HKO temperature station, Chinese name as used by the tc feed, e.g. 沙田, 香港天文台, 屯門."* | Free text with no `enum`. She must know that 「大圍」 is not a station and 「香港天文台」 is. **This is the default page** — the single most important param in the product | Add `enum` of the ~24 HKO station names with `enum_titles` (values are already Chinese). Title 「地區」/"District". Description: 「揀最近你屋企嗰個」 |
| **`commute-combo`** | `kmb_stops[].stop_id` | *"16-char KMB stop ID (data.etabus.gov.hk stop list)… **Ask your agent to look up stop IDs.**"* | Instructs her to ask an agent she does not have. Pattern `^[0-9A-F]{16}$` | §9 — needs a stop picker, not a text field |
| | `ctb_stop` | *"6-digit Citybus stop ID from the rt.data.gov.hk v2 route-stop API. Ask your agent to look it up."* | **This is the 969 blocker.** | same |
| | `mtr_line` / `mtr_sta` | *"3-letter line code… e.g. TKL, TCL, EAL"* / *"3-letter station code… e.g. LHP, TIK, OLY"* | Two coupled code fields where a wrong pair silently returns nothing | Enum + `enum_titles` (港島綫/將軍澳綫…) + `implies` to lock station to line — the schema supports exactly this and the pack does not use it |
| **`gmb-minibus`** | `stop_seq` | *"1-based stop sequence in the chosen direction. Ask your agent to list the stops for your route and direction."* | She would have to count stops on a route map | Needs a lookup; until then, mark the pack maker-only in the gallery |
| | `route_seq` | *"1 = first listed direction, 2 = reverse. Check which is which in the route info"* | `enum_titles` say 「第一個方向/相反方向」— still meaningless without the route info | Fold into the stop picker |
| **`sushiro-queue`** | `latitude` / `longitude` | *"Decimal degrees. Default is central Kowloon."* | She will not type 22.302 | Replace with an 18-district enum mapping to coordinates, or drop to a "near my favourite branch" mode — `fav_store` is already a perfect bilingual dropdown |
| **`temp-trend`** | `latitude`/`longitude`, `scale_min`/`scale_max` | *"Left end of the temperature bars. Use -5 for cold-climate locations."* | Chart-engineering knobs on a household page | Hide behind 「進階」; district enum for location |
| **`photo-frame`** | `image_url` | *"Any https URL serving a PNG (≤200KB, source ≤800×480 px)."*; default `https://replace-me.example.com/photo.png` | 「PNG ≤200KB」means nothing to her; the placeholder default renders as a broken frame if she installs without editing | Make `required` with **no default**, form-validate `https://…`, plain helper 「一張圖片嘅網址（.png）」 |
| **`family-board`** | `album_url` | default `https://photos.app.goo.gl/REPLACE-WITH-YOUR-ALBUM` | A `REPLACE-WITH` default that renders literally into a QR code on her wall | Gate behind `show_album` (already `depends_on`), no default, validate |
| **`stock-ticker`** | `symbols`, `secrets.api_key` | *"Finnhub symbols… e.g. BINANCE:BTCUSDT"* | Needs an API key the engine cannot resolve (`text.cpp:568`) | Mark 「需要開發者設定」in the gallery; hide install |
| **all 16** | every `title`/`description` | English-only, by schema | See §9 | schema + packs |

---

## 5. Voice: the honest options

**Where it stands.** The firmware pipeline is complete and good — KEY0 tap → `beepRecordStart()` → capture → ElevenLabs Scribe v2 (`STT_URL`, `main.cpp:1672`; `scribe_v2`, `:1799`) → `matchPageByText()` → page switch + `beepMatch()`. Every failure is one error tone and a fall-through, never fatal (`:1889–1978`). It needs exactly one thing: a key in NVS, settable only by `YAT SECRET stt <key>` (`:1244`). For May that is not "hard", it is **structurally impossible** — the command exists on no surface she can reach, and even reaching it would only move the problem to "create an ElevenLabs account and generate an API key".

| Option | What she does | Cost to her | Cost to us | Honest verdict |
|---|---|---|---|---|
| **A. Paste-a-key field in ③.5 + a step-by-step signup guide** | Follows a screenshot-by-screenshot guide, signs up, copies a key, pastes it | ~10 min, one account, a credit card on the paid tier | ~0.5 d site + ~0.5 d guide | Works. Contradicts 「沒有帳戶要開」— but only for the one optional feature, and only if we say so plainly |
| **B. Ship voice as maker-only until a keyless path exists** | Nothing. KEY1/KEY2 cycle pages | Zero | Zero. Delete voice from her surfaces; keep `YAT SECRET` for makers | Truthful, cheap, and loses the feature PRD §3 names as a competitive differentiator |
| **C. On-device Cantonese keyword spotting** | Nothing | Zero | Large — PRD §4.4 already defers wake-word to v2 for exactly this reason | Right answer eventually, not a v1 answer |
| **D. Community-funded shared key** | Nothing | — | — | **Not acceptable — see below** |

**Why D is out, stated once so it stays out.** A shared key is a server by another name: someone holds the credential, pays the bill, and can see or be compelled to produce the traffic. It breaks the project's load-bearing claim — 「沒有伺服器」/ "no backend to operate at all" (PRD §1, §6 non-goals) — and it does so on the one data path that carries **audio recorded inside people's homes**. It also collapses under the first abusive user, the first quota exhaustion, and the first ToS review, and every one of those failures lands on households who never agreed to be part of a pool. There is no version of this that is only slightly a server.

**Recommendation: A, framed as B.** Build the paste-a-key field in ③.5 (it is `YAT SECRET stt <value>` over a session the site already holds — an afternoon), and put voice behind a clearly optional, collapsed panel that opens with the truth:

> **聲控（可以唔設定）** — 按掣講「天氣」就轉版。
> 呢個功能要用 ElevenLabs 嘅語音辨識，佢哋要你開一個帳戶攞一條密碼匙。除咗呢一項，YAT 由頭到尾都唔使開任何帳戶。
> 唔想開？完全冇問題 —— 用裝置上面嘅掣一樣可以揀版。
>
> *Voice (entirely optional) — press the button, say 「天氣」, the page changes. It needs an ElevenLabs account for the speech recognition. This is the one and only thing in YAT that asks you to sign up for anything. Don't want to? The buttons do the same job.*

Then the guide, with screenshots, ending in the paste field. And **make the no-key case non-hostile in firmware** (§8 fix 6): a KEY0 tap on a device with no key should not answer with a 330 Hz error buzz that reads as a fault.

---

## 6. When it goes wrong, browser-only

**Wrong WiFi password.** Traced: `improvConnectCb` runs `WiFi.begin` and blocks 15 s (`main.cpp:2198–2210`); on failure the library sends `STATE_STOPPED` then `ERROR_UNABLE_TO_CONNECT` (0x03) (`ImprovWiFiLibrary.cpp:90–93`); `provision()`'s 45 s waiter catches the error packet (`tools.js:666–670`) and shows:

> 無法連接該網絡 —— 檢查密碼，並確認這是 2.4 GHz 網絡。

**This is the best-handled failure on the site** — correct, actionable, bilingual, and retryable (no state guard blocks a second attempt). Two refinements: the console simultaneously logs `[improv] state 已停止 / Stopped`, which contradicts the friendly message for anyone who looks; and the *timeout* branch (`:684–689`) advises 「按住 KEY0 五秒」 for a device that is in fact sitting in provisioning mode, which is wrong advice for the state it fires in.

**Unplugged mid-flash.** `doFlash()` catches, logs 「失敗 failed: …」, sets the label to 「失敗」, reverts the step to `ready`, and `transport?.disconnect()` runs in `finally` (`tools.js:522–535`). But `state.port` still holds the dead `SerialPort` (only cleared on `NotFoundError`, `:526–529`), and the `disconnect` listener is attached in `openSession()` (`:557`), which never ran during a flash — **so nothing tells her the device is gone**, and clicking 安裝 again reuses the stale port and fails differently. There is no 「重新整理頁面」 instruction anywhere. Fix: clear `state.port` on any flash failure, attach the disconnect listener to `navigator.serial` once at init, and on disconnect show 「裝置甩咗線。插返好，然後重新整理呢一頁。」

**Safari / iPad / phone.** The dateline reads 「WebSerial 不可用 · unavailable」 and the notice (`tools.html:55–57`) says flashing needs desktop Chrome or Edge, then offers: download `firmware-e1002.uf2`, double-tap RESET, find a drive called `XIAO-BOOT`, drag the file, then 「用裝置上的 QR 熱點門戶設定 WiFi」. Count the terms May cannot parse: WebSerial, UF2, RESET 掣, XIAO-BOOT, flash offset `0x0`, 熱點門戶. **Comprehensible: no.** And the fallback is a dead end regardless — even completed, it lands her in the phone-portal dance the new bar rules out, with still no way to choose pages. The honest messaging for her is a stop sign, not a fallback:

> 呢一頁要用電腦版 Chrome 或者 Edge 先用到。用 Safari、或者用手機、平板都唔得。
> 揾部電腦，開 Chrome，再返嚟呢一頁。
> *This page needs desktop Chrome or Edge — not Safari, not a phone or tablet. Find a computer, open Chrome, and come back.*

Keep the UF2 section, collapsed, labelled 「進階 · 開發者用」.

**Device offline at home, a week later.** Her affordances, honestly enumerated:

1. **The panel itself: nothing.** No error card exists in firmware — `drawProvisioningScreen()` (`main.cpp:2141`) is the only non-pack screen. UX-FLOWS G5/G12 remain open, and this is where they bite a household. The device shows a plausible old page. If the fetch fails *after* WiFi is up and a snapshot exists, she gets `stale 09:15` — red, English, 8 px, next to `data.weather.gov.hk` (`render.cpp:950–957`). She cannot read it, cannot see it from three metres, and would not know what it meant.
2. **The buttons: undiscoverable and now actively misleading.** KEY0 tap → error buzz (§2 S9). KEY0 held 5 s → re-provision, but `readKey0GestureAtWake()` only samples when KEY0 *caused an ext1 wake* (`:2246–2249`) — hold it while the device is awake and nothing happens, with no feedback either way. Nothing on the device, on the site, or in any box is labelled.
3. **The site: it does teach KEY0-hold** — in a `<details>` collapsed behind 「裝置沒有回應？」 (`tools.html:153–157`) inside a step rendered at `opacity: 0.4`, and in the timeout toast (`tools.js:589`). Better than nothing, badly placed, and it names a button that has no name on the hardware.
4. **After 15 min of failures** the device self-enters provisioning (`:2496`) and its screen switches to two QR codes — sending her down the phone-portal path, *not* back to the website that set it up. The two channels give contradictory instructions.

The minimum honest recovery story for a browser-only owner: an on-device card that says, in Chinese, what is wrong and **to bring it back to the computer and open the website**; and a 「部機好似有問題」 diagnostic panel on the site that reads `YAT STATUS` and translates it (`voice_ready`, `next_wake_s`, `battery_mv`, `page_id`) into sentences.

---

## 7. The e-paper's own language

Every string May can ever see on the panel, quoted from source.

| String | Where | Verdict |
|---|---|---|
| `設定 WiFi   Set up WiFi` | `main.cpp:2147` | ✅ Perfect register |
| `掃描加入設定網絡` / `Scan to join setup Wi-Fi` | `:2167` | ✅ |
| `然後掃描開啟設定頁` / `Then open the setup page` | `:2168` | ✅ |
| `只支援 2.4GHz WiFi · 2.4 GHz networks only` | `:2122` | ✅ Right words, right place |
| `網絡 Network: YAT-4F2A    密碼 Password: xxxxxxxx` | `:2175` | ✅ |
| `首次設定 / no WiFi credentials` | `:2501` | ✅ |
| `連線失敗 3 次 / 3 consecutive WiFi failures` | `:2502` | ⚠️ Accurate, but it is a *count*, not an instruction. She learns nothing about what to do |
| **`KEY0 長按 / KEY0 held 5s`** | `:2500` | ❌ **Names a button that is not labelled on the hardware.** The device tells her a thing she cannot act on and cannot look up |
| `fw 0.3.0-dev` | `:2183` | ⚠️ Harmless, meaningless to her |
| `更新 07:12 / upd 07:12` | `render.cpp:934–937` | ✅ The G16 fix works — 「更新」 is exactly the right word |
| `data.weather.gov.hk` | `render.cpp:947` | ⚠️ Provenance is a brand value, but a bare hostname reads as an error to her. 「資料來自 天文台」 would carry the same trust in her language |
| **`stale 09:15`** | `render.cpp:956` | ❌ English-only, 8 px, red. The one glyph whose entire job is telling a household "don't trust this number" is the one she cannot read. UX-FLOWS G18, still open. `舊資料 09:15` costs nothing |
| Battery glyph | `render.cpp:888–890` (HEAD `f34fc74`) | ⚠️ Engine-side landed; **`main.cpp` never calls `setBatteryPercent()`** — grep is empty. The glyph draws nothing on the actual device — ✅ **Resolved 2026-08-05:** `firmware/src/main.cpp:764` now calls it every cycle, fed from `batteryPercent(battVolts)` at `:317`; the glyph draws (`engine/src/render.cpp:1471`, `:1868`) |
| *(no error card exists)* | — | ❌ The largest gap in the on-device language: the device has no way to say 「我上唔到網」 |

Nothing on the panel says "NVS", "pack", "config" or "JSON" — the on-device vocabulary is in better shape than the website's. Two blemishes (`KEY0 長按`, `stale`) and one absence (any error card) are the whole list.

---

## 8. Top-8 fixes, ranked by (unblocks-May × 1/effort)

| # | Fix | Why it ranks here | Effort | Tag |
|---|---|---|---|---|
| **1** | `FLASH_BAUD` 921600 → 115200 in `tools.js:22` | The repo already documents 921600 failing on this bridge (`platformio.ini:45`). If it reproduces, *every* browser flash fails at step 1 with an error no troubleshooting row explains. Verify on hardware; the fix is one line | **10 min** + 1 hardware test | site — ✅ **Resolved 2026-08-05:** landed at `460800`, not `115200` (hardware-verified, ~4× faster than the fallback this row proposed) |
| **2** | **Build step ③.5** (§3) | The entire second half of the bar. Without it the website flashes a device and hands her a terminal. Nothing else on this list matters if this is absent | **4 d** (3 d `pages.js` + 0.5 d `tools.js` protocol layer + 0.5 d `generate.py`) | site |
| **3** | Per-card 安裝到裝置 in the gallery + emit the raw params schema from `generate.py` | Turns 16 catalogue cards into 16 installable pages, and surfaces the bilingual `enum_titles` the packs already carry. Shares the form module with #2 | **1 d** (on top of #2) | site + tooling |
| **4** | Bilingual `title`/`description` in the params schema, then fix all 16 packs | Every form label she reads is English-only *by schema* today (`$defs/scalarParam.title: {"type":"string"}`). Blocks #2 from being usable in Chinese | **1 d** | packs + schema + docs |
| **5** | Human-usable params for the packs she actually wants: `hko-now.district` → station enum; `commute-combo` stop pickers; `sushiro-queue` district instead of lat/long | Two of her three wishes are blocked on opaque IDs, and the third only works by coincidence of the default. `hko-now.district` alone is ~1 h and covers the default page | **2 d** (0.25 d for `hko-now` alone) | packs — ✅ **Resolved 2026-08-05 (hko-now only):** `district` now has bilingual title/description plus a 24-station `enum`/`enum_titles` in `../yat-packs/official/hko-now.yat-pack.json`. `commute-combo` and `sushiro-queue` not verified. |
| **6** | Voice: paste-a-key field in ③.5 + signup guide + stop error-buzzing a keyless KEY0 tap | Makes the flagship feature reachable at all, and stops the device's most obvious button from sounding broken out of the box | **1 d** site + **0.25 d** firmware | site + firmware |
| **7** | On-device error card ("我上唔到網 / can't reach the internet — 拎返去電腦，開 yat.hk") | Her only recovery channel when the shelf device dies. UX-FLOWS G5/G12; `prov::` drawing helpers already exist. Also: wire `setBatteryPercent()`, and make the stale glyph `舊資料 09:15` | **1.5 d** firmware + **0.25 d** engine | firmware + engine |
| **8** | Site honesty pass: Safari message as a stop sign not a fallback; clear `state.port` + a global disconnect handler; protocol table → all ten commands; empty-release copy for non-makers | Four independent paper cuts, all under an hour each, all in copy or a few lines of JS | **0.5 d** | site |

Fixes 1, 4, 5(partial) and 8 total under two days and are worth doing regardless of when #2 lands. Fixes 2+3 are the product.

**Re-ranking after the parallel build (§11).** #2's protocol layer and form builder have landed, which moves roughly 1.5 d of #2 into the done column — and promotes **#4 and #5 from "important" to blocking**: `pack-form.js` renders `title` and `description` verbatim, so a finished form still shows May 「District」 and *"Ask your agent to look up stop IDs."* The cheapest path to a demonstrably non-technical setup is now #4 (1 d) + `hko-now`'s enum from #5 (0.25 d), not more site code.

---

## 9. Pack and schema content changes

**The blocking schema change.** `$defs/scalarParam` and `$defs/arrayParam` declare:

```json
"title":       { "type": "string", "maxLength": 64 },
"description": { "type": "string", "maxLength": 200 }
```

Plain strings — so a param label **cannot be written in Chinese without abandoning English**, on a product whose every other user-facing string is bilingual. Note the inconsistency inside the same file: `enum_titles` items are `{en required, zh-Hant optional}`, and the pack's top-level `description` is likewise an object (PACK-SPEC §2). Param titles are the only human-facing strings in the format that are English-only, and they are the ones a non-technical owner reads most.

Change both to the same `{en, zh-Hant}` shape as `enum_titles` (accepting a bare string as the `en` form keeps every existing pack valid), then sweep all 16 packs. Downstream: `generate.py` `param_chips()` picks up `zh-Hant` for free (its `local(..., "zh-Hant")` call already exists and silently falls back today), PACK-SPEC §3.1/§3.2 tables, `skills/pack-developer`, and the pack-authoring prompt so new packs are born bilingual.

**Description rewrites — audience change, not translation.** Today they are notes from one engineer to another. Three that must change and one worked example:

- `commute-combo.kmb_stops`: *"…**Ask your agent to look up stop IDs.**"* — the pack tells the user to consult a coding agent. If the gallery form is the primary surface, no description may ever reference an agent, a CLI, an API doc, or a repo path. Grep for `Ask your agent` before shipping (three hits today: `commute-combo` ×2, `gmb-minibus` ×1).
- `hko-now.district`: *"HKO temperature station, Chinese name as used by the tc feed, e.g. 沙田"* → `title: {zh-Hant:"地區", en:"District"}`, `description: {zh-Hant:"揀最近你屋企嗰個。", en:"Pick the one nearest you."}`, plus the `enum` + `enum_titles` that turn it from a spelling test into a dropdown.

  **Resolved 2026-08-05:** exactly this — `../yat-packs/official/hko-now.yat-pack.json`'s `district` param now carries `title`/`description` as `{en, zh-Hant}` and a 24-entry `enum`/`enum_titles`.
- `stock-ticker`: declares a secret the engine rejects outright (`engine/src/text.cpp:568`). Either mark it maker-only in gallery metadata or move it out of `yat-packs`' `official/` (into `community/`, say) until pack-scoped secrets are wired end to end — a card that cannot install is worse than a card that isn't there.
- Defaults containing `REPLACE-WITH-YOUR-ALBUM` / `replace-me.example.com` (`family-board`, `photo-frame`) must become required-with-no-default, so the form asks instead of the panel rendering a placeholder onto her wall.

**What not to change.** The `enum_titles` work across `tides`, `aqhi`, `headlines-simple`, `news-sections`, `sushiro-queue`, `gmb-minibus`, `commute-combo` is already exactly right for this audience. The `aliases` blocks (en / zh-Hant / jyutping on all 16) are ready for Screen D as-is. The packs are ahead of the website here; the fix is in the pipeline, not the content.

---

## 10. What this means for PRD §2

The amended §2 is achievable — the protocol layer, the engine, the WASM preview, the pack metadata and the WebSerial session all exist and work. But as of `f34fc74` the sentence *"the owner configures via the website"* describes a website that can flash firmware and join a WiFi network, and then stops. **The owner's configuration surface is one step (③.5) that has not been built**, and everything else in this document is either a paper cut around it or a consequence of its absence.

Two PRD statements should be corrected now rather than after someone tests them:

1. **§4.1 step 5** ("Starter pages → same page: pick weather district, add bus stops / MTR station (guided lookup), choose language… Optional here: voice key") describes §3 of this document. It is unimplemented, and the "guided lookup" clause is load-bearing: without it, `commute-combo` and `gmb-minibus` cannot be configured by anyone who is not running an agent.
2. **§4.1's browser fallback** ("Unsupported browser → UF2 fallback flash; provisioning falls back to the QR captive portal") is a maker path presented as a user path. Under the amended §2 it is not a fallback; it is a different product for a different person, and the site should say so.

---

## 11. Acceptance check against the in-progress build

Read at 22:09 on 2026-07-28, and it moved twice while this section was being written — `device-setup.js` and a rewritten `tools.html` landed at 22:08. Treat every line number here as a moving target and the findings as a checklist, not a scorecard. **The surface now exists**: `tools.html` has a step 4 「揀頁面 Choose what it shows」 and a step 5 「用講嘢揀頁面（可以唔用）」, driven by `device-setup.js` (page list, picker, installer, restart) on top of `yat-serial.js` + `pack-form.js`. §1–§2's walk is superseded from step ⑥ onward; §2's S3/S5 are being closed as this is written.

### What is right, and worth not re-litigating

`yat-serial.js` is a faithful port of `tools/serial/yat.py`'s reader and gets the two things that break naive implementations: `[`-prefixed `LOGF` lines are skipped rather than parsed (`:169–171`), and GET/PUT lengths come from `TextEncoder`, so a page called 壽司郎 survives the round trip (`:54–56`). `normalizeConfig` mirrors the firmware's v1→v2 migration (`main.cpp:500–518`) and preserves unknown top-level keys. `pack-form.js` uses `enum_titles` for bilingual option labels (`:26–32`) — the asset §4 found the pipeline discarding — and implements `depends_on`, `implies` with a lock-and-explain line, multi-select, and repeating rows.

**The voice panel is the strongest copy in the project.** `tools.html:245–262` independently arrives at §5's recommendation and executes it better: optional stated first (「唔設定都完全用得」), the microphone's actual duty cycle, the named third party, the free-tier caveat with pricing deferred to them, and 「條鎖匙淨係入喺裝置自己度… 唔會經過任何網站」. That is the honest version of the one signup in the product. Ship it as written.

Two other correctness wins worth naming, because both are non-obvious: the installer detects an already-installed page and says 「今次會更新佢嘅設定」 rather than silently duplicating; and `device-setup.js:199`/`:425` already handle the `YAT PAGES` boot-snapshot trap with 「呢一版係啱啱加落去，裝置要重新開機先識得佢」 instead of surfacing `ERR no such page`.

### Blocking defects

**B1 — The form is built and still English-only.** `labelFor()` (`pack-form.js:46–55`) asks for `local(spec.title,"zh-Hant")`, but **every pack's `title` is a plain string** by schema (`$defs/scalarParam.title: {"type":"string"}`), so the call returns the English text, `zh === en`, the Chinese prefix renders empty, and the label is 「District」/「Latitude」/「KMB stops」. The module's own header comment concedes it: *"the schema's own `title` is English, so enum options are the one place a pack hands us bilingual copy."* §9's schema change is no longer a polish item — **it is the thing standing between a finished form and a usable one.**

**Resolved 2026-08-05:** the schema change landed — `paramTitle` and `paramDescription` are now `oneOf` a plain string (legacy, en-only) or `{en, "zh-Hant"?}` (`schema/yat-pack.schema.json:187–205` and the sibling `paramDescription` def) — and at least `hko-now`'s `district` param has been rewritten to use it: bilingual `title`/`description` plus a 24-station `enum`/`enum_titles` (`../yat-packs/official/hko-now.yat-pack.json`), closing §9's fix example and §4's "this is the default page" concern. Not verified: whether every other pack in `yat-packs/official/` has been swept the same way (§4's table lists several more — `commute-combo`, `sushiro-queue`, etc.).

**B2 — `spec.description` is rendered verbatim** (`pack-form.js:236`). Under 「KMB stops」 May will read *"Each entry: 16-char KMB stop ID (data.etabus.gov.hk stop list)… Ask your agent to look up stop IDs."* The form faithfully delivers maker-ese to the person the bar was written for. §4's rewrites are a dependency of this module shipping, not a follow-up.

**B3 — The STT key is echoed into the on-screen log, and this is now live.** `secret()` goes through `_sendLine()` (`yat-serial.js:368–374` → `:208–211`), which calls `onLog("> YAT SECRET stt sk_…")`. Step 5's password field feeds exactly this path (`device-setup.js:616`), so the key the panel just promised 「唔會經過任何網站」 gets printed into step 3's visible console — the panel people screenshot when asking for help. Redact in `_sendLine`, or give `secret()` its own writer that logs `> YAT SECRET stt ••••`. **Highest severity-to-effort item in the new code.**

**Resolved 2026-08-05:** `secret()` now masks the value before logging — `const shown = value === "-" ? value : "•".repeat(8)`, passed as a separate log-display string to `_sendLine(line, shown)` (the website's `yat-serial.js:377–384`, private codebase) — so the console view shows eight bullets in place of the key, never the key itself.

**B4 — Item-level validation is missing on row editors.** `rowEditor.read()` drops empty cells (`pack-form.js:167`), and `validate()` only checks the array's `minItems`/`maxItems` — never the item schema's `required`, `pattern`, `minLength`. A half-filled `commute-combo` row yields `{route:"969"}` with no `stop_id`; the device accepts it (structurally valid JSON, `main.cpp:1097`), and the page renders wrong with nothing anywhere saying why. The item schema already carries `required:["stop_id","route","label"]` and `pattern:"^[0-9A-F]{16}$"` — enforce them.

**B5 — The 16-page ceiling is promised in copy and unenforced in code.** Step 4's lede now says 「裝置一次可以放最多 16 版」 (`tools.html:200`), but `mergePageIntoConfig` (`yat-serial.js:83–96`) will happily push a 17th. `parsePagesFromV2` then rejects the *whole file* at `n > 16` (`main.cpp:470`) and the device falls back to built-in defaults with the bad file left on disk — so from May's chair, **adding one page deletes all her pages.** Refuse at 16 in the browser, in her language. Same class: a page whose `id` or `pack` is empty rejects the entire config (`:475`).

**B8 — The pack-secrets warning promises a recovery that does not exist.** `device-setup.js:102–113` tells her a pack needing a key 「裝置會照裝，不過未有鎖匙之前佢會顯示『攞唔到數據』」. Both halves are wrong for the only pack this fires on: the engine rejects `secrets.*` outright (`engine/src/text.cpp:568`), so no key will ever make `stock-ticker` work, and **there is no on-device error card to display 「攞唔到數據」** (§7) — the panel will just keep showing the previous page. Block install for packs declaring `secrets` until the engine supports them, and say so plainly: 「呢一版而家仲未用得，要等韌體更新」.

**B6 — The 6 s control timeout will fire during a normal panel refresh.** `CONTROL_TIMEOUT = 6000` (`yat-serial.js:46`), but `epaper.update()` blocks the single-threaded firmware for ~30 s and serial is not read at all during it. Any command issued in that window times out and surfaces as 「裝置沒有回應」 — most likely immediately after an install-and-reboot, which is precisely when May is watching. Either raise the control timeout past 35 s or have the UI retry silently before it says anything alarming.

**B7 — No keepalive, and step 4 is exactly the slow-human path.** `wake()` exists (`yat-serial.js:227–229`) and nothing calls it periodically — no `setInterval` anywhere in `device-setup.js`. The device's console window is 10 s and extends on *any* received byte (`main.cpp:1326–1330`), so a heartbeat every ≤9 s while step 4 is open holds it awake for free. Without one, filling in a params form and editing keywords — a minute of typing, by design — reliably ends with a sleeping device on 安裝. §3.0.

**B9 — Step order still walks her through a terminal.** The console is step 3 「序列日誌 Serial log」; pages is step 4; voice is step 5. §3 specified pages immediately after WiFi with the log demoted into a `<details>`, and the reason stands: the numbered rail is the instruction, and as built it instructs May to read a serial console before she is allowed to choose a page. The 「連上網之後，落去揀頁面」 link (`tools.html:159`) patches the path but not the numbering. Renumber to pages = 3, voice = 4, log = 5-inside-`<details>`.

**B10 — The wake instruction names an unlabelled button and overshoots.** Step 4 says 「如果裝置訓咗，撳住佢上面嘅 KEY0 五秒」 (`tools.html:203`). Three problems: the hardware has no "KEY0" printed on it (§7); KEY0-hold enters *provisioning*, which wipes the panel to the QR screen and starts an AP when all that was needed was an awake device; and the gesture is only sampled when KEY0 caused an ext1 wake (`main.cpp:2246–2249`), so holding it on an already-awake device does nothing. A tap of any button wakes it into console windows. Recommend: 「撳一下裝置側邊嘅細掣（唔使撳實）」 with a diagram, plus the live 「等緊裝置醒…」 state from §3.0.

### Correctness notes that will surface as UX bugs

- **`YAT PAGES` is a boot snapshot** — `g_pages` is set once per cycle (`main.cpp:2429`), which `test-fake-device.js` models deliberately. **Already handled**: `device-setup.js:269` treats the config file as the source of truth and `:425` explains the lag in her language. Keep it that way; a later refactor that "simplifies" by trusting `PAGES` will silently reintroduce "I added a page and nothing happened".
- `scalarListEditor.read()` (`pack-form.js:94`) returns strings unconditionally — no `coerce()` against `items.type`. Latent today (no pack in `yat-packs` has a numeric scalar list), live the moment one does.
- `uniqueItems` is declared (`news-sections.sections`) and normative for row editors per PACK-SPEC §3.2, and `validate()` never checks it.
- `errFor()` (`yat-serial.js:452–463`) maps six ERR strings but not `ERR short read` — the one a byte-count bug produces, and the one worth an explicit message.
- Error copy is inconsistent: `"裝置沒有回應 no answer from the device"` is bilingual, `"a device needs at least one page"` and `"this page file has no \"id\""` are English-only. If any of these can reach the panel, they need her register.
- `packKeywords()` (`pack-form.js:35–42`) mixes jyutping (`"tin1 hei3"`) into the same chip list as 天氣 and "weather". May will not know what the third kind is; label or separate them.
- **The truthful source disclosure PRD §4.6 promises is missing.** The installer renders name, description, a secrets warning, settings and keywords (`device-setup.js:90–130`) — but never "where this page reads from", even though it already fetches `gallery-data.json`, which carries `hosts` per pack. One line, and it is the sentence that makes 「冇伺服器」 concrete at the moment she is deciding: 「呢一版會向 data.weather.gov.hk 攞資料，除此之外唔會send你任何嘢去任何地方。」
- `test-fake-device.js` is the harness only — nothing imports it and no assertions exist yet. The `silent` (deep-sleeping) mode is the right fixture for the §3.0 wake path; use it.

### The acceptance checklist

The surface is done for May when, on a device flashed from the site and with no terminal open anywhere:

1. After WiFi succeeds, the next thing on screen is a page chooser — not a console. The console is behind a `<details>`.
2. Every label and helper she reads is Chinese-first. **No screen contains the word "agent", "API", "ID", "JSON" or a hostname presented as an instruction.**
3. She can install 天氣 for 屯門 (not just the seeded 沙田), pick a bus stop without typing a code, and see her Sushiro branch by name.
4. She is told, before install, where the page fetches from — in one sentence, in Chinese.
5. Install ends with 「拔咗條線，擺佢上櫃」 and the panel changes within ~40 s.
6. Filling in a form slowly never produces a timeout error.
7. Adding a 17th page is refused politely; it never empties her device.
8. The STT key never appears in any log view.
9. A pack that cannot work (declares `secrets`) is refused at the card, not installed with a promise of a fetch-failed screen that the firmware cannot draw.
10. Every string in §7's table that is marked ❌ is fixed, or May has a browser path that makes it unnecessary.

Items 2, 3 and 4 are the ones no amount of site code can satisfy alone: they need the schema change (§9), the pack param rewrites (§4), and one line of disclosure copy. **The site half of this bar is nearly built. The pack half has not started.**
