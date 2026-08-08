# Setting up a YAT device

The canonical account of getting a device from a sealed box to your own pages
on the panel — written to be read by a person, and to be enough for an AI agent
asked to help someone through it. Every claim here is checked against
`firmware/src/main.cpp` and the website (https://yat.day, a separate private
codebase); where the two disagree, the firmware wins and this file is wrong.

**Companion docs:** [../PRD.md](../PRD.md) (why the product works this way) ·
[../ARCHITECTURE.md](../ARCHITECTURE.md) (how) ·
[UX-FLOWS.md](UX-FLOWS.md) (every screen and sound the device can produce) ·
[PACK-SPEC.md](PACK-SPEC.md) (writing a page)

---

## The shape of it

There are two steps, and only the first one needs a computer.

| | Where | How long | Needed again? |
|---|---|---|---|
| **① Flash the firmware** | A computer running desktop Chrome or Edge, USB-C data cable | ~1 min + ~40 s to boot | Once per device, ever |
| **② Everything else** | The device's own screen and a phone | ~5 min the first time | The same gesture, any time you want to change something |

Step ② is not a one-off wizard. Adding a page next month, swapping a bus stop,
pasting an API key, turning the beeps off — all of it is the same gesture into
the same page, forever. If you find yourself reaching for a cable after step ①,
something has gone wrong; see [Troubleshooting](#troubleshooting).

**What you need**

- A **reTerminal E1002** or **E1001**. Both are supported and both flash from
  the same page; the E1002's panel is six-colour, the E1001's is mono with a
  faster refresh. Pick your model on the flash page before pressing Install.
- A **USB-C data cable**. Many cheap USB-C cables carry power only, and that is
  the single most common flashing failure.
- A computer with **desktop Chrome or Edge**, for step ① only. Safari and every
  phone or tablet browser lack WebSerial. This is not something you did wrong.
- **2.4 GHz WiFi.** The ESP32-S3 cannot join a 5 GHz-only network. If your
  router presents one name for both bands you are usually fine; if it presents
  a 5 GHz-only name, the device will not see it.
- Optional, later: an **ElevenLabs** API key if you want to talk to the device,
  and a **DeepSeek** key if you want it to understand phrasings nobody wrote
  down. Both are pasted into the device's own settings page, not into anything
  we host. There is no account to create for YAT itself.

---

## ① Flash the firmware

Open [yat.day/tools.html](https://yat.day/tools.html) (the Flash page) on the
computer.

1. **Don't plug in yet.** Pick your model and press **安裝 Install**.
2. A browser popup asks you to choose a serial port, and it opens **empty** —
   that is the expected state. **Plug the USB-C cable into the device now.** An
   entry appears in the list the instant you do, named something like
   `USB Serial`, `wchusbserial…` or `CH340`. The one that just appeared is your
   device. Pick it and press Connect.
3. The browser puts the device into its bootloader by itself. No button on the
   device is pressed at any point. Flashing takes about a minute.
4. The page then polls the device until it boots and draws its first screen —
   roughly 40 seconds, most of which is one e-ink refresh. It beeps once on
   that first boot.
5. The page hands over to your phone. **The computer's job is finished.**

*Plugging in second is deliberate.* It is what makes "the entry that just
appeared is yours" true, and it removes the guesswork from a picker that
otherwise lists two or three indistinguishable ports.

**No WebSerial?** Borrow a computer running desktop Chrome or Edge — that is
the only flashing route this project can currently stand behind. A release does
also carry a `.uf2` for the drag-drop bootloader route, and building one
yourself works ([`tools/release/README.md`](../tools/release/README.md) has the
commands), but **nobody has flashed a device that way yet**, so it is not
written up here as an instruction to follow.

---

## ② Set up WiFi and pages, from a phone

Everything from here happens on the device.
[yat.day/setup.html](https://yat.day/setup.html) is instructions only — it
connects to nothing, stores nothing, and cannot see your device.

### The gesture

**Hold the green button until you hear a beep.** Usually five to ten seconds.

Do not count. The device is asleep, and only starts counting once it has woken,
so from your finger's point of view the threshold arrives late — seven or eight
seconds is normal, ten happens. One firm tone sounds *while the button is still
down*: that tone means "long enough, you can let go". Letting go on a count of
five is the single most common way to conclude the button is broken.

Then wait. What happens next, in order:

1. **Silence, and a still panel** — a few seconds, up to about twenty on a slow
   network, while the device joins WiFi and syncs its clock. Nothing to see or
   hear. It has not failed.
2. **Three rising tones** — it is in setup mode and has started drawing.
3. **About 30 seconds of the old image** — e-ink holds the previous picture
   while it redraws. A full minute from release to a new screen is normal.

A short **tap** of the same button is something else entirely: that is voice
(see [Buttons and voice](#buttons-and-voice)).

### The two screens

Look at which screen arrived.

**A — 設定 WiFi / Set up WiFi.** A fresh device, or one whose saved network
would not come up. The panel shows a QR that joins the device's own network
`YAT-xxxx`, and prints that network's name and password as text in case the QR
will not scan.

- Join it. The captive portal usually **opens by itself** — give it half a
  minute before reaching for the second QR.
- Pick your home network, type the password, save. **2.4 GHz.**
- After you tap save, **this same screen stays up for about half a minute**
  with no "saving…" message — just the identical picture. That is the device
  closing its own network, syncing the clock, and drawing the next screen.
  Don't press anything.
- When the panel changes to screen B on its own, WiFi is saved. No second
  button press is needed.
- If nobody completes this within ten minutes, the device goes back to sleep
  **without forgetting anything** and quietly retries the saved network half an
  hour later.

**B — 揀內容 / Choose what it shows.** WiFi is saved. This is the screen you
will see every time from now on. It prints, all at once:

- a **big QR** for the device's address on your home network;
- **`http://yat-xxxx.local`**, for typing on a computer — that name belongs to
  the device and never changes, so it is safe to bookmark;
- a **numeric address** (`http://192.168.x.x`) for networks where `.local` does
  not resolve;
- a **small corner QR** onto the device's own hotspot, plus
  `http://192.168.4.1`, for routers that stop devices from seeing each other;
- the line **"10 分鐘冇郁過就會自動離開設定 / Setup closes itself after 10 idle
  minutes"**, stated where the person waiting on it can read it.

If instead the panel says **屋企 WiFi 連唔上 / can't reach your home Wi-Fi** in
red, the saved network did not come up this time. The big QR becomes the
device's own hotspot; join it and open `http://192.168.4.1`. To change the
saved network, hold the green button again — the screen says so, and that
gesture routes to screen A rather than to a hotspot that cannot fix WiFi.

### The settings page

Your phone opens a page **the device is serving**. Your browser will say
**"Not Secure"**, and that is expected: the address is plain `http://` because a
device on your own network cannot hold a certificate for a name only your house
knows. Nothing you type leaves your home network.

What is in there:

- **The pack library** — weather, warnings, bus and MTR, tides, news, a family
  noticeboard, and the rest. Each pack has a preview, a form generated from its
  own schema (districts and stops are pickers, not IDs), and a plain line saying
  where that page will fetch from. Up to **16 pages** per device. A pack that is
  not in the library can be installed from its author's own GitHub link; see
  [below](#安裝出面嘅-pack--installing-from-a-link).
- **Page order and removal**, and the voice keywords each page answers to.
- **部機嘅設定 / Device settings** — the two keys, quiet hours, per-page update
  interval, auto-rotate, and the beep switch. See below.
- **裝置最近嘅問題 / Recent issues** — the last eight things the device could not
  do, in plain language. Worth a look whenever something seems off; see
  [Troubleshooting](#troubleshooting).

**Nothing is written as you edit.** Adds, removals and reorders collect in a
draft, and one **儲存 Save** uploads the packs and writes the config once. That
is deliberate: a save costs a ~30 second panel redraw during which the device
answers nothing at all and its own hotspot briefly drops. The page waits it out
and tells you when the panel actually shows what you picked — and it will say so
honestly if the page you chose is not what ended up on the ink.

Then **完成 Done**. The blue "setup mode" band across the bottom of the panel
disappears and the device returns to its schedule. Forgot to press it? Ten idle
minutes does the same thing by itself. Using the page keeps resetting that ten
minutes, so it will not close on you mid-job.

### 安裝出面嘅 pack / Installing from a link

Under the pack library there is a quieter door: **安裝出面嘅 pack · Install from
a link**. Paste a GitHub address — either a single `.yat-pack.json` file, or a
whole repository, in which case the packs inside are listed for you to pick
from — and the pack is fetched **by your phone**, checked, and described to you
before anything touches the device.

This is how you run a pack somebody wrote and has not put through the library:
their own repo, a friend's, your own. It is also the only way to run one, on
purpose — there is no other channel.

**What the library gives you that a link does not.** Every pack in the library
arrived through a pull request that checked it against the schema, rendered it
against real API responses, and produced a summary listing every host it
contacts and every key it asks for. A link has had none of that. Nobody has
looked at it. **Whether you trust the person who wrote it is the whole of the
protection**, which is why the page says so in those words and makes you tick a
box before the install button turns on.

**What a bad pack could actually do to your household.** It could put rubbish
on the panel, or show things that are simply wrong — a wrong bus time, a wrong
forecast — and it could keep contacting the hosts it names from your home WiFi,
which lets those hosts see your home network's address. Every one of those
hosts is listed on the card before you decide; that list is the thing worth
reading.

**What it could not do, no matter who wrote it.** It could not reach your voice
key or your smart-voice key. Those live in the device's own storage and have no
name a pack is able to refer to; there is no path from a pack to them. It could
not run code, because a pack is not code — it is JSON describing some addresses,
how to pick values out of what comes back, and how to lay out a screen, and
nothing in it is ever executed. It could not read your WiFi password or change
your other pages. And it could not fetch endlessly: at most eight sources, at
most 256KB per response, and never more often than every five minutes, all
enforced by the device rather than chosen by the pack.

A file that is not a pack, or is bigger than the device can hold, or reuses the
id of a pack already in the library, is **refused** rather than warned about —
there is no button to press through those. The id rule is the one worth knowing:
the device keeps one file per pack id, so a stranger's `hko-now` would replace
your 現時天氣 for every page using it, and that is not something a warning can
make safe.

Pages installed this way are marked **出面嚟** in the list, and they are never
offered the library's 更新 badge. Their updates come from you pasting the link
again — the device does not go back to that repository on its own, ever, and
neither does this page. Contacting a stranger's server on a schedule is exactly
what is being avoided.

One caveat: the device stores the pack file but not the form behind it, so
changing such a page's settings later means pasting the same link again. The
page remembers the link and offers it to you.

### Device settings in detail

| Setting | What to know |
|---|---|
| **語音 key / Voice key** | ElevenLabs. Saved by **its own button**, not by the big Save — and the device tries the key against the service while storing it, so you find out immediately whether it works. That is why it takes a moment. The device never hands a key back; the page can only tell you whether one is there. |
| **智能聲控 / Smart voice** | DeepSeek. Same write-and-verify behaviour. It does **nothing** without the voice key above. |
| **唔更新嘅時間 / Quiet hours** | Half-hour steps. The device sleeps through them; the last page stays on the panel at zero power. |
| **幾耐更新一次 / Update every** | Per-page override of how often that page refetches, 5 minutes to once a day. Left alone, each page follows the cadence its own pack asks for. |
| **自動輪播 / Auto-rotate** | Off, 30, 60 or 180 minutes. Nothing shorter exists on purpose: every advance is a full 30-second refresh, paid in battery and in the panel's finite rated refresh count. |
| **嘀嘀聲 / Beeps** | Turns off the pure acknowledgements. The sounds that carry information you are standing there waiting for — the voice cues, the tone that tells you to let go of the button, the three tones on entering setup — keep sounding at any setting. |

One sound can arrive when nobody pressed anything: **two low buzzes** means
something new went wrong since the last time the device said so — once per
fault, never repeating on every refresh. What it was is written in the setup
page's 裝置最近嘅問題 card. With beeps off, or during quiet hours, the
announcement waits for the next allowed cycle instead of being dropped.

### 更新 / Updating

Firmware updates are one tap from the phone, from this same settings page.
The first flash still needs the computer ([above](#-flash-the-firmware)); no
update after that ever does.

- Every time you open the settings page, it quietly checks GitHub's public
  releases in the background. **The device itself never does this** — nothing
  on it calls home on its own schedule; the check happens only because your
  phone, with someone in front of it, opened the page.
- If a newer release exists, a **有新版本 / A new version is available** card
  appears, naming the version you're on, the version on offer, and a link to
  see what changed in it.
- Tap **更新 Update**. The device downloads the update itself, over your home
  WiFi, straight from the official YAT release on GitHub and nowhere else.
  The panel switches to a **更新緊韌體 / Updating firmware** card and stays
  there for the whole download.
- **唔好熄電源 — leave it powered on**, and don't unplug it. This takes a few
  minutes, and the screen not moving for that whole time is what it looks like
  when it's working, not a freeze.
- When it finishes, the device reboots on its own into the new version. Your
  pages, settings and saved keys are untouched.
- If anything goes wrong partway — the WiFi drops, the power goes out — the
  device simply keeps running the firmware it already had. The download lands
  in a spare, inactive half of its storage and is checked before anything
  switches over to it, so an interrupted update leaves nothing to recover:
  the copy that was already running was never touched.

The card only appears once a *newer* release exists than the one already on
the device — a freshly flashed device is already on the current release, so
there is nothing to show until the next one ships.

---

## Buttons and voice

Three buttons, four gestures. Every one of them makes a sound, because a panel
that takes 30 seconds to change cannot acknowledge anything on its own.

| Gesture | What happens |
|---|---|
| **Green, tap** | Voice. Wait for the **high beep** before speaking — that beep means "go". |
| **Green, hold until it beeps** | Setup mode (above). The one gesture for every later change. |
| **◀ / ▶** | Previous / next page. |
| **◀ + ▶ together** | A help card: your pages, which one is showing, up to three words each answers to, what the buttons do, and how to get into setup. Any button takes it away. |

### Talking to it

Tap, wait for the high beep, then say the name of a page: 「天氣」, "news",
「小巴」, 「潮汐」. It records about four seconds, chirps lower to say it has you,
switches page, and spends ~30 seconds redrawing. The framing that makes this
feel right is **ask, put it down, it'll be there** — the 30 seconds is panel
physics, not software, and the immediate beep is what makes it feel answered.

Two tiers decide which page you meant:

1. **Keywords, on the device.** Each pack declares words in English, Chinese and
   Jyutping, and you can add your own per page. Free, offline, instant.
2. **A language model, only if you set the smart-voice key.** Keyword matching
   is literal, so 「而家出面凍唔凍」 matches nothing. With a DeepSeek key set, the
   device asks the model to pick one of *your* pages. It cannot invent a page —
   anything that is not one of your page ids reads as "didn't catch that" — and
   if the service is down, voice quietly falls back to keywords only.

### The 待辦 list, out loud

> **仲未出得街 · Not in the library yet.** The 待辦 page is written and the
> firmware speaks its language, but it is being held back until the voice half
> has been tested properly on a real device. It is not in the pack library, so
> nothing below can be reached today — this section describes what is coming
> rather than what you can do this afternoon.

If you have installed the **待辦 / todo** page, voice can do three things to it
as well as show it. Nothing here exists on a device without that page: the
words below simply mean nothing, and voice keeps switching pages exactly as it
did before.

Tap, wait for the high beep, then start with one of the three words:

| Say | What it does |
|---|---|
| 「**加**買菜」 · 「**記住**交電費」 · "**add** buy milk" | Puts it on the list. Everything after the first word is the item, spaces and all. |
| 「**搞掂**買菜」 · 「**完成**交電費」 · "**done** buy milk" | Ticks that item off. |
| 「**清理**」 · 「**清咗**」 · "**clear**" | Tidies away the items already ticked off. |

Then the list redraws with the change on it, the same ~30 seconds as any other
page. **The screen is the receipt** — you do not have to remember whether it
heard you.

Three things worth knowing, because they are what the device does rather than
what it promises:

- **It will not guess which item you meant.** You do not have to say the whole
  thing — 「搞掂買菜」 finds 「去街市買菜」 — but if *two* things on your list could
  be the one, it ticks off neither and gives you the try-again tone. Say more of
  the item and it will find it. A device that guesses is a device you have to
  check after every 「搞掂」, and then you may as well have used the phone.
- **Voice never deletes anything you have not finished.** 「清理」 only removes
  items already ticked off. There is no word you can say that takes an
  unfinished item off the list — that is a trip to the settings page, on
  purpose.
- **Simplified or traditional, it does not matter.** The transcription service
  hands back whichever it feels like; the device folds them together before
  matching, so 「买菜」 finds 「買菜」.

It also gives you the try-again tone, and changes nothing, when the list is
already 20 items long, when you ask for something that is already on it, or
when 「清理」 has nothing ticked off to tidy.

With the **smart-voice key** set, the same three verbs work from ordinary
sentences too — 「我交咗電費喇」 ticks off 交電費 — because the model is shown your
open items along with your pages. Without that key it is the three words above
and nothing else, which is worth knowing before you conclude the feature is
broken. The model is only ever allowed to answer with an item *number* off a
list the device built, so it cannot tick off something you do not have.

**The sounds:**

| Sound | Meaning |
|---|---|
| Two soft chirps, same note | Heard you, can't act — no voice key yet, or no WiFi. **The microphone was never powered.** Also means "only one page installed, nowhere to step" on the arrows. |
| One high beep | Speak now. |
| A lower beep | Recording finished, working on it. |
| A rising pair | Recognised — switching page, or changing the list, then redrawing. |
| One long low buzz | Something failed: an expired key, a dead mic, no network. |
| A gentle falling pair | Didn't recognise that, or couldn't tell which item you meant. Nothing changed; try again with more of the words. |

**This is the one part of YAT that leaves your home.** Everything else goes
straight from the device to a public API. Those four seconds of audio go to the
transcription service whose key you supplied. If you don't want that, don't add
a key — with no key the microphone is never powered at all, and the button just
chirps.

**And if you set the smart-voice key as well:** the open items on your 待辦 list
are sent to that second service, with each request, so that it can tell which
one you meant. Not the finished ones, not the other pages' contents, and not
anything at all if you have no todo page — but if a shopping list is one thing
and 「打電話畀律師」 is another, that is the difference the second key makes. The
first key alone gives you the three words above and sends nothing but the audio.

---

## Troubleshooting

The device's own **裝置最近嘅問題 / Recent issues** card, in the settings page, is
the first place to look for anything that "just stopped working". E-ink holds
its last image with the power off, so *the screen stopped changing* is the only
symptom every fault has — that card is where the device gets to say which one it
was. It keeps the last eight, and they survive being unplugged.

| Symptom | What it usually is |
|---|---|
| Held the button a while, no beep | **Keep holding.** The count starts when the device wakes, so seven to ten seconds from your finger's point of view. Otherwise: wrong button, or the battery (plug in USB-C and retry). |
| Silence and a still panel for a minute after letting go | Expected. Silent WiFi + clock phase (up to ~20 s), three tones, then ~30 s of drawing. Treat it as failed only after about 90 seconds. |
| Red **叉住電先設定 / Plug in to set up** screen | Below 3.50 V the device refuses setup mode rather than browning out halfway through it — an AP, a WiFi client and two full refreshes back to back is the heaviest thing it ever does. Plug in USB-C and hold the button again. The screen clears itself after a few minutes. |
| The port picker stays empty after plugging in | Empty *before* you plug in is correct. If plugging in adds nothing, it is almost always a charge-only cable. Swap it, or try another USB port. |
| Scanned the QR, the page won't open | Your phone must be on the same WiFi as the device. Still nothing: scan the small corner QR to join the device's own hotspot and open `http://192.168.4.1`. |
| The page is plain text saying **介面載入唔到 / couldn't load the interface** | The device is fine and is answering you — this is its own built-in fallback. What failed is **your phone's** internet: the richer interface is fetched live from the website. You can still see status and the page list. Get the phone online and reopen the same address. |
| The settings page stops responding halfway | Almost always the ~30 second redraw. Wait. If it really is gone, hold the green button again — anything already saved is still saved. |
| A tap on the green button gives two soft chirps and nothing else | "Heard you, can't act." Usually no voice key yet (Device settings → Voice key), or no WiFi saved. On the arrows, the same pair means only one page is installed. |
| 「搞掂...」 gives the falling pair and the item stays on the list | Two things on the list could have been the one you meant, so it ticked off neither rather than guessing. Say more of the item. The other reasons for that tone on a 待辦 command: the item is already ticked off, the list is full at 20, or you asked to add something already on it. |
| No pages at all | A brand-new device shows a welcome page until you choose your first one. Its copy points back at the same green button. |
| Setup closed while I was reading | Ten idle minutes. Nothing is lost, nothing is broken, and the same gesture reopens it. Touching the page resets the timer. |
| The screen has not changed in days | Check Recent issues first. A page that cannot fetch keeps showing its last good data with a stale marker rather than going blank, which is correct behaviour but does look like a frozen screen. |

**Deeper diagnosis** lives on the flash page: `tools.html` has a live device log
(a screenshot of it is worth a paragraph of description when asking for help),
and `tools.html?dev=serial` exposes the serial protocol —
`YAT STATUS`, `YAT ERRORS`, `YAT PAGES`, `YAT PORTAL` (opens the settings server
over the cable), and the file commands. That path is kept for development and
rescue: bulk transfers over it are fragile by design, and the supported route
for anything a household does is the device's own page.

---

## For agents

If you are an AI agent helping someone set up a device, the load-bearing facts:

- **Never tell anyone to hold the button for five seconds.** Say "hold until you
  hear the beep, usually 5–10 seconds". The device starts counting at wake, and
  a five-count release is the most common false failure in this product.
- **Never send them back to a cable.** After flashing, every configuration
  change is the green button plus a phone. If you are about to suggest
  WebSerial for a routine task, you have the wrong path.
- **Narrate the waits before they happen.** The silent 20 seconds after release,
  the half-minute where screen A does not change after saving WiFi, the 30
  seconds where the device answers nothing after Save. Each one is mistaken for
  a failure by someone who was not warned.
- **Read Recent issues before theorising.** `GET /api/status` on the device (or
  `YAT ERRORS` over serial) returns the ring directly, newest first, with codes
  `src_fetch`, `stt_http`, `llm_http`, `render`, `fs_write`, `wifi_lost`.
- **Do not promise features that are not built, and do not oversell ones that
  are.** As of 2026-08-05, one-tap OTA updates from the phone are built and
  have been rehearsed end-to-end on hardware against a tagged release (see
  [Updating](#更新--updating)). There is still no `sync_url` remote-config, no
  Bluetooth configuration, and no user-facing setup skill. See ROADMAP.md
  before telling anyone something is coming.
- **Pack authoring** is a different job with its own guide:
  [`skills/pack-developer/SKILL.md`](../skills/pack-developer/SKILL.md) and
  [PACK-SPEC.md](PACK-SPEC.md). `tools/preview/` renders any pack to a PNG
  without hardware, and `tools/preview/simulator.py` gives you a browsable local
  version of the library.
