# YAT — Product Requirements Document

**Version:** 0.4 (revised against a working device — the setup surface moved)
**Date:** 2026-08-03
**Status:** Implementation under way. §4 has been rewritten against what the firmware and the website actually do (`firmware/src/main.cpp`; the website at https://yat.day is a separate, private codebase); where this document and the repo disagree, the repo wins. Requirements that are still only requirements are marked **(not built yet)** rather than quietly dropped.
**Companion docs:** [BRAND.md](BRAND.md) · [ARCHITECTURE.md](ARCHITECTURE.md) · [ROADMAP.md](ROADMAP.md) · [docs/SETUP.md](docs/SETUP.md)

---

## 1. Overview

YAT is an open-source ambient e-ink display platform built for everyday life in Hong Kong, running on off-the-shelf hardware (initially the SeeedStudio reTerminal E1002). It shows the information a Hong Kong household actually checks — weather and warnings, bus and MTR arrival times, headlines — on a calm, battery-powered e-ink screen that updates itself and otherwise stays out of the way.

There is no company, no account, no subscription — **and no server, anywhere**. Not a hosted one, not a self-hosted one. The firmware contains a spec engine that fetches public APIs directly, extracts the data, and renders pages on the device itself. Users buy the device (~US$99 sale / US$109 list, indicative, checked
2026-08-05) and flash it once from a web page; from then on the device serves its own settings page, reached from a phone by holding the green button and scanning the QR that appears on the panel. Pack authoring is conversational, with an AI coding agent (Claude Code, Codex). The only thing the YAT project hosts is a static website.

**Tagline:** Your day, quietly displayed.

### What YAT is

- A **personal information display** that lives on a shelf, desk, or fridge and updates on a schedule.
- **Hong Kong-first**: bilingual (Traditional Chinese / English), aware of typhoon signals, rainstorm warnings, KMB/Citybus/MTR arrivals, and local news.
- **Fully standalone**: the device talks straight to public data APIs over home WiFi. Nothing else to run, fund, or keep alive — for the user *or* the project.
- **Open and extensible**: every widget page ("content pack") is a single declarative JSON file anyone can write — or ask an AI to write — and install without reflashing.
- **Calm technology**: no glow, no notifications, no scrolling. Weeks of battery life.

### What YAT is not

- Not a hosted service. There is no yat.hk cloud, no signup, no fees — and no backend to operate at all.
- Not a smart speaker or chatbot. Voice switches pages, and — on the one pack that has a list to change — adds an item, ticks one off, or tidies away the ticked ones (§4.4). That is the entire output space, for the optional language model too: it may name a page the household already installed, or one of those three operations on one list. There is nothing to ask it, and nothing it can answer.
- Not a general tablet replacement. The screen takes ~30 seconds to refresh; YAT embraces this instead of fighting it.

---

## 2. Audiences

(From BRAND.md, prioritized for v1.)

| Audience | v1 priority | What they need |
|---|---|---|
| **Makers / developers** | **Primary** | A device they can buy today, a flasher page that sets it up, a pack format they can extend in an evening |
| Everyday HK residents | Secondary (via makers gifting/helping) | Weather, warnings, "when's my bus", zero maintenance |
| Families | Secondary | Shared glanceable view in the kitchen/hallway |
| Local businesses | Later (v2+) | Auto-updating info displays |

**The UX bar is a person with no technical background** (decision 2026-07-28): plug the device into a computer once to flash it, then set up WiFi and pages from a phone browser — nothing else may be required. Makers matter as contributors and early adopters, but maker-grade paths (serial clients, JSON files) are fallbacks, never the answer to "how does the user do X". Anything configurable only outside a browser is a gap by definition. Within a household: **the owner configures from a phone; everyone else only ever touches buttons and voice.**

---

## 3. Competitive positioning

| | **YAT** | TRMNL | SenseCraft HMI | OpenDisplay |
|---|---|---|---|---|
| Hardware | BYO reTerminal E100x (~US$99) | $139 proprietary device (BYOD firmware exists) | Seeed devices | BYO various |
| Backend required | **None — engine runs in firmware** | Hosted SaaS (or self-host "BYOS") | Seeed cloud | Local always-on sender (Home Assistant) |
| HK content | **First-class: HKO warnings, bus/MTR ETA, zh-Hant** | Generic plugins, English-first | Generic widgets | Generic |
| CJK / bilingual | Native Traditional Chinese throughout | Limited | Partial | Limited |
| Setup | **Web page flash once, then the device serves its own settings page to your phone** | Web onboarding | Web/app onboarding | Manual, technical |
| Add a widget | Drop in one JSON file, no reflash | Server plugin | Cloud widget editor | Sender-side code |
| Voice page switching | Yes (push-to-talk v1, Cantonese-aware) | No | No | No |
| Color | 6-color Spectra 6 | Mostly mono | Varies | Varies |
| Cost to run | **$0, nothing hosted by anyone** | Subscription or self-host | Free tier cloud | $0 + your HA box |

**YAT wins on**: local relevance (nobody else renders a T8 signal takeover or a KMB ETA properly in Traditional Chinese), true zero-infrastructure (every alternative needs *something* running — a SaaS, a cloud, or a home hub), AI-native setup, and voice. It does not try to win on plugin catalog size — TRMNL has years of head start there.

### Why not build on OpenDisplay?

Considered and rejected as a foundation. OpenDisplay (GPL-v3, Open Home Foundation) is a **local BLE-push** standard: a "sender" in your home (Home Assistant is the reference) renders images and pushes them over Bluetooth to a dumb receiver. Its friction is structural, not cosmetic: it requires an always-on sender within BLE range. YAT is the opposite bet — the *device* is the smart part, and needs nothing but WiFi. Building on OpenDisplay would also drop voice (not in their protocol) and push HK content into the Home Assistant ecosystem, narrowing the audience to HA users.

The two projects share philosophy (dumb-simple operation, low power, no accounts, no telemetry) and YAT borrows what transfers: the Toolbox-style web flasher UX (§4.1) and — as a **v2 interop feature** — YAT firmware may additionally speak the OpenDisplay receiver protocol, so Home Assistant users can BLE-push images to a YAT device. Ride the ecosystem; don't build on its transport.

---

## 4. User experience

Three journeys define the product: **setting up** (§4.1), **living with it** (§4.2–4.4), and **changing it** (§4.5–4.6).

### 4.1 Setup UX — unboxing → running

Target: **under 15 minutes** from opening the box to the first page on screen. No accounts are created at any point. A computer appears exactly once — to write the firmware. Everything after that is the device's own screen and a phone, including every later change.

Two proven references on this exact hardware shaped the flasher:

- **OpenDisplay Toolbox** (https://opendisplay.org/firmware/toolbox/index.html) — connect USB, click *Install*: **esptool-js over WebSerial with automatic bootloader entry** (the browser toggles DTR/RTS; no boot-button dance). Proven on reTerminal E1001–E1004. YAT's primary flash mechanism.
- **Seeed's reTerminal E-Series Firmware Hub** (https://seeed-projects.github.io/OSHW-reTerminal-Series-E-D/) — proves the **UF2 drag-drop fallback** (double-tap reset → `XIAO-BOOT` drive → copy file) for browsers without WebSerial.

#### Before purchase (README + site landing page)

- What to buy: reTerminal E1001 or E1002 (indicative prices, official store links, what differs — color vs mono, refresh speed). E1002 is the only model in the build matrix today.
- What you need **once**: a computer running **desktop Chrome or Edge** and a USB-C *data* cable, to flash. No phone or tablet browser can do this step, and saying so before anyone plugs anything in is part of the flow.
- What you need **forever after**: a phone — any browser — and **2.4 GHz WiFi** (ESP32-S3 cannot join 5 GHz-only networks; the #1 predictable support issue).
- Optional: an ElevenLabs API key if you want voice (Cantonese-accurate STT); a DeepSeek, OpenRouter **or** NVIDIA key if you want voice to understand phrasings nobody wrote down (§4.4) — one field takes any of the three, and the device works out which it is. Both are pasted into the device's own settings page later, not here.
- Notably absent: any account signup. There is nothing to sign up for.

#### The flow — one computer step, then the panel leads

```
Step one — the only cable, on a computer (yat.day/tools.html):
1. Pick your model. Press Install. Plug the USB-C cable in only when the
   port picker opens — the entry that appears is your device. The browser
   enters the bootloader itself; no button on the device is pressed. ~1 min.
   (No WebSerial? Build/download the .uf2, double-tap RESET, drag it onto
   the XIAO-BOOT drive.)
2. The page polls until the device boots and draws its first screen (~40 s),
   then hands over. The computer's job is finished here.

Step two — the panel and a phone (yat.day/setup.html is only instructions):
3. Hold the green button until it beeps. Usually 5–10 s: the count starts
   when the device wakes, so a finger needs longer than five, and every
   string on the site and in the firmware says "until the beep", never a
   number of seconds. Three rising tones follow, then a ~30 s redraw.
4. The panel draws one of two screens.
   A. 設定 WiFi — a fresh device. QR to join the device's own YAT-xxxx
      network; its captive portal asks for the home 2.4 GHz network.
      Saving takes ~30 s during which the screen does not change at all.
   B. 揀內容 Choose what it shows — once WiFi is saved, and every time
      afterwards. A big QR for http://yat-xxxx.local, the numeric address
      for networks where .local does not resolve, and a small corner QR
      onto the device's own AP for routers that isolate clients.
5. The phone opens the settings page THE DEVICE SERVES: browse the pack
   library, fill in each pack's params, reorder, set keys and schedules.
   One Save uploads the packs, writes config.json once, and the panel
   redraws (~30 s) with a blue "setup mode" band across the footer.
6. Tap 完成 Done — the band goes and the device returns to its schedule.
   Ten idle minutes does the same thing by itself, which is also the
   answer to walking away mid-setup.
```

Two structural properties of step 5 are worth stating as requirements, not implementation detail:

- **The device serves about a kilobyte of bootstrap HTML and nothing else.** The real interface is an ES module imported at run time from the static site, so the settings UI improves for already-flashed devices without a reflash — and the same pack library appears on the site and inside the device's page. The obligation runs the wrong way round on purpose: a change to those files is live for every device already in the world, so it must work against the oldest `/api` surface still out there.
- **When the phone has no internet, the bootstrap's own fallback page still answers**, showing device status and the page list, and saying plainly that it is the phone's connection that failed and not the device. Browsing the library is what degrades.

Setup mode is refused below 3.50 V with a red **叉住電先設定 / Plug in to set up** card naming the actual voltage: an AP, a WiFi client and two full refreshes back to back is the heaviest thing the firmware ever does, and it is the one mode worth declining rather than browning out halfway through.

**(Not built yet)** A WASM preview panel inside the setup flow — the engine does compile to WASM and the gallery renders packs live with it, but the device's settings page shows each pack's pre-rendered preview, not a live render of the user's own params.

#### AI personalization

The conversation is the interface for **authoring a page that does not exist yet** — that ladder is §4.6, and it works today:

```
$ cd yat && <your coding agent>
> "Build me a pack showing GMB minibus arrivals for route 69 in Sai Kung"
```

The repo is self-guiding for this: `AGENTS.md`, `skills/pack-developer/SKILL.md` and `docs/PACK-SPEC.md` give any agent the grammar, the widget catalog and a preview CLI that renders to a PNG without hardware.

**(Not built yet)** The other half — an agent that configures a *household's live device*: "set up my YAT for Sha Tin, 272K to Kowloon Tong on weekday mornings, quiet hours 11pm–6:30am", resolving human descriptions to stop IDs, previewing each change, writing it to the device, and ending with a cheat-sheet of the voice words for *your* pages. Nothing in the repo does this. It is also worth being honest about what it now competes with: the device's settings page already resolves stops with pickers and needs no clone, no cable and no agent — so the case for the user skill is the fuzzy multi-step request, not config editing per se. [docs/SETUP.md](docs/SETUP.md) is the canonical narrative an agent should follow when helping someone through setup by hand.

#### Setup failure modes (handled in the flow, not documented away)

| Failure | Handling |
|---|---|
| Charge-only USB cable / board undetected | The port picker opens empty *by design* — plugging in while it is open is the instruction, and an entry that fails to appear is diagnosed as "this cable has no data lines" before anything else |
| Unsupported browser for flashing (Safari/Firefox/any phone) | Said plainly before the user plugs anything in, with "nothing is wrong on your end" and the borrow-a-computer suggestion; UF2 drag-drop as the manual route |
| Auto bootloader entry fails / flash stalls | Guided double-tap RESET retry, then UF2; erase-then-flash for recovery |
| Held the button, no beep | Keep holding — the count starts at wake, so seven to ten seconds from the finger's point of view. Wrong button and flat battery are the other two causes, in that order |
| Silence and a still panel after release | Expected for up to a minute: a silent WiFi + clock phase, three tones, then a ~30 s redraw. Named on the panel-adjacent instructions so nobody concludes it failed at second ten |
| 5 GHz-only WiFi | The captive portal cannot join it; the site, the panel and the portal all name 2.4 GHz as the requirement |
| Saved WiFi will not come up | The setup gesture routes to WiFi provisioning instead of the content portal, so a changed router password never strands the household on an AP that cannot fix it |
| Router isolates clients / `.local` does not resolve | The panel prints all three routes at once: `.local` name, numeric address, and a QR onto the device's own AP at `192.168.4.1` |
| Phone offline when the settings page loads | The device's built-in fallback page answers, shows status and pages, and says it is the phone's connection that failed |
| First fetch fails (API/WiFi) | Offline / no-data cards on the panel naming the step, and the issue log in the settings page (§4.5) |

### 4.2 Daily use

The device holds up to 16 pages and evaluates its own schedule (RTC + NTP). Which page is on the panel changes for four reasons and no others: an arrow button, a voice match, **auto-rotate** (off by default; 30, 60 or 180 minutes, chosen in the settings page — deliberately nothing shorter, because every advance costs a full 30-second refresh whether the data moved or not), and the warning takeover. How often the *active* page refetches is the pack's own cadence, overridable per page in the settings page. Typical day:

- 06:30 — quiet hours end; morning brief page (weather + first ETAs + headlines).
- 07:30–09:30 — commute window: transport ETA page refreshes every 10 min.
- Daytime — hourly rotation through weather / news.
- 23:30 — quiet hours; device sleeps until morning (last page stays visible — e-ink holds the image at zero power).
- **Warning takeover:** the firmware checks HKO's warning endpoint on every wake (tiny request). When T3+/red rain/black rain is up, it preempts the playlist with a full-screen warning page and tightens the wake cadence. This is the single most "Hong Kong" feature; because the check rides every wake, reaction time = the current wake interval.

If the fetched data hasn't changed since last time, the device skips the 30-second refresh entirely and goes back to sleep — no flashing for no reason. Calm by default.

### 4.3 Buttons

Three physical buttons, four gestures. The device is asleep between wakes, so every gesture is also a wake — and every one of them makes a sound, because a panel that takes 30 seconds to change cannot acknowledge anything on its own.

| Gesture | Action | Status |
|---|---|---|
| Green button (tap) | **Voice** (§4.4): high beep → speak → lower beep → STT → route → switch page, or change the todo list if that pack is installed. No key, or no WiFi yet: two soft chirps and the mic is never powered | fw 0.4-dev |
| Green button (**hold until it beeps**) | **Setup mode**: WiFi provisioning first if the saved network will not come up, then the device's own settings portal (§4.5). The one gesture for every later change | fw 0.4-dev |
| ▶ / ◀ | Page forward / backward | fw 0.4-dev |
| ◀ + ▶ together | **Help card**: your pages, which one is showing, up to three words each answers to, what the buttons do, and how to get into setup | fw 0.4-dev |

**Never "hold for five seconds."** The device only starts counting once it has woken, so a real finger needs seven to ten, and everyone who let go on a count of five concluded the button was broken. The firmware sounds one firm tone at the threshold *while the button is still down*, and every string in the product — panel, site, portal — names that sound instead of a duration.

### 4.4 Voice: page switching, and list intents (v1: tap-to-talk)

The user taps the voice button once, speaks a page keyword in Cantonese, English, or Mandarin, and the device switches to it. With the todo pack installed, the same tap can also add an item to that list, tick one off, or tidy away the ticked ones — the only three things voice can *do* rather than *show*, and the only pack that has them (see "Voice intents" below).

```
User: [taps the green button] → high beep (speak now; ~4 s window,
                                 configurable 2–8 s)
User: 「天氣」 / "bus" / 「巴士幾時到」
                              → lower beep (heard you, working)
                              → rising double-beep (matched: switching)
                                 / gentle falling pair (didn't catch that)
                                 / two soft chirps (heard you, can't act:
                                   no key, or no WiFi — mic never powered)
~30–40 s later                → the requested page is on screen
```

Honest constraint, stated everywhere we describe the feature: **the e-ink panel takes ~30 seconds to refresh — that is panel physics, not software.** The framing is *"ask, put it down, it'll be there"*. The immediate audio acknowledgment is what makes it feel responsive.

Design details:

- The device records (mic powered only for the capture window, never otherwise) and sends audio **directly to the STT API** with the user's own key, stored in the device's NVS. ElevenLabs Scribe is the engine — an earlier hardware prototype proved its Cantonese accuracy on this hardware.
- **Routing is two-tier, and the second tier is opt-in.**
  1. **Aliases, on the device.** Each pack declares words in English, Traditional Chinese and Jyutping (`["bus", "巴士", "幾時有車"]`), overridable per page by the household; the transcript is normalized (simplified→traditional, case, punctuation) and matched as a substring. Costs nothing, needs no network beyond the STT call that already happened, and is what answers 「天氣」.
  2. **A language model, only when a key is set.** Tier 1 is literal: 「而家出面凍唔凍」 names no alias and used to get the try-again tone. With a router key pasted into the settings page, one chat completion is shown the page list and the transcript and asked to name **one page id**. It cannot invent a page — the answer is looked up in the configured pages, and anything else (a hallucinated id, an apology, `NONE`) lands in the same no-match branch. It cannot fail loudly either: a dead API or an expired key degrades to keyword-only routing plus a row in the device's issue log, never a fault buzz.
- **The router key may come from DeepSeek, OpenRouter or NVIDIA, and nobody is asked which.** There is one field and no provider control anywhere: OpenRouter stamps its keys `sk-or-` and NVIDIA stamps theirs `nvapi-`, so the device reads the service off the key itself and DeepSeek is simply what matches neither. This exists because signup and payment, not capability, are what stop people — someone who cannot get a DeepSeek account should not thereby lose the feature, and NVIDIA hands out free credits to anyone with an account. A DeepSeek key defaults to `deepseek-chat` (their non-thinking endpoint, which tracks their current chat model); an OpenRouter key defaults to `deepseek/deepseek-v4-flash` (~3× cheaper than V3 per token, checked 2026-08-06 — a hybrid-thinking model, so the router's request explicitly disables reasoning); NVIDIA's catalogue carries no equivalent, so it defaults to `google/gemma-3-12b-it` — instruction-tuned, multilingual, and specifically **not** a reasoning model. The common rule across all three: this router caps the answer at 8 tokens, and anything that spends them thinking returns nothing a page id can be read out of. `/api/status` reports which provider is in use (never the key), so support can tell one service's outage from another's without asking anyone to read a credential aloud.
- **The two keys name their providers, because swapping them is the likeliest way setup breaks.** They are different kinds of key from different companies, they sit one above the other, and both arrive from a clipboard. Each card names its own provider in the heading, says outright that the other is not the same thing, and links to where that provider issues keys. If someone swaps them anyway, verify-on-save catches it before anything is written — a key checked against the wrong service comes back refused and is never stored.
- The second key is useless without the first, and the settings page says so rather than leaving someone to wonder why a device with a router key stays silent.
- **Voice intents, and the one pack that has them.** *(Parked 2026-08-07: the firmware ships this, but the todo pack is held out of the library until the voice half is tested on hardware — so no device can reach it yet. The intent layer is dormant without the pack, verified on a real device.)* Everything above answers *which page*. With the **todo** pack installed — and only then — voice can also do three things to that one list: 「加買菜」 puts an item on it, 「搞掂買菜」 ticks one off, 「清理」 tidies away the ticked ones (each with an English and a second Cantonese form: `記住`/`add`, `完成`/`done`, `清咗`/`clear`). The device rewrites that page's params in `config.json` through the same machinery the settings page's Apply uses, then renders the list *this* cycle, so the panel is the confirmation rather than a second thing to go and check. **Marking an item done is the feature this exists for**, and the design is shaped around the fact that its failure mode is not a missed command but the *wrong item ticked off*: the phrase is matched against the open items only, in either direction, and taken only when **exactly one** survives — zero or two get the same "didn't catch that" tone as an unrecognized page. There is also no voice path that removes an unfinished item; `清理` touches done items and nothing else, which is the promise the docs make and a property of the code rather than of the wording.
  - **The two tiers differ here, and honestly.** Tier 1 is keyword-only in the strict sense: the verb must be the *first* thing said, and the rest is taken literally — 「加牛奶」 works, 「幫我加樽牛奶落去」 does not. Tier 2 replaces the router's single call rather than adding one: with the todo pack installed, that same completion is shown the pages *and* the numbered open items, and must answer with exactly one of `PAGE:<id>`, `TODO:ADD:<text>`, `TODO:DONE:<n>`, `TODO:CLEAR` or `NONE`. Done is answered **by number**, never by the model re-typing an item's text, which removes the near-miss class entirely: the answer is an index into a list the firmware built, and an index outside it is refused rather than clamped to the nearest thing. Anything that fails validation — an unknown shape, a page nobody configured, an out-of-range number, an answer that ran into the token cap — falls back to tier 1 on the same transcript and then to the no-match tone. A device with no router key keeps the three literal verbs; a device with no todo page is unaffected in either tier, and its page-switching request is byte-for-byte the one it always sent.
  - **What this costs in privacy, stated where it is decided.** The todo list is the household's own text, and tier 2 sends the *open* items with every router call. That is a real widening of "the one part of YAT that leaves the house" and belongs beside the microphone disclosure, not in a footnote: the first key sends audio, the second key also sends the list. Neither is sent by a device without that key, and no item text reaches a log line at intent level — the device logs counts and indices, and the transcript it already logged.
- No wake word, no always-listening, and audio goes only to the STT provider the user chose. **This is the one part of YAT that leaves the house**, and it is stated wherever voice is described. Voice is entirely optional — without a key the microphone is never powered, and the arrow buttons cycle pages.
- **v2:** hands-free wake word ("YAT, 天氣") via on-device keyword spotting — Cantonese model training required; always-on mode for plugged-in devices. Explicitly out of v1.
- **(Not built yet)** STT provider choice. Speech-to-text is ElevenLabs, hard-coded. The *router* now takes DeepSeek, OpenRouter or NVIDIA (above); an OpenRouter or NVIDIA key also reaches any model that service carries, via a hand-edited `llm_model` in `config.json` — config-only and deliberately absent from the settings page, because choosing a model is a question about price and capability that this product's audience has no way to answer.

### 4.5 Config UX — changing what's on the screen

v1 has **no cloud config** — the device holds its own (`config.json` + pack files on LittleFS, secrets in NVS); every surface edits that.

- **Surface 1 — the device's own settings page (primary, and the one the product is designed around).** Hold the green button until it beeps; the panel prints an address and a QR; a phone opens it. The device runs the HTTP server, so there is no login, no pairing, no cable, and nothing to install — and no dependency on which browser the household happens to own. Everything is here: the pack library with each pack's params, page order, removal, the two voice keys, quiet hours, per-page cadence, auto-rotate, the beep switch, and what has recently gone wrong (below). Setup closes itself after ten idle minutes and the same gesture reopens it, any time, forever.
- **Surface 2 — conversation, for authoring.** Point a coding agent at the repo and describe the page you want; the developer skill (`skills/pack-developer/SKILL.md`) writes the pack, validates it against the JSON Schema, and renders it through the real engine to a PNG. This is Rung 2 of §4.6 — content creation. **(Not built yet)** the *user-facing* half: a skill that edits a household's live config, previews the change, and writes it to the device. Preview-before-push remains the rule for whatever eventually does that.
- **Surface 3 — the serial file protocol (development and rescue).** `YAT LS/GET/PUT/RM/STATUS/PAGES/USE/SAY/SECRET/PORTAL/REBOOT` over USB, behind `tools.html?dev=serial`. It was the original step ③ and it repeatedly failed real users — a 9 KB `PUT` with no flow control overflows the device's RX buffer, and the port picker offers ports nobody can tell apart — so it is documented as a debugging tool, not as an answer to "how does the user do X". `YAT PORTAL` opens the settings portal over the cable, which is the honest bridge between the two.

**Superseded.** *BLE Toolbox* (the website connecting to a config GATT service over Web Bluetooth) is not built and is no longer planned for v1: the device-served page reaches every phone including iOS, needs no pairing, and costs no BLE stack in firmware. *`sync_url`* (hash-gated pull of config and packs from a static host) is not built either; it stays on the v2 list as the remote-edit story, where it is a genuinely different capability rather than a second way to do what surface 1 already does.

What the settings page holds, beyond the page list:

| Setting | Behaviour |
|---|---|
| 語音 key / Voice · ElevenLabs | Writes on its own button, not on Save, **and the device tries it live** while writing — a paste that fails comes back as a failure rather than a stored dud. Names ElevenLabs in the heading and links to where the key comes from. Presence only is ever reported back; the device never hands a key out again |
| 智能聲控 / Smart voice (DeepSeek, OpenRouter or NVIDIA) | Same write-and-verify behaviour, against whichever service the key's prefix names — one field, no provider picker. Its copy says outright that it does nothing without the voice key above, and that it is not the same kind of key |
| 唔更新嘅時間 / Quiet hours | Half-hour steps; the device sleeps through them (last page stays on the panel at zero power) |
| 幾耐更新一次 / Update every | Per-page cadence override, 5 min – 1 day, defaulting to what the pack itself asks for |
| 自動輪播 / Auto-rotate | Off, 30, 60 or 180 minutes (§4.2) |
| 嘀嘀聲 / Beeps | Off silences the pure acknowledgements — the page-step chirp, the "heard you, no key yet" pair, the boot chirp. Everything carrying information somebody is standing there waiting for keeps sounding at any setting: the voice cues, the tone at the hold threshold (it is a documented instruction — without it nobody knows when to let go), and the three tones on entering setup (the only sign of life through a 30-second refresh). A quiet shelf device, not a mute one, and the card says which is which rather than promising a silence the firmware will not deliver |
| 裝置最近嘅問題 / Recent issues | The last eight things the device could not do, newest first, in the household's terms: a page that could not fetch, a transcription service that did not answer, a page that would not draw, a home WiFi that was not there. A ring in NVS that survives deep sleep, so it is still there when someone finally comes to look |
| 更新 / Firmware update | Shown only when the phone's check of GitHub's public releases (never the device's own doing) finds the device behind: current version, new version, a link to what changed, one 更新 button. A tap has the device download the release over its own WiFi and install it, rebooting into it when done; an interrupted or failed download leaves the device on the firmware it had. No release has been tagged yet, so this row has nothing to show today |

That last row is a product requirement, not a diagnostic nicety. E-ink holds its last image with the power off, so **"the screen stopped changing" is the only symptom every fault has** — a device that fails quietly is the worst thing this product can be.

What the rest of the household sees: none of the above. Buttons and voice only; the owner configures.

Scheduling concepts the config expresses: **pages[] (pack + params + voice-alias overrides — the device stores the full list)**, per-page cadence, quiet hours, auto-rotate interval. Commute windows (`from`/`to` plus a weekday mask, tightening the cadence inside them) are evaluated on-device too, but they are declared by the **pack**, not by the household: the settings page can only set one flat interval per page. Warning takeover and the failure-backoff ladder are firmware behaviour rather than config. All of it evaluated on-device.

### 4.6 Content creation UX — from tweak to new pack

A pack is **one declarative JSON file** — a *data section* (API URLs, auth by named reference, path-expression field extraction) and a *render section* (a widget tree: text, big numbers, lists, icons, images — see ARCHITECTURE.md §10). No code. The firmware's engine executes it; installing = copying the file onto the device (WebSerial, skill, or sync repo). **No reflash, no build, no deploy, no server.**

The creation ladder:

**Rung 0 — reconfigure an existing pack.** New bus stop, different district. This is config (§4.5), not content creation. The skill should recognize when a param change does the job and not scaffold a new pack.

**Rung 1 — the `image` pack (escape hatch).** A built-in pack whose param is an image URL: the device fetches a PNG on schedule, dithers it (existing pipeline), displays it. Anything that can produce an 800×480 image on the web — a chart generator, a script on someone's NAS, a TRMNL-style renderer someone chooses to run — becomes a YAT page with zero code. This honestly replaces the old "screenshot any webpage" idea, which required a browser in the cloud; here, *if* someone wants server-rendered content, they bring their own URL.

**Rung 2 — AI-authored pack (`create-pack`).** The flagship developer experience:

```
> "/create-pack — I want a page showing today's tide times for Tai Po Kau"
```

The skill interviews (data source? — it searches data.gov.hk/HKO for a matching API and confirms; layout? — offers 2–3 widget-tree sketches; cadence? aliases? — proposes en/zh-Hant/jyutping), then writes `tides.yat-pack.json`, verifies the extraction against the real API, renders it through the engine (native/WASM — identical to hardware), shows the simulated e-ink PNG, iterates to approval, and installs it. Minutes end-to-end, and every iteration shows a picture, not code.

**Rung 3 — hand-written pack.** Same file, written by hand against `docs/PACK-SPEC.md` (the path-expression grammar and widget catalog). The preview CLI runs standalone:

```
npm run preview -- --pack tides.yat-pack.json --params '{"station":"Tai Po Kau"}'
# → out/tides.png  (800×480, real engine, real 6-ink dither)
```

No hardware, no account, no quota. The contribution loop must work on a laptop on the MTR.

**Rung 4 — extend the engine (firmware contribution).** When the spec genuinely can't express something, the answer is a new widget type or format parser **upstream** — a C++ contribution that ships to everyone via OTA — never user-installable code on the device. The widget catalog growing is how the platform absorbs needs the spec can't express; packs stay data forever.

**The pack gallery.** The website hosts a browsable gallery — real renders of every pack in the [yat-packs](https://github.com/yat-hk/yat-packs) `official/` library, plus a live in-browser render through the WASM engine. Browsing only: **installing belongs on the device's own page**, which imports the identical library (`gallery-data.json` and the pack files under the website's `assets/packs/`) and mounts the identical installer UI. Params are a form generated from each pack's JSON Schema, and before install a truthful machine-derived summary says where the page will fetch from — possible because the spec declares every URL and every secret's allowed destinations, and the engine enforces it. Safety is structural, not review-based: a pack declaring `secrets` is refused by the installer and left out of the generated library entirely, because the engine rejects `{{secrets.*}}` and there is no on-device card that could explain the resulting blank page.

The **community packs repo** (`yat-packs` on GitHub, with CI validating each spec against this repo's schema and rendering a golden preview) holds those 17 real-world packs in `official/`, plus a `community/` tier for third-party contributions. The gallery is built by the website's `generate.py` (private codebase), which reads the real packs from a sibling `yat-packs` checkout's `official/` (`--packs-dir`, default `../yat-packs/official` — the script refuses to run if that checkout is missing) and this repo's own `packs/examples/render-test.yat-pack.json` for the engine-conformance section.

**The developer pipeline (agent-first, end to end).** A third-party developer never needs to learn the spec by hand — they describe what they want to *their* agent, and the published **developer skill** does the rest. The skill knows the full spec grammar, the widget catalog, the validation matrix (docs/SPEC-VALIDATION.md), and the test tooling:

```
dev: "make a pack showing GMB green minibus ETAs for route 69 in Sai Kung"
agent (with yat skill): finds the data.gov.hk endpoint → writes the spec →
  validates against the JSON Schema → renders preview PNGs (CLI/WASM engine)
  → iterates with the dev on the picture → installs it on the dev's own
  device from its settings page → opens a PR with the golden render attached
```

If a use case exceeds the grammar, the skill says so precisely ("needs sequential sources — queued v1.x, see SPEC-VALIDATION #14") instead of failing vaguely — and that report is itself the signal for what the spec grows next.

**Design principle: the spec is written by AI agents, not humans.** It optimizes for machine precision over human ergonomics — no shorthand, one canonical form per construct, a published JSON Schema, and validators that return exact, actionable errors (agents converge fast on precise errors). Verbose widget trees are fine; ambiguity is not. `docs/PACK-SPEC.md` is a formal reference written for agent consumption and ships inside the skill. Humans are welcome to write specs; the product just never depends on that being pleasant.

---

## 5. V1 content packs

All four ship as declarative specs — if the spec can't express the flagship packs, the spec isn't done.

**Where this stands:** the `yat-packs` `official/` library holds 17 real-world packs written against the shipped spec (plus this repo's own `packs/examples/render-test.yat-pack.json`, an engine-conformance pack rather than a fifth role), and the four roles below are filled by `hko-now` / `hko-9day`, `commute-combo` / `gmb-minibus`, `headlines-simple` / `news-sections`, and `photo-frame`. The names in the headings are the roles, not the file names. Warning takeover is a firmware behaviour with its own embedded pack rather than part of the weather pack.

### 5.1 `hko-weather` — Weather & Warnings

- Current temp/humidity, today + 3-day forecast, district-selectable. Source: HKO Open Data API, fetched by the device.
- UV, chance of rain; bilingual labels.
- **Warning takeover** (firmware behavior, §4.2): typhoon signals (T1/T3/T8/T9/T10), rainstorm (amber/red/black), thunderstorm, cold/hot weather. T3+ and red/black rain preempt the playlist with a full-screen page: big signal symbol, what it means, what's closed (schools/courts per standard arrangements), when issued. This page is the demo that sells the product.
- Voice aliases: `weather`, `天氣`, `tin1 hei3`; `typhoon`, `打風`, `風球`.

### 5.2 `transport-eta` — Bus & MTR arrivals

- KMB + Citybus stop ETA and MTR next-train via data.gov.hk real-time APIs, fetched by the device.
- Config = list of (company, stop, route) entries; the AI resolves human descriptions to stop IDs during setup.
- **Commute windows** (every 10 min 07:30–09:30 weekdays, hourly otherwise) so ETAs are fresh when they matter without draining battery.
- Layout: route number large (transit-signage aesthetic per BRAND.md, via the `icon`/badge widgets), minutes-to-arrival, destination in zh-Hant.
- Voice aliases: `bus`, `巴士`, `幾時有車`; `MTR`, `地鐵`, `港鐵`.

### 5.3 `news-brief` — Headlines

- RSS ingestion (RTHK Chinese/English as defaults; user-configurable feeds) via the engine's RSS parser.
- v1 mode: headlines list. **AI morning brief** — an LLM condensing the morning's feeds into a 5-bullet briefing — arrives with the spec's `llm` transform step (v1.x): the device calls the LLM API directly with the user's key, once or twice daily. "AI does the work; YAT shows the result."
- Voice aliases: `news`, `新聞`.

### 5.4 `image` — Any image URL (escape hatch)

- Params: image URL, refresh cadence. Device fetches, dithers (existing 6-ink pipeline), displays.
- Covers the long tail: chart services, self-hosted generators, anything that emits a PNG. Voice aliases user-defined.

### Pack requirements (all packs)

- Bilingual output driven by a `lang` param (`zh-Hant`, `en`, `bilingual`).
- Rendered on the engine's standard chrome: automatic footer with data source, updated time, battery/stale glyphs (Trustworthy principle).
- Fail visibly but calmly: on fetch failure, show last good data with a "stale since HH:MM" glyph — never a blank or error screen.

---

## 6. Feature list

### v1 (must ship)

Checked means it exists in the repo and has been exercised on the device; it does not mean finished.

- [x] **Firmware spec engine**: fetch (HTTPS + pinned CA bundle, no `setInsecure()`) → path-expression extract (ArduinoJson-filtered) → hash-skip → widget-tree render (real proportional Noto Sans / Noto Sans TC bitmaps shared byte-for-byte with the native and WASM targets) → refresh → deep sleep; on-device scheduler (per-page cadence, pack-declared commute windows, quiet hours, auto-rotate); warning-takeover behaviour
- [x] **YAT Pack Spec** (`docs/PACK-SPEC.md`, agent-first formal reference) with a strict JSON Schema: data + render sections, params schema, aliases, pack-scoped secrets with enforced `sent_to`, `inline` sources, `{{now}}` + date filters; install without reflash. 0.3-draft; freeze gate met for the rows the matrix exercised
- [x] The four v1 pack roles (§5) as declarative specs, bilingual, on the standard chrome
- [x] **The device's own settings page** — the primary config surface (§4.5): a bootstrap in firmware, the UI imported from the static site, everything from the pack library to the two keys to the issue log
- [x] **Website** (static): **Flash** (model picker, esptool-js with auto bootloader entry, UF2 fallback, post-flash liveness check, serial log viewer), **Set up** (instructions for the panel-and-phone half; it connects to nothing), **Photo guide**, **Gallery** (packs from the `yat-packs` `official/` library with real renders and a live WASM render; browsing only — installing belongs on the device)
- [x] Push-to-talk voice: direct STT (user key on device — firmware-only, never pack-accessible), on-device alias matching (en/zh-Hant/jyutping + s2t), optional LLM routing as a second tier, buzzer feedback; optional throughout — degrades to page-cycle
- [x] Preview CLI (`tools/preview`, native engine build): spec → PNG, no hardware needed; plus a local simulator and a WASM build verified pixel-identical
- [x] **The device says what went wrong**: an NVS error ring surviving deep sleep, surfaced in the settings page in the household's terms, plus firmware-owned cards on the panel (offline, no data, low battery, setup refused, safe mode, help)
- [ ] **Community packs repo** (`yat-packs`): CI validates schema + renders golden previews; PR-based contribution; hostile-pack defenses per the ARCHITECTURE threat model (pack-scoped secrets, SSRF blocks, resource caps, parser fuzzing, firmware-reserved warning chrome). *The defenses are in the engine; the repo and its CI are not built.*
- [x] **OTA from the phone**: the settings page checks GitHub releases when opened (device never phones home) and offers 更新; a tap sends a validated tag, and the device builds the release-asset URL itself, downloads over WiFi with TLS pinned to GitHub's own certificate chain, and verifies the image (ESP-IDF's own check, not a manifest sha256) before switching to it — rolling back automatically if the new image never completes a full post-update cycle. *No release has been tagged yet, so the update card has nothing to offer, and the download-through-install path is unexercised against a real one; the per-model release build matrix that will supply those tags has shipped separately (2026-07-28).*
- [ ] Published skills: **user skill** (setup / configure with preview-before-push / diagnose) and **developer skill** (spec grammar + widget catalog + validation matrix, preview tooling, on-device testing, PR submission). *`skills/pack-developer/SKILL.md` exists in-repo and is not published; the user skill is not written.* `AGENTS.md` and `docs/SETUP.md` cover agents and humans
- [ ] The v0.1 battery measurements (sleep floor, mAh per cycle and per skipped wake). *Still unrecorded, which makes every battery number in this document an estimate*

**Dropped from v1** — *BLE config mode* (button-triggered GATT service sharing the serial command layer). The device-served settings page does the same job over WiFi, reaches iOS, needs no pairing, and keeps a BLE stack out of the firmware. See §4.5.

### v2 (explicitly deferred)

- Hands-free Cantonese wake word (on-device KWS; plugged-in always-on mode)
- Spec v1.x additions per SPEC-VALIDATION: `mqtt` retained-read sources (Home Assistant), `format: "ics"` (secret-URL calendars), `chart` widget, sequential sources, `llm` transform step (AI briefs)
- OAuth-based packs (Google Calendar proper, Strava…) — token refresh on a sleeping device is a real subsystem
- Optional `sync_url`: hash-gated pull of config and packs from any static host (the user's own GitHub repo) on wake — remote editing, config history, `git revert` as undo. Moved here from v1: it is a genuinely different capability, not a second way to do what §4.5's surface 1 already does
- More **STT** providers so the microphone path is not tied to ElevenLabs — the router side is done (DeepSeek/OpenRouter/NVIDIA, §4.4); transcription is the half still on one vendor
- Semantic ink roles and per-model device profiles, so the same pack renders honestly on a mono panel and at other sizes
- More HK packs: racing and the long tail (the gallery makes these community-sized)
- Improv-BLE (phone-based provisioning)
- OpenDisplay receiver-protocol compatibility (accept BLE-pushed images — interop, not foundation; see §3)
- E1001 firmware support (mono driver, grayscale dither, faster/partial refresh profile — flasher UI lists it from day one, "coming soon" until it lands)
- YAT for Business layouts; additional e-ink hardware targets

### Non-goals (will not build)

- Any backend — hosted or self-hosted. (The flasher site is static files; the optional sync source is static files. Nothing executes anywhere but the device.)
- User accounts, telemetry collection, mobile app
- User-installable *code* on the device — packs are data forever; engine growth happens upstream
- Two-way conversational assistant on the device
- Anything requiring the screen to update faster than the panel allows

---

## 7. Success metrics

No telemetry (by design), so success is measured in the open:

- **Setup completion**: box → first page in <15 min, ≥80% unassisted (community reports/issues as signal).
- **Battery**: ≥6 weeks on a charge with the default schedule (hourly + commute windows). Tracked from v0.1.
- **Pack ecosystem**: count of community spec files in the wild; time-to-first-pack for a new contributor (target: one evening).
- **Adoption**: GitHub stars/forks, devices seen in the wild (self-reported).
- **Resilience**: a device left alone for a month is still correct (no wedged states requiring reflash).

---

## 8. Top risks

| Risk | Impact | Mitigation |
|---|---|---|
| **Spec not expressive enough** for real third-party use cases | Platform caps out; devs bounce | [docs/SPEC-VALIDATION.md](docs/SPEC-VALIDATION.md): 20-use-case matrix simulated before grammar freeze (≥80% pass bar); agent-first design makes verbosity free; defined extension path (spec-minor via OTA); skill reports gaps precisely, feeding the next grammar rev |
| **Malicious community packs** (secret exfiltration, SSRF, parser exploits, spoofed warnings, prompt injection) | User harm; trust collapse | Structural defenses in the engine (pack-scoped secrets, system secrets firmware-only, single-pass substitution, private-range blocks, resource caps, fuzzed parsers, firmware-reserved warning chrome) + gallery CI + maintainer review — full threat model in ARCHITECTURE |
| **Engine complexity in C++** (path evaluator + binder + widget renderer is now the project's core) | Slow build-out; bugs ship in firmware | Tiny deliberate grammar; portable core with golden-file tests in CI (native target); WASM preview keeps behavior visible; OTA fixes reach devices |
| **Widget kit can't hit the brand aesthetic** (no HTML/CSS) | Pages look like a hobby project, not BRAND.md | Invest in bitmap font set (19 MB LittleFS) + icon/badge library; explicit v0.3 exit criterion judged against BRAND.md |
| Board deep-sleep floor unmeasured | Battery weeks not months → weaker story | Measure in v0.1 (exit criterion); on-screen battery glyph from day one |
| Device-side TLS to arbitrary APIs + GitHub OTA | Fetch/OTA fragility in the field | Curated CA bundle + LittleFS extension; OTA built in v0.4 with certificate-pinned redirects and rollback (§4.5) — live end-to-end still unexercised pending the first tagged release; per-source failure → stale glyph, never blank |
| data.gov.hk / HKO API drift | Packs break silently | Spec fixes ship as JSON updates, not firmware releases; stale-data glyphs and the settings page's issue log make breakage visible. **Open gap:** getting the fixed pack onto a device that already has the old one is a manual reinstall today (§9.10) |
| **The settings UI is fetched live by every flashed device** | A site deploy changes what strangers' devices show, without their consent, and can break older firmware | The bootstrap falls back to a plain built-in page that still answers; beyond that, care — there is no version negotiation yet (§9.9) |
| Voice feels slow (~35–45 s button-to-visible) | Feature judged a gimmick | Sub-second audio ack; honest framing; physics stated everywhere |
| Cantonese wake word (v2) proves infeasible on-device | v2 flagship slips | v1 does not depend on it; push-to-talk already delivers the value |

---

## 9. Open questions (to resolve during v0.x, not blockers)

**Settled**

1. ~~Buzzer pin & capability on E100x~~ — GPIO45, passive buzzer driven by `tone()`. The audible vocabulary is now a designed set, not a single ack (§4.3, §4.4).
2. ~~Exact YAT path-expression grammar boundary~~ — worked through the SPEC-VALIDATION matrix; 18 packs run against live HK APIs.
2b. ~~BLE config-mode UX~~ — moot. BLE is dropped from v1 in favour of the device-served settings page (§4.5).
3. ~~WASM build of the engine~~ — built, and verified pixel-identical against the native and firmware targets (`tools/wasm/`).
6. ~~Whether voice should accept free-form commands routed to an LLM~~ — yes, narrowly: a second routing tier, off unless a key is set (§4.4). The assistant scope-creep is fenced off by the model's entire output space being an enumerated list — the pages the household installed, plus, on a device with the todo pack, three named operations on that one list, where "tick this off" is answered by a *number* out of a list the firmware built rather than by free text. Adding the todo intents widened that space by four answers and by nothing else; there is still no question it can be asked.
7. ~~License choice~~ — **GPL-3.0** (see §10).

**Still open**

4. Sync manifest format for `sync_url` (single JSON manifest vs directory convention) and whether to sign it — now a v2 question.
5. UF2 fallback packaging of the full flash image (primary esptool-js path unaffected) — the converter exists in `tools/release/`; no release has produced one yet.
8. GitHub org/repo naming; domain for the site; skill publication channel. The site currently points at `github.com/yat-hk/yat`.
9. **The compatibility obligation that runs the wrong way.** Every device already flashed imports its settings UI from the live site (§4.1). There is no versioning story for that yet: a change to those files must keep working against the oldest `/api` surface in the wild, and today nothing enforces it but care.
10. **Keeping installed packs current.** A pack that gains a feature in the gallery leaves already-installed copies behind with no path forward but knowing to reinstall. Raised from live use, 2026-08-02.

---

## 10. Licence and name

**[GPL-3.0](LICENSE).** Free to use, study, and modify; anything distributed that is built on it must be open under the same terms, with source and attribution, and buyers of any derived hardware must be able to install modified firmware (GPLv3 §6). Vendored components keep their own permissive licences ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)) — including the OpenCC-derived simplified→traditional table (Apache-2.0), which the earlier licence question flagged and which GPL-3.0 absorbs cleanly.

**The YAT name and logo are not licensed.** Code reuse under the GPL does not grant the right to present a product under this name. Commercial licensing outside GPL terms is by arrangement with the author.

Copyright (C) 2026 Alan Ho.
