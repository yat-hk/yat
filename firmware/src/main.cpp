// YAT v0.3 — scheduler, config & provisioning (reTerminal E1002 / E1001).
//
// One wake cycle, top to bottom, then back to deep sleep:
//
//   wake (timer or KEY0/1/2) -> mount LittleFS -> load config+pack -> SHT4x
//   -> [KEY0 tap: voice capture -> STT -> page match] -> WiFi -> SNTP
//   -> fetch pack source -> extract -> hash-skip?
//   -> render into the 4bpp sprite -> panel refresh -> store hash
//   -> compute next wake from schedule+quiet-hours -> console window -> sleep
//
// Everything happens in setup(); loop() is only a safety net. v0.1 measured
// the walking skeleton (ROADMAP §v0.1); v0.2 made the device config-driven:
// LittleFS holds config.json + packs/*.yat-pack.json (ARCHITECTURE §4), an
// on-device scheduler reads the pack's schedule + config quiet_hours
// (ARCHITECTURE §3, PACK-SPEC §10), all three buttons wake via ext1
// (ARCHITECTURE §3), and a serial line protocol gives the toolbox a file
// interface without a reflash (ARCHITECTURE §4). v0.3 removes the last
// compile-time secret: WiFi credentials move to NVS, and a fresh/reset
// device provisions itself over a WiFiManager QR captive portal or
// Improv-serial (ARCHITECTURE §8), and a KEY0 tap records a few seconds of
// speech, transcribes it, and switches to the page whose aliases it names
// (PRD §4.4 tap-to-talk). See README.md.

#include <Arduino.h>
#include <ESP_I2S.h>
#include <HTTPClient.h>
#include <ImprovWiFiLibrary.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>  // esp_reset_reason() — the crash-loop breaker's input
#include <esp_timer.h>
#include <time.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "TFT_eSPI.h"

#ifndef EPAPER_ENABLE
#error "EPAPER_ENABLE not set - check -DBOARD_SCREEN_COMBO=521 in platformio.ini"
#endif

// Typography comes from the engine itself as of v0.3 (engine/src/fonts_data_*.cpp,
// reached through <yat/fonts.h>): real proportional Noto Sans / Noto Sans TC
// bitmaps at the spec's sizes, ~1.36MB of flash, shared byte-for-byte with the
// native and WASM targets. That replaced efont's 16x16 full-BMP table (~738KB),
// which this file no longer includes — nothing references it, so it is no longer
// linked into the image at all.
#include <yat/engine.h>
#include <yat/icons.h>

#include <pngle.h>

// Vendored Project Nayuki qrcodegen (MIT) — see engine/third_party/qrcodegen/
// for the license and implementation. This is the same encoder
// engine/src/render.cpp uses for the pack "qr" widget (§9.11); the header is
// outside the engine library's declared includeDir, so platformio.ini adds
// engine/third_party/qrcodegen to the include path directly rather than
// touching engine/ (owned by another agent). qrcodegen.c itself is compiled
// exactly once, by the engine library's own qrcodegen_shim.c.
#include "qrcodegen.h"

// Simplified -> Traditional Chinese char map (OpenCC-derived) — the same
// table engine/src/text.cpp's "s2t" filter binary-searches (PACK-SPEC §8.7).
// Its declarations (S2T_N/S2T_FROM/S2T_TO) live directly under
// engine/third_party/, outside the engine library's declared includeDir, so
// this is the identical pattern as qrcodegen.h just above: platformio.ini
// adds engine/third_party to the include path directly rather than touching
// engine/ (owned by another agent). text.cpp's own lookup/UTF-8 helpers are
// file-local (anonymous namespace) and not reachable from here, so the
// voice-matcher section below (normalizeUtf8) re-implements the same ~10-line
// binary search + UTF-8 walk against this one shared table.
#include "s2t_table.h"

#include "certs.h"
#include "hko_now_pack.h"
#include "portal_page.h"
#include "welcome_pack.h"
#include "secrets.h"
#include "warning_takeover_pack.h"

#include "yat_beep.h"
#include "yat_buttons.h"
#include "yat_config.h"
#include "yat_console.h"
#include "yat_errors.h"
#include "yat_hw.h"
#include "yat_llm.h"
#include "yat_net.h"
#include "yat_portal.h"
#include "yat_provision.h"
#include "yat_sched.h"
#include "yat_screens.h"
#include "yat_voice.h"
#include "yat_common.h"
#include "yat_state.h"

// The whole cycle — mbedTLS handshake, ArduinoJson parse, engine render — runs
// on the Arduino loop task, whose 8 KB default is nowhere near enough for any
// of the three. Verified against the real xtensa frames (`-fstack-usage` on
// this exact build) rather than guessed, because the three deepest paths are
// all recursive and none of them is obvious by eye:
//
//   render     setup() 720 + Engine::render 688 + 8 x (drawContainer 272 +
//              drawWidget 560) + drawQr 7,984 (a leaf, entered only when a page
//              really draws a QR)                                    ~16.0 KB
//   extract    setup() 720 + fetchAndExtract 1,232 + scanListNesting 624 per
//              level of widget nesting, at the schema's 24-deep
//              ceiling                                               ~16.5 KB
//   QR screens setup() 720 + a card frame + prov::drawQR 7,904         ~8.8 KB
//
// The arithmetic is deliberately pessimistic — it assumes every frame on the
// chain is live at once. An empirical whole-render probe (the native target,
// sampling the stack pointer from inside Canvas::fillRect, which is the only
// engine hook reached from inside the QR leaf) puts the deepest example pack,
// family-board, at 13.9 KB against the 16.0 KB predicted here.
//
// So ~16 KB worst case against 32 KB: about 16 KB of margin, and the number to
// watch is logStackHeadroom()'s, which reports the real thing from hardware.
// Do not read the margin as slack to spend — the deep paths are all data-driven
// (a pack's nesting, a feed's shape), so what is measured is what today's packs
// happen to need, not a bound on what a future one can ask for.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

EPaper epaper;
WiFiManager g_wm;

static float g_temp = 25.0f, g_humi = 50.0f;
bool g_panelReady = false;
bool g_buttonWake = false;
bool g_pageForward = false;  // this wake's cause was KEY1 (ext1)
bool g_pageBack = false;     // this wake's cause was KEY2 (ext1)
// Both arrows (◀+▶) together: the household is asking what this thing can do
// and what it will listen for. Set by any of the four ways that gesture can be
// caught (sampleArrowsAtWake, reportWakeReason, armButtonInterrupts,
// processKeyLatches) and acted on once, in setup(), where it replaces the
// page-step this wake would otherwise have been. Not a rotation slot: the help
// card never touches g_activePageIdx, so the page you were on is the page the
// next press steps from.
bool g_helpRequested = false;
// Set by YAT USE when it changes the active page mid-cycle (serial protocol
// section below); honored by the hash-skip check the same way g_buttonWake
// and g_coldBootWake are, so a page switched over the console during the
// wifi-connect console window still renders THIS cycle instead of waiting
// for the next wake.
bool g_serialForceRender = false;
// G14: power-on / brownout-recovery / post-provisioning / post-OTA boots all
// report ESP_SLEEP_WAKEUP_UNDEFINED. None of those may hash-skip: the panel
// could be holding a mid-refresh brownout image, or (G7) the provisioning
// screen from a device that just finished setup — either way, the next
// render must happen even if the data hashes the same as before.
bool g_coldBootWake = false;
// A firmware-owned card (low battery, setup refused) was holding the panel and
// the condition behind it has cleared on this wake. Honored by the hash-skip
// check like g_coldBootWake: unchanged data must not leave a card describing a
// problem that no longer exists on screen for hours. See ScreenCard below.
static bool g_cardCleared = false;
// A source served a stale snapshot this cycle, or stopped doing so, versus the
// state the panel was last drawn in ("was_stale" in NVS). The red stale badge
// is chrome, not data: a failed fetch restores the identical snapshot values,
// so the content hash matches and the badge would never appear on the very
// transition it exists to announce. Also honored by hash-skip.
static bool g_staleChanged = false;
// Auto-rotate (config.json "loop_min") stepped the active page at the top of
// this cycle. Honored by hash-skip like the flags above, and for a reason none
// of them cover: the stored hash describes the page that was on the panel
// BEFORE the advance, so two pages whose data happens to hash the same — two
// views of one feed, or any page on a quiet afternoon — would leave the panel
// showing the page the household just rotated away from. See maybeAdvanceLoopPage().
bool g_loopAdvanced = false;
bool g_fsReady = false;
// This boot mounted the filesystem only after formatting it — every pack and
// the page list that were on it are gone. Reported by YAT STATUS rather than
// left silent, because from the outside a wiped device is indistinguishable
// from one that was never set up (see the LittleFS.begin call in setup()).
bool g_fsFormatted = false;
uint32_t g_lastHash = 0;
uint64_t g_plannedSleepSeconds = 0;  // for YAT STATUS "next_wake_s"

// Consecutive crash resets on record (NVS "panic_n") — read once at the top of
// setup(), acted on just past the KEY0 gesture. See PANIC_SAFE_MODE_N.
uint32_t g_panicStreak = 0;

// What the LAST render actually put on the panel, mirrored from NVS ("render_ok"
// / "render_page") so it survives deep sleep and answers correctly at any point
// in a cycle, including before this cycle has rendered anything.
//
// It is deliberately the RESULT, not the intent: the setup site's saved-card can
// only honestly say 「搞掂」 when the page the household picked is the page the
// ink is showing, and every other outcome — a pack that loaded but would not
// render, a firmware card that got there first, the welcome fallback — has to be
// distinguishable from success rather than assumed to be it. Empty page id means
// "whatever is on the panel is not one of the configured pages"; false with a
// page id means that page was tried and would not draw.
bool g_renderOk = false;
std::string g_renderedPage;

// Warning takeover (PRD §4.2/§5.1, docs/UX-FLOWS.md G15): set once per cycle,
// right after WiFi connects and before any page pack loads, by the warnsum
// policy check below (or by YAT TAKEOVER's debug override). Read by the
// pack-load branch (embedded warning-takeover pack vs. the configured
// playlist), the hash-skip bypass, the sleep-cadence clamp, and YAT STATUS.
bool g_takeoverActive = false;
std::string g_takeoverSignal;    // short code for logs/STATUS, e.g. "TC8NE"/"WRAINB"; "" if inactive
static bool g_takeoverChanged = false;  // this cycle's active flag flipped vs. the persisted NVS value

// ---------------- provisioning globals (ARCHITECTURE §8) ----------------
// AP identity, derived once per boot in buildApCreds() — needed both for the
// provisioning screen/portal and as the Improv "device name", so it's filled
// in unconditionally early in setup(), whether or not this boot actually
// provisions.
char g_apSsid[24] = {0};
char g_apPass[9] = {0};       // 8 chars + NUL
char g_qrWifiPayload[80] = {0};

volatile bool g_portalSuccess = false;  // set from the core-0 portal task
volatile bool g_improvSaved = false;    // set from ImprovWiFi's connect callback
TaskHandle_t g_portalTaskHandle = nullptr;

// The number behind SET_LOOP_TASK_STACK_SIZE above. uxTaskGetStackHighWaterMark
// reports the LOWEST free byte count this task's stack has ever had (ESP-IDF's
// port returns bytes, not words), so it only ever falls — which is what makes
// logging it at several points through a cycle worth more than one total at the
// end: the drop attributes itself to a phase.
//
// Two phases are deep, and they sit at opposite ends of a cycle. mbedTLS's
// handshake runs inside httpsFetch; the engine's render walks a recursive widget
// tree and drops into a ~8 KB leaf frame whenever a page draws a QR code (the
// two qrcodegen scratch buffers), as does every firmware screen that prints one.
// Anything unexplained here means one of those grew — see the stack analysis in
// this file's SET_LOOP_TASK_STACK_SIZE comment.
void logStackHeadroom(const char* where) {
  LOGF("[mem] stack: %u bytes still free of %u after %s\n",
       (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)getArduinoLoopTaskStackSize(),
       where);
}

// ---------------- one wake cycle ----------------

void setup() {
  // Before anything else that takes time: the both-arrows gesture is only
  // readable off the pads while the fingers are still down, and every
  // millisecond spent here is a millisecond of somebody's press being missed.
  // Deliberately ahead of even the wake-up blink.
  sampleArrowsAtWake();

  // Short "I woke up" blink rather than holding the LED on for the whole cycle:
  // a few mA across ~40 s is a visible fraction of the per-cycle budget this
  // firmware exists to measure, and it would sit inside every reading.
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);  // LOW = on
  delay(80);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_KEY0, INPUT_PULLUP);
  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);
  pinMode(PIN_BAT_EN, OUTPUT);
  digitalWrite(PIN_BAT_EN, LOW);

  Serial.begin(115200);
  // Never block on debug output: Serial is the USB-Serial-JTAG (HWCDC), and
  // with no host reading it a full TX buffer stalls the writer. A LOGF stall
  // >~22 ms during a console PUT (256-byte RX ring at 115200) drops payload
  // bytes — "ERR short read" at home, fine in the lab where a monitor drained.
  Serial.setTxTimeoutMs(0);
  // Console PUTs stream multi-KB payloads with no flow control; the default
  // 256-byte RX ring overflows on any hiccup. Must be set before begin().
  Serial1.setRxBufferSize(4096);
  Serial1.begin(115200, SERIAL_8N1, PIN_DBG_RX, PIN_DBG_TX);

  // Cold-boot chirp: the only instant proof of life this hardware can give.
  // A just-flashed panel keeps its old image through ~60 s of first-boot work
  // and refresh — and a reflash of identical content skips the refresh
  // entirely — so without a sound, a perfect reboot is indistinguishable from
  // a dead one. Timer and button wakes stay silent; beeping every cycle would
  // be unbearable on a shelf device.
  //
  // config.json's "beep" reaches this one through NVS: LittleFS is not mounted
  // for another few hundred milliseconds, and a chirp that arrived after the
  // mount would no longer be answering the boot.
  restoreBeepPrefFromNvs();
  if (g_beepEnabled && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
    beep(2093, 50);
    beep(1568, 50);
  }

  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  LOGF("\n=== YAT v%s (%s) ===\n", FW_VERSION, MODEL_NAME);
  reportWakeReason();
  // audit #18: before anything that can fault, count this boot if the last one
  // ended in one. Only reads NVS and logs here — the decision needs the KEY0
  // gesture, which is not read for another few hundred milliseconds.
  noteResetReason();
  // What the panel is actually holding, for YAT STATUS during this cycle's
  // early console windows — the normal cycle overwrites it once it renders.
  restoreRenderResultFromNvs();
  // And what went wrong on the cycles before this one, so /api/status and
  // YAT ERRORS can answer from the first console window onward rather than
  // only about failures this particular wake happens to hit.
  restoreErrorRingFromNvs();
  // Before anything that reads the clock — and in particular before the
  // warnsum TLS fetch further down, which on a cold boot used to hand mbedTLS
  // a 1970 date to check certificate validity against (see restoreClockFromNvs).
  restoreClockFromNvs();
  const float battVolts = batteryVolts();
  const int batteryPct = batteryPercent(battVolts);
  LOGF("[power] battery %.2f V (%d%%)\n", battVolts, batteryPct);
  LOGF("[mem] heap %u free, psram %u free\n", (unsigned)ESP.getFreeHeap(),
       (unsigned)ESP.getFreePsram());

  // G25/G26: below LOW_BATTERY_PCT, skip the whole cycle -- no WiFi join, no
  // fetch, no pack load -- in favor of a firmware-owned low-battery card and
  // a long low-power sleep. Deliberately ahead of buildApCreds()/the KEY0
  // gesture read/provisioning below: at this charge level even an
  // intentional KEY0-held provisioning request would risk finishing the
  // battery off in AP mode, so every wake (button or timer) gets the same
  // answer until the household charges it. Buttons still physically wake the
  // device early (lowLevelSleep always re-arms ext1), so a press re-checks.
  //
  // The card itself is drawn once per streak. Every button press on a flat
  // device wakes it, and redrawing cost a 30 s refresh each time — the most
  // expensive thing available, spent to put an identical image back on the
  // panel, at the exact moment there is least charge to spend. A worried
  // household pressing buttons was the fastest way to finish the battery off.
  const ScreenCard cardOnScreen = loadScreenCard();
  if (batteryPct < LOW_BATTERY_PCT) {
    if (cardOnScreen == ScreenCard::LowBattery) {
      LOGF("[power] low-battery card already on the panel — no refresh this wake\n");
    } else {
      saveScreenCard(ScreenCard::LowBattery);
      drawLowBatteryCard(batteryPct);
    }
    deepSleepMinutes(LOW_BATTERY_SLEEP_MIN, "low battery");
  }

  // The charge is back above whichever floor put the card up, so the panel is
  // holding a warning about a problem that no longer exists. Clearing the
  // marker is not enough — the data may well hash the same as it did before the
  // card went up, and hash-skip would leave the card there for hours — so this
  // is also a render reason in its own right.
  if (cardOnScreen == ScreenCard::LowBattery ||
      (cardOnScreen == ScreenCard::SetupRefused && battVolts >= CONTENT_PORTAL_MIN_VOLTS)) {
    LOGF("[power] battery recovered — clearing the on-screen card and forcing a render\n");
    saveScreenCard(ScreenCard::None);
    g_cardCleared = true;
  }

  // The help card is the one card with no condition to re-check: it was asked
  // for, it has been read, and this wake is the one meant to take it away. So
  // it clears unconditionally — and, like every other card, clearing the
  // marker alone is not enough, because the data behind the page underneath it
  // has very likely not changed and hash-skip would put nothing back.
  if (cardOnScreen == ScreenCard::Help) {
    LOGF("[help] help card was on the panel — clearing it and forcing a render\n");
    saveScreenCard(ScreenCard::None);
    g_cardCleared = true;
  }

  // AP identity + Improv-serial setup happen unconditionally, before the
  // provisioning-or-not decision below: Improv is active whenever awake
  // (ARCHITECTURE §8), not only during provisioning, and the AP name doubles
  // as the Improv device name either way. Cheap — no radio join here.
  buildApCreds();
  g_improv.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "yat", FW_VERSION, g_apSsid,
                         "http://{LOCAL_IPV4}");
  g_improv.onImprovError(improvErrorCb);
  g_improv.onImprovConnected(improvConnectedCb);
  g_improv.setCustomConnectWiFi(improvConnectCb);

  // One sampling pass decides which KEY0 gesture this was (see
  // readKey0GestureAtWake()); only costs time when KEY0 actually caused this
  // wake. Held 5 s -> provisioning (PRD §4.3), tapped -> voice (PRD §4.4).
  const Key0Gesture key0 = readKey0GestureAtWake();
  const bool key0Held = key0 == Key0Gesture::Hold;
  const bool key0Tap = key0 == Key0Gesture::Tap;

  // From here on a press is never silently lost: the three buttons get digital
  // interrupts that latch it, and the checkpoints through the rest of this
  // function act on it (button-latch sections). Deliberately after
  // readKey0GestureAtWake(), which does its own RTC-to-GPIO handoff on KEY0 and
  // samples the pad by hand — an interrupt attached before that would be
  // attached to a pad the RTC mux still owns.
  armButtonInterrupts();

  // audit #18: the third crash reset in a row stops the cycle right here —
  // ahead of the filesystem mount, the config parse, the pack load and the
  // renderer, which between them are everything that could plausibly have
  // caused it. Deliberately behind the KEY0 read: the card this draws asks for
  // the green button, so a hold has to be able to reach the setup portal (where
  // a bad page can actually be swapped out) instead of being answered with the
  // same card again. Nothing but the button and a clean cycle clears the count,
  // so a device that faults on wake #1 and again on wake #2 still gets its
  // ordinary third try — this only fires once the pattern is unambiguous.
  if (g_panicStreak >= PANIC_SAFE_MODE_N) {
    if (key0Held) {
      clearPanicStreak("green button held — the household is taking over");
    } else {
      LOGF("[crash] %u crash resets in a row — safe mode: no pack, no render, %u min sleep\n",
           (unsigned)g_panicStreak, (unsigned)SAFE_MODE_SLEEP_MIN);
      if (cardOnScreen == ScreenCard::SafeMode) {
        LOGF("[crash] safe-mode card already on the panel — no refresh this wake\n");
      } else {
        saveScreenCard(ScreenCard::SafeMode);
        drawSafeModeCard();
      }
      // Safe mode is a rate limiter, not a terminal state, and the card says so
      // in as many words ("tries again by itself in an hour"). Leaving the count
      // at the threshold would make that a lie and the mode a dead end that only
      // a person could ever leave — while the fault behind it may well be a feed
      // that comes back on its own. So the count drops to one below the
      // threshold on the way out: the next wake gets a full, ordinary attempt,
      // and if that faults too the very next boot is back in here. One crash an
      // hour instead of one every forty seconds, with a self-healing path intact.
      {
        Preferences prefs;
        prefs.begin("yat", false);
        prefs.putUInt("panic_n", PANIC_SAFE_MODE_N - 1);
        prefs.end();
      }
      deepSleepMinutes(SAFE_MODE_SLEEP_MIN, "safe mode: repeated crashes");
    }
  }
  // Got past the breaker with its card still on the panel — this is the one
  // ordinary attempt safe mode allows per hour. Force a render, because the
  // household's data has very likely not moved while the device was stuck
  // rebooting and hash-skip would otherwise leave 「暫時停低咗」 up on a device
  // that has been working again for hours.
  //
  // The marker is deliberately NOT cleared here, unlike the low-battery and
  // setup cards above: the panel still physically holds that card, and this
  // cycle is likelier than most to fault before it can replace it. Left set, a
  // second safe-mode entry knows the card is already there and skips the 30 s
  // refresh; the successful render at the end of setup() clears it for real.
  if (cardOnScreen == ScreenCard::SafeMode) {
    LOGF("[crash] safe-mode card is on the panel and this cycle is running — forcing a render to "
         "replace it\n");
    g_cardCleared = true;
  }

  // LittleFS ("littlefs" label — partitions_ota_32mb.csv) before anything
  // else needs config: first boot seeds config.json + the hko-now pack file
  // from the embedded copy, after which the filesystem is authoritative.
  //
  // Mounted WITHOUT format-on-fail first. A format is not a quiet recovery
  // step: it deletes every pack the household installed and the page list they
  // built, and the device then comes up looking factory-fresh with no
  // explanation for where their screen went. It is still the only way back
  // from an unmountable volume (and is what a just-flashed device needs), so
  // it still happens — but it is now attributable: a log line, a lifetime
  // count in NVS, and a flag in YAT STATUS the setup site can read.
  g_fsReady = LittleFS.begin(false, "/littlefs", 10, "littlefs");
  if (!g_fsReady) {
    Preferences prefs;
    prefs.begin("yat", false);
    uint32_t formats = prefs.getUInt("fs_fmts", 0) + 1;
    prefs.putUInt("fs_fmts", formats);
    prefs.end();
    LOGF("[fs] littlefs UNMOUNTABLE — formatting (format #%u on this device). Expected on a "
         "first boot after a flash; at any other time every pack and the page list on the "
         "volume have just been lost.\n",
         (unsigned)formats);
    g_fsFormatted = true;
    g_fsReady = LittleFS.begin(true, "/littlefs", 10, "littlefs");
  }
  if (g_fsReady) {
    LOGF("[fs] littlefs mounted%s\n", g_fsFormatted ? " (freshly formatted, reseeding defaults)" : "");
    ensureDefaults();
    conditionClear(COND_CONFIG);
  } else {
    LOGF("[fs] littlefs mount FAILED — config/pack storage unavailable this cycle\n");
    // §11.4a: the volume would not mount even after a format, so this cycle
    // (and every cycle until it does) runs on compiled-in defaults — the
    // household's page list, their Wi-Fi choice of pages, their per-page
    // settings are all unreadable. Nothing on the device can repair that, so
    // it earns the Action banner rather than a log line nobody will ever see.
    conditionSet(COND_CONFIG);
  }
  DeviceConfig cfg = loadConfig(g_fsReady);
  g_pages = cfg.pages;                                    // snapshot for YAT PAGES/USE/STATUS
  g_voiceSeconds = cfg.voiceSeconds;                      // mirror for the voice flow + YAT STATUS
  g_activePageIdx = loadClampedPageIdx(g_pages.size());   // NVS "page_idx", clamped into range
  g_loopOriginEpoch = loadLoopOrigin();                   // NVS "loop_at", auto-rotate's origin

  // Voice keyword matcher cache (YAT SAY, section above): built once here so
  // it's ready for every console window this boot, including one opened from
  // inside runProvisioningMode() below — reads each page's pack file off the
  // just-mounted LittleFS, so it must come after g_fsReady/g_pages, not before.
  buildPageAliasCache();

  // KEY1/KEY2 page cycling (PRD §4.3): applied here, right after the page
  // count is known, rather than in reportWakeReason() (which only sets the
  // g_pageForward/g_pageBack intent — it runs before LittleFS is even
  // mounted). Both wrap; g_activePageIdx is the in-memory mirror of NVS
  // "page_idx" from here on, so every mutation persists immediately.
  //
  // A wake caused by KEY1/KEY2 is the ordinary way a prev/next press happens —
  // the device sleeps between wakes, so the press IS the wake — yet this path
  // had no beep at all until now; only its awake-press twin (keyStepPage() in
  // yat_buttons.cpp) acknowledged. Fixed the same way keyStepPage() does it:
  // ack before the sensor read and WiFi join below, because the press was
  // now and the ~30 s refresh is not.
  if (g_pageForward || g_pageBack) {
    if (g_pages.size() <= 1) {
      beepNoKey();  // nowhere to step — same "noted" cue keyStepPage() uses
    } else {
      beepPageStep();
    }
  }
  if (g_pageForward) {
    g_activePageIdx = (int)((g_activePageIdx + 1) % (int)g_pages.size());
    savePageIdx(g_activePageIdx);
    LOGF("[pages] KEY1 page-forward -> idx %d (\"%s\")\n", g_activePageIdx,
         g_pages[g_activePageIdx].id.c_str());
  }
  if (g_pageBack) {
    g_activePageIdx = (int)((g_activePageIdx - 1 + (int)g_pages.size()) % (int)g_pages.size());
    savePageIdx(g_activePageIdx);
    LOGF("[pages] KEY2 page-back -> idx %d (\"%s\")\n", g_activePageIdx,
         g_pages[g_activePageIdx].id.c_str());
  }

  // Sensors first: the panel needs temperature for waveform compensation, and
  // a bad read here should not cost us the whole cycle.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (sht4xRead(g_temp, g_humi)) {
    LOGF("[sht4x] %.1f C, %.0f %%RH\n", g_temp, g_humi);
  } else {
    LOGF("[sht4x] read failed — using defaults %.1f C / %.0f %%RH\n", g_temp, g_humi);
  }
  epaper.setTemp([]() -> float { return g_temp; });
  epaper.setHumi([]() -> float { return g_humi; });

  // WiFi credentials + the other two provisioning triggers (ARCHITECTURE §8):
  // no NVS credentials yet (seeding once from secrets.h first if it's been
  // filled in), or 3 consecutive connect failures already recorded.
  maybeSeedWifiFromSecrets();
  WifiCreds creds = loadWifiCreds();
  g_keyCreds = creds;  // for a KEY0 tap caught mid-cycle (button-latch sections)
  uint32_t fails = wifiFailCount();

  // G9: a transient outage (router reboot, brief power cut) must not
  // permanently strand the device in provisioning mode once the network is
  // back. Before the fails>=3 trigger commits to tearing down the last-good
  // page and burning battery in AP mode, give the saved credentials one more
  // try in this same boot.
  bool wifiUp = false;
  if (!key0Held && creds.valid && fails >= 3) {
    LOGF("[wifi] %u consecutive failures recorded — retrying saved credentials once before "
         "provisioning\n",
         (unsigned)fails);
    if (wifiConnect(creds.ssid.c_str(), creds.pass.c_str())) {
      LOGF("[wifi] retry succeeded — network is back, staying on saved credentials\n");
      wifiRecordSuccess();
      fails = 0;
      wifiUp = true;
    } else {
      wifiRecordFailure();
      fails = wifiFailCount();
    }
  }

  // The green button is the ONE setup gesture. With credentials already on
  // file it means "let me change what this shows", which is the content portal
  // directly; without them WiFi has to come first, and the portal picks up the
  // moment it lands. The two automatic triggers below are unattended repairs
  // rather than setup requests, so they keep the old behavior of provisioning
  // and then rebooting into the normal cycle — nobody is standing there to
  // finish a content session, and an AP left beaconing at one costs battery.
  if (key0Held) {
    if (battVolts < CONTENT_PORTAL_MIN_VOLTS)
      refuseSetupLowBattery(battVolts, /*userInitiated=*/true);  // [[noreturn]]
    if (creds.valid) runContentPortal(PortalEntry::Gesture);     // [[noreturn]]
  }

  if (key0Held || !creds.valid || fails >= 3) {
    // D1: the floor guards the LOAD, not the gesture — WiFi provisioning runs
    // the same AP plus 30 s refresh the content portal does. It used to be
    // checked only on the paths a person asked for, which left first boot (and
    // the three-failures repair) free to open an AP on a nearly flat battery,
    // the one boot with nothing on the panel to fall back to. key0Held is
    // already past the check above, so anything caught here is automatic and
    // resumes by itself once charged.
    if (battVolts < CONTENT_PORTAL_MIN_VOLTS)
      refuseSetupLowBattery(battVolts, /*userInitiated=*/false);  // [[noreturn]]
    // Drawn on the provisioning screen (drawProvisioningScreen), so it obeys the
    // copy rule too: this is why-you-are-here, not how-long-to-press, and the
    // number never belonged in it. 長按綠色掣 already said it this way.
    const char* reasonEn = key0Held             ? "green button held"
                           : !creds.valid        ? "no WiFi credentials"
                                                 : "3 consecutive WiFi failures";
    const char* reasonZh = key0Held      ? "長按綠色掣"
                           : !creds.valid ? "首次設定"
                                          : "連線失敗 3 次";
    // Only the button means somebody is here; first boot and the three-failures
    // repair are both unattended, and both are what the streak counts.
    runProvisioningMode(reasonZh, reasonEn, /*thenPortal=*/key0Held || !creds.valid,
                        /*userInitiated=*/key0Held);
  }

  // Past every door into setup, so this cycle is genuinely running normally.
  // Two leftovers to clear, both of which runProvisioningMode is still reading
  // further up this function and so could not be cleared any earlier:
  //   - the streak, because a cycle that got here has credentials it believes
  //     in and never entered provisioning;
  //   - the setup screen, if the panel is still holding one. Clearing the
  //     marker alone is not enough, for the same reason it isn't for the help
  //     card: the household's data has very likely not moved while the device
  //     was stuck without a network, and hash-skip would render nothing and
  //     leave 「設定閂咗」 on a device that has been working again for hours.
  clearProvisionStreak();
  if (cardOnScreen == ScreenCard::Provisioning || cardOnScreen == ScreenCard::SetupClosed) {
    LOGF("[provision] setup screen was on the panel and this cycle is not in setup — clearing it "
         "and forcing a render\n");
    saveScreenCard(ScreenCard::None);
    g_cardCleared = true;
  }

  // Both arrows: draw the help card and stop the cycle there. Placed past the
  // provisioning decision for the same reason tap-to-talk is — a device that
  // cannot reach the internet has something more urgent to say than a list of
  // pages, and covering the setup screen with a help card would strand a
  // household mid-setup. Still ahead of the WiFi join below, so the card costs
  // no radio at all: everything on it is already on this device.
  //
  // A press made while the PREVIOUS cycle was awake arrives through NVS rather
  // than through the wake reason (requestHelpNextCycle); taking it here means
  // one request is answered exactly once, and a boot that ends in provisioning
  // instead simply consumes it.
  const bool helpFromThisWake = g_helpRequested;  // before folding in the deferred NVS request below
  if (takeHelpRequest()) g_helpRequested = true;
  if (g_helpRequested) {
    // A deferred request (helpFromThisWake false) was already acknowledged by
    // processKeyLatches()'s beepPageStep() the moment the awake press latched
    // it. A request that IS this wake's cause (both arrows woke the device)
    // has had no sound at all until now — the same gap prev/next had, fixed
    // the same way: ack before the sensor read and WiFi join below.
    if (helpFromThisWake) beepPageStep();
    saveScreenCard(ScreenCard::Help);
    drawHelpCard(batteryPct);
    // Any button wakes the device straight out of this sleep, which is the
    // dismissal the card's own last line describes; HELP_CARD_SLEEP_MIN is
    // only the fallback for nobody coming back. Either way the next cycle
    // draws the real page again — see the ScreenCard::Help branch above, which
    // is what stops hash-skip from putting nothing back.
    deepSleepMinutes(HELP_CARD_SLEEP_MIN, "help card shown");
  }

  // PRD §4.4 tap-to-talk. Placed here, past the provisioning decision, for two
  // reasons: credentials are known-present (so the capture has a network to
  // upload to), and the alias cache + page list it matches against are already
  // built. It runs before the pack load below, so a matched page is the one
  // this cycle renders. Never fatal — see runVoiceCapture().
  if (key0Tap) runVoiceCapture(creds, wifiUp);

  if (!wifiUp) {
    if (!wifiConnect(creds.ssid.c_str(), creds.pass.c_str())) {
      wifiRecordFailure();
      recordError(ERR_WIFI_LOST, nullptr);
      // G5/G12: the network never came up this cycle. A WiFi failure is
      // never itself a "recovered" transition (there's nothing rendered yet
      // to recover from), so the second out-param is discarded here.
      bool unusedRecovered = false;
      bool firstOffline = trackOfflineCycle(/*offlineThisCycle=*/true, unusedRecovered);
      // Also redraw when the panel is holding the OTHER offline card: an
      // outage that starts upstream and then takes the router with it is one
      // unbroken streak, and the no-data card's "your Wi-Fi is fine" must not
      // survive the moment it stops being true.
      if (firstOffline || loadScreenCard() != ScreenCard::Offline) {
        saveScreenCard(ScreenCard::Offline);
        drawOfflineCard();
      }
      deepSleepError("wifi failed");
    }
    wifiRecordSuccess();
  }
  maybeEnterConsole("wifi connect");

  // Warning takeover decision (PRD §4.2/§5.1, docs/UX-FLOWS.md G15) — after
  // WiFi, before any page pack loads, so it can preempt the pack-load choice
  // below. See fetchWarnsumDecision()/the policy comment above it.
  {
    uint8_t takeoverSim = 0;
    bool prevActive = false;
    std::string prevSignal;
    {
      Preferences prefs;
      prefs.begin("yat", true);
      takeoverSim = prefs.getUChar("takeover_sim", 0);
      prevActive = prefs.getUChar("takeover", 0) != 0;
      prevSignal = prefs.getString("takeover_sig", "").c_str();
      prefs.end();
    }

    if (takeoverSim != 0) {
      g_takeoverActive = true;
      g_takeoverSignal = takeoverSim == 1 ? "TC8NE (SIMULATED)" : "WRAINB (SIMULATED)";
      LOGF("[takeover] SIMULATED via YAT TAKEOVER %u — forcing active, signal \"%s\"\n",
           (unsigned)takeoverSim, g_takeoverSignal.c_str());
    } else {
      WarnsumResult wr = fetchWarnsumDecision();
      if (wr.ok) {
        g_takeoverActive = wr.active;
        g_takeoverSignal = wr.signal;
      } else {
        // Never block the cycle on a bad fetch: keep whatever was true last
        // wake rather than guess, so a transient warnsum hiccup mid-typhoon
        // cannot silently drop the takeover page back to the normal playlist.
        g_takeoverActive = prevActive;
        g_takeoverSignal = prevSignal;
      }
    }

    g_takeoverChanged = (g_takeoverActive != prevActive);
    LOGF("[takeover] active=%s signal=\"%s\"%s\n", g_takeoverActive ? "true" : "false",
         g_takeoverSignal.c_str(), g_takeoverChanged ? " (state CHANGED this wake)" : "");

    Preferences prefs;
    prefs.begin("yat", false);
    prefs.putUChar("takeover", g_takeoverActive ? 1 : 0);
    prefs.putString("takeover_sig", g_takeoverSignal.c_str());
    prefs.end();
  }

  bool haveTime = syncTime();
  time_t now = time(nullptr);

  // Auto-rotate (config.json "loop_min"), placed here for everything it gets to
  // depend on: a clock SNTP has just corrected, so the interval and the quiet-
  // hours check are both against real local time; the takeover decision above,
  // so a preempted playlist does not rotate behind the warning; and the pack
  // load below, which reads g_activePageIdx — so an advance costs this cycle's
  // refresh and not one of its own. Cycles that never get this far (no network,
  // provisioning, the help card) do not rotate, which is right: each of them has
  // something else on the panel, and the interval resumes when the device does.
  maybeAdvanceLoopPage(cfg, now, haveTime);

  // Pack: FS is the source of truth from first boot onward, and the ACTIVE
  // page's pack is what gets loaded/rendered/scheduled this cycle. A pack
  // file that's missing, unreadable, or fails engine.load() must not stall
  // the whole cycle: try the next page (wrapping, at most one full loop) and
  // promote whichever page's pack actually loads to be the new active page.
  // If every configured page fails, fall back to the embedded hko-now pack
  // outright — not tied to any page index, the same safety net v0.2 had for
  // a broken hko-now file specifically, generalized to N pages.
  std::string err;
  std::string packJson;
  std::string activePackId;
  yat::Engine eng;
  LittleFsStateStore stateStore;
  if (g_fsReady) eng.setStateStore(&stateStore);  // no FS -> stays null, v0.2 behavior (never stale)
  PngleImageDecoder imgDecoder;
  eng.setImageDecoder(&imgDecoder);
  eng.setBatteryPercent(batteryPct);  // G25: standard-chrome footer glyph
  // §11.4a: the one thing the panel can say that the pack cannot know. Only
  // the Action tier comes from here — the engine derives the other two from
  // its own render (the footer's data-warning glyph) and its own snapshots
  // (the "showing older data" strip), so there is nothing to pass for those.
  //
  // One code at a time, most-urgent first, because the banner is one line and
  // a household given two instructions at once follows neither. Storage before
  // voice on the same reasoning the empty-state card is calm: a device that
  // cannot keep its own settings will keep losing whatever the household fixes
  // next, so it is the repair that has to happen first.
  //
  // Suppressed outright under a warning takeover. That page preempts the
  // playlist because a signal is in force and the panel has exactly one thing
  // to say (PRD §4.2/§5.1); a banner about an API key sharing the top of it
  // would be the device talking over itself at the one moment it must not. The
  // condition is not cleared, only unsaid — it is back on the next ordinary
  // cycle, and the setup portal's issues card never stopped listing it.
  {
    const uint32_t cond = g_takeoverActive ? 0u : conditionsActive();
    eng.setNotice(cond & COND_CONFIG    ? yat::NoticeCode::ConfigUnreadable
                  : cond & COND_STORAGE ? yat::NoticeCode::StorageFailed
                  : cond & COND_VOICE_KEY ? yat::NoticeCode::VoiceKeyRejected
                                          : yat::NoticeCode::None);
    if (cond) LOGF("[cond] active conditions 0x%x — chrome will carry the top banner\n", (unsigned)cond);
  }

  const size_t numPages = g_pages.size();
  bool loaded = false;
  bool loadedFromFs = false;

  if (g_takeoverActive) {
    // Warning takeover (PRD §4.2/§5.1, G15): preempt the playlist entirely —
    // the active page's own pack is not even attempted this cycle. Embedded
    // exactly like the hko-now fallback below, so it never depends on
    // LittleFS holding a valid pack file.
    packJson.assign(WARNING_TAKEOVER_PACK);
    if (eng.load(packJson.c_str(), nullptr, err)) {
      activePackId = "warning-takeover";
      loaded = true;
      LOGF("[takeover] rendering embedded warning-takeover pack (signal \"%s\")\n",
           g_takeoverSignal.c_str());
    } else {
      LOGF("[takeover] embedded warning-takeover pack failed to load (%s) — falling back to the "
           "normal playlist this cycle\n",
           err.c_str());
    }
  }

  const int startIdx = g_activePageIdx;
  if (!loaded) {
    for (size_t attempt = 0; attempt < numPages; attempt++) {
      size_t idx = ((size_t)startIdx + attempt) % numPages;
      const ConfigPage& page = g_pages[idx];
      std::string candidateJson;
      std::string candidatePath = "/packs/" + page.pack + ".yat-pack.json";
      bool gotFromFs = g_fsReady && littlefsReadFile(candidatePath.c_str(), candidateJson);
      if (!gotFromFs) {
        LOGF("[pages] page %u (\"%s\") pack \"%s\" not found/unreadable — trying next page\n",
             (unsigned)idx, page.id.c_str(), page.pack.c_str());
        continue;
      }
      std::string loadErr;
      const char* paramsArg = page.paramsJson.empty() ? nullptr : page.paramsJson.c_str();
      if (!eng.load(candidateJson.c_str(), paramsArg, loadErr)) {
        LOGF("[pages] page %u (\"%s\") pack \"%s\" failed to load (%s) — trying next page\n",
             (unsigned)idx, page.id.c_str(), page.pack.c_str(), loadErr.c_str());
        continue;
      }
      packJson = std::move(candidateJson);
      activePackId = page.pack;
      loaded = true;
      loadedFromFs = true;
      if ((int)idx != g_activePageIdx) {
        LOGF("[pages] active page %d unusable — promoting page %u (\"%s\") to active\n",
             g_activePageIdx, (unsigned)idx, page.id.c_str());
        g_activePageIdx = (int)idx;
        savePageIdx(g_activePageIdx);
      }
      break;
    }
  }

  if (!loaded) {
    LOGF("[pages] all %u page(s) failed to load — falling back to embedded hko-now pack\n",
         (unsigned)numPages);
    packJson.assign(HKO_NOW_PACK);
    if (!eng.load(packJson.c_str(), nullptr, err)) {
      LOGF("[engine] embedded hko-now fallback load failed: %s\n", err.c_str());
      deepSleepError("pack load failed");
    }
    activePackId = "hko-now";
    // Active page index is left untouched here: none of the configured pages
    // worked, so there's nothing valid to promote — the next wake tries the
    // same page list again, in case a pack file gets fixed via the serial
    // protocol during this cycle's console windows.
  }
  LOGF("[engine] pack loaded (%s, %u bytes) — page %d/%u \"%s\", pack \"%s\"\n",
       loadedFromFs ? "littlefs" : "embedded", (unsigned)packJson.size(), g_activePageIdx,
       (unsigned)numPages,
       loadedFromFs ? g_pages[g_activePageIdx].id.c_str() : (g_takeoverActive ? "(takeover)" : "(fallback)"),
       activePackId.c_str());
  // Point of no return for the page buttons: from here the loaded pack, not
  // g_activePageIdx, is what this cycle renders, so a press caught at any
  // later checkpoint gets its own cycle instead (see lowLevelSleep).
  g_packCommitted = true;

  // What this cycle will claim to have drawn (see g_renderOk). Only a pack that
  // came off the filesystem is one of the household's pages: a warning takeover
  // and the embedded hko-now fallback both preempt the playlist, so neither can
  // honestly answer "yes, that is your page on the screen".
  const std::string drawnPage = loadedFromFs ? g_pages[g_activePageIdx].id : std::string();

  // Name the page every source failure in the block below belongs to. A pack
  // that came off the filesystem is one of the household's own pages and is
  // worth naming; the takeover and hko-now fallbacks are not pages anyone
  // picked, so those rows say only that a fetch failed.
  errCopyField(g_fetchErrPage, sizeof g_fetchErrPage, drawnPage.c_str());
  g_fetchErrRecord = true;
  // fetchPackSource(), not httpsFetch() directly: this is the loaded pack's
  // own declared source URLs, which need the private/loopback-range guard
  // (yat_net.cpp) that firmware's own fetches don't.
  const bool extractOk = eng.fetchAndExtract(fetchPackSource, now, err);
  g_fetchErrRecord = false;
  if (!extractOk) {
    LOGF("[engine] extract failed: %s\n", err.c_str());
    deepSleepError("fetch/extract failed");
  }
  if (!err.empty()) LOGF("[engine] warn: %s\n", err.c_str());
  maybeEnterConsole("fetch");

  {
    std::string dataStr;
    serializeJson(eng.data(), dataStr);
    LOGF("[engine] data %s\n", dataStr.c_str());
  }

  // G5/G12: every source failed and nothing -- not even a stale snapshot --
  // came back. eng.anyStale() is deliberately excluded: a stale-serve cycle
  // has old data plus the footer's own "stale" marker to show for it, which
  // is the right, calmer signal for that case (docs/UX-FLOWS.md G18) -- this
  // card is only for a cycle that would otherwise show nothing new AND has
  // nothing old to fall back on either. Distinct from the hard failure above
  // (a pack/grammar bug): fetchAndExtract() returned true here, just with
  // nothing usable in data().
  bool offlineNoData = !err.empty() && !eng.anyStale() && engineDataAllNull(eng.data());
  bool offlineRecovered = false;
  const bool firstOffline = trackOfflineCycle(offlineNoData, offlineRecovered);
  if (offlineNoData) {
    // Which card depends on what actually failed. The cycle got this far on a
    // joined network, so an empty result is normally the pack's sources being
    // down, not the household's WiFi — and the offline card's "check your WiFi,
    // hold the green button to set up again" would send someone to re-provision
    // a connection that is working. Re-checked rather than assumed: the link can
    // drop between the join and here, and "your Wi-Fi is fine" has to be true
    // when the panel says it.
    const bool wifiOk = WiFi.status() == WL_CONNECTED;
    // The link was up at the join and is not up now: the network went away
    // between the two, which is a different fault from "it never came up" only
    // in when it happened, and the same one to the household.
    if (!wifiOk) recordError(ERR_WIFI_LOST, nullptr);
    const ScreenCard want = wifiOk ? ScreenCard::NoData : ScreenCard::Offline;
    // First cycle of a streak draws (G5's "only render once"); so does a cycle
    // where the streak persists but the failure moved between the two cards,
    // which is a different thing to tell the household.
    if (firstOffline || loadScreenCard() != want) {
      saveScreenCard(want);
      if (wifiOk) {
        drawNoDataCard(loadedFromFs ? g_pages[g_activePageIdx].id.c_str() : activePackId.c_str());
      } else {
        drawOfflineCard();
      }
      deepSleepError("offline (no data)");
    }
    // Same streak, same card already on the panel -- spend nothing more on it.
    deepSleepError("offline (no data), card unchanged");
  }

  // Hash-skip: the single biggest battery lever (ARCHITECTURE §2). Unchanged
  // data means no 30 s panel refresh, which is most of a cycle's energy.
  uint32_t hash = eng.dataHash(now);
  g_lastHash = hash;
  Preferences prefs;
  prefs.begin("yat", false);
  uint32_t prevHash = prefs.getUInt("hash", 0);
  LOGF("[hash] now %08x, stored %08x\n", hash, prevHash);

  // G18: the red stale badge is chrome the hash cannot see. A source that fails
  // is served from its last snapshot, so data() — and therefore the hash — comes
  // back byte-identical to the fresh cycle before it, and the one render that
  // would have put the badge up is exactly the one hash-skip throws away. The
  // panel then shows hours-old numbers with no marking at all. Track the badge's
  // own state next to the hash and treat a flip either way as a render reason.
  const bool anyStale = eng.anyStale();
  const bool prevStale = prefs.getUChar("was_stale", 0) != 0;
  g_staleChanged = anyStale != prevStale;

  JsonVariantConst schedule = eng.pack()["schedule"];

  if (haveTime && !g_buttonWake && !g_coldBootWake && !g_serialForceRender && !g_keyForceRender &&
      !g_takeoverChanged && !offlineRecovered && !g_cardCleared && !g_staleChanged &&
      !g_loopAdvanced && prevHash == hash) {
    clearErrorStreak(prefs);  // fetched, extracted, and the panel is already right
    prefs.end();
    LOGF("[hash] hash-skip — data unchanged, no render\n");
    SleepPlan plan = computeSleepPlan(schedule, cfg, time(nullptr), haveTime);
    clampForTakeover(plan);
    LOGF("[sched] %s\n", plan.reason.c_str());
    deepSleepScheduled(plan.seconds, "hash-skip");
  }
  if (g_buttonWake && prevHash == hash)
    LOGF("[hash] unchanged, but button wake — rendering anyway\n");
  // G14: cold boot (power-on/brownout/post-provisioning/post-OTA) must never
  // hash-skip — see g_coldBootWake's declaration for why.
  if (g_coldBootWake && prevHash == hash)
    LOGF("[hash] unchanged, but cold boot (power-on/brownout) — rendering anyway\n");
  if (g_serialForceRender && prevHash == hash)
    LOGF("[hash] unchanged, but YAT USE forced a render — rendering anyway\n");
  // A page button pressed while this cycle was awake, early enough that the
  // pack it chose is the one loaded above. Two pages can share data sources,
  // so an equal hash proves nothing about the page: render.
  if (g_keyForceRender && prevHash == hash)
    LOGF("[hash] unchanged, but a page button was pressed this cycle — rendering anyway\n");
  // Warning takeover ACTIVATED or DEACTIVATED this wake must always render —
  // the whole point is that the household sees the change immediately rather
  // than on the next cadence tick. While takeover merely PERSISTS unchanged
  // across wakes, ordinary hash-skip on the takeover pack's own data applies
  // normally (no need to redraw an unchanged signal every 15 min).
  if (g_takeoverChanged && prevHash == hash)
    LOGF("[hash] unchanged, but takeover state changed this wake — rendering anyway\n");
  // G5/G12: coming back online after an offline streak must always render --
  // the offline card, if one was drawn, needs to be replaced with the real
  // page immediately rather than left on screen until data happens to change.
  if (offlineRecovered && prevHash == hash)
    LOGF("[hash] unchanged, but back online after an offline streak — rendering anyway\n");
  // A low-battery or setup-refused card is still on the panel and its condition
  // has cleared — the render is what takes it off.
  if (g_cardCleared && prevHash == hash)
    LOGF("[hash] unchanged, but an on-screen card needs clearing — rendering anyway\n");
  // Auto-rotate moved to a different page this cycle. The stored hash belongs to
  // the page that WAS on the panel, so an equal one here says nothing about the
  // page about to be: two views of the same feed hash alike, and so does any
  // page whose numbers happen not to have moved. Skipping would leave the panel
  // on the page the rotation just left.
  if (g_loopAdvanced && prevHash == hash)
    LOGF("[hash] unchanged, but auto-rotate advanced to a different page — rendering anyway\n");
  // The stale badge went up or came down; identical data, different chrome.
  if (g_staleChanged && prevHash == hash)
    LOGF("[hash] unchanged, but the stale badge %s this wake — rendering anyway\n",
         anyStale ? "appeared" : "cleared");
  // Without a synced clock the chrome clock would be wrong, so render anyway
  // but do not let a stale hash suppress the next attempt.

  LOGF("[epd] init...\n");
  epaper.begin();
  g_panelReady = true;

  EPaperCanvas canvas;
  yat::DefaultFontProvider font;
  uint32_t tr = millis();
  if (!eng.render(canvas, font, now, err, kPanelProfile)) {
    prefs.end();
    LOGF("[engine] render failed: %s\n", err.c_str());
    // The panel keeps whatever it was already holding, so this is the one
    // outcome where the record has to contradict it: the page was tried and it
    // would not draw (audit #23). Naming it is what lets the setup site say so.
    noteRenderResult(false, drawnPage);
    deepSleepError("render failed");
  }
  LOGF("[render] %lu ms\n", (unsigned long)(millis() - tr));
  // The deepest point in an ordinary cycle, and the one worth watching: this is
  // measured against whichever pack the household actually configured, not
  // against the ones in packs/examples.
  logStackHeadroom("engine render");
  if (eng.anyStale()) {
    time_t oldest = eng.oldestStaleSince();
    struct tm st;
    localtime_r(&oldest, &st);
    LOGF("[state] stale since %02d:%02d\n", st.tm_hour, st.tm_min);
  }

  panelRefresh(drawnPage);
  maybeEnterConsole("render/refresh");

  prefs.putUInt("hash", hash);
  // Both written here, with the image already on the panel, for the same reason
  // the hash is: these record what the screen IS showing, not what this cycle
  // hoped to show. A render that bailed above leaves the previous values, which
  // still describe the previous image.
  prefs.putUChar("was_stale", anyStale ? 1 : 0);
  clearErrorStreak(prefs);  // content is on the panel: whatever was broken, isn't
  // Same reasoning, and this is the only place that can honestly say it: a cycle
  // that loaded a pack, rendered it and drove the panel to completion is proof
  // the fault behind any recorded crash streak did not happen this time.
  clearPanicStreak(prefs, "cycle completed with content on the panel");
  prefs.end();
  saveScreenCard(ScreenCard::None);  // real content now, whatever card came before

  LOGF("[power] battery %.2f V after refresh\n", batteryVolts());
  // Whole-cycle worst case: TLS handshake, JSON parse and render all folded in.
  logStackHeadroom("the whole cycle");
  LOGF("[mem] heap %u free\n", (unsigned)ESP.getFreeHeap());

  SleepPlan plan = computeSleepPlan(schedule, cfg, time(nullptr), haveTime);
  clampForTakeover(plan);
  LOGF("[sched] %s\n", plan.reason.c_str());
  deepSleepScheduled(plan.seconds, "cycle complete");
}

void loop() {
  // Unreachable: setup() always ends in deep sleep. Present so a future
  // firmware bug cannot leave the device spinning at full power.
  deepSleepMinutes(RETRY_MIN, "fell through to loop()");
}
