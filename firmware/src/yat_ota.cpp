// Firmware update over the household's own WiFi.
//
// The errand this removes is the whole point: until now, moving a device onto
// new firmware meant a computer, a USB cable and a web flasher — which for the
// households this is built for means it never happens. Here the phone that is
// already in setup mode notices a newer release, offers 更新, and the device
// fetches it itself.
//
// WHO CHECKS FOR VERSIONS. Not this file, and not the device at all. The
// PHONE does the check, against api.github.com, and only while the portal is
// open in front of somebody. Nothing on the device polls for updates, phones
// home, or reports its version anywhere: a shelf device that quietly talks to
// a server we run is exactly what this product promises not to be. The device
// learns a release exists by being told a tag by the person holding the phone.
//
// WHAT THAT MEANS FOR TRUST. Being told a tag by a LAN peer is a much smaller
// thing to trust than being told a URL, and the difference is this section:
//
//   - the request carries ONLY a tag, and the tag must match
//     ^v[0-9]+\.[0-9]+\.[0-9]+$ before it is looked at twice;
//   - the URL is built HERE, from constants in this file, and can therefore
//     only ever be a release asset of github.com/yat-hk/yat;
//   - both hosts in the redirect chain are verified against the pinned
//     anchors in certs.h (no setInsecure() anywhere in this firmware);
//   - what lands is written to the INACTIVE app slot and validated by
//     esp_ota_end() before anything is pointed at it.
//
// KNOWN RESIDUAL, written down rather than papered over: the portal itself is
// unauthenticated on the LAN — anyone already on the household's WiFi can open
// it, which is a property of the whole portal and not of this route. So a LAN
// peer can make the device install a DIFFERENT, OLDER, but still genuine YAT
// release: a downgrade to a legitimately signed image. It cannot install a
// binary of its own, which is the attack worth stopping. Closing the downgrade
// hole needs either anti-rollback efuses (one-way, and it would brick a device
// on any release we had to pull) or an authenticated portal, and both are
// bigger decisions than this file. Reported, not fixed.

#include "yat_ota.h"

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "certs.h"

#include "yat_beep.h"
#include "yat_common.h"
#include "yat_errors.h"
#include "yat_hw.h"
#include "yat_sched.h"
#include "yat_screens.h"
#include "yat_state.h"

// ---------------- what may be fetched ----------------
//
// The whole of the device's freedom in constructing a URL. A request supplies
// the tag and nothing else, so these three lines are the complete set of
// places an update can come from.
//
// The asset name matches .github/workflows/release.yml, which publishes
// firmware-<model>.bin (the OTA app image) alongside firmware-<model>.factory
// .bin (bootloader + partitions + app, for a USB/UF2 first flash). It is the
// APP image that belongs here: the factory image starts at flash offset 0 and
// would be meaningless written into an app slot.
//
// <model> is this build's own YAT_MODEL_NAME (yat_common.h), so an E1001 asks
// for firmware-e1001.bin and an E1002 for firmware-e1002.bin. That is the
// whole of the model-safety here, and it has to be: the release matrix
// publishes both files side by side under one tag, and an E1001 that fetched
// the E1002 image would flash a binary compiled against a different panel
// driver onto a device whose only remaining way out is a USB cable.
static const char* const OTA_HOST = "github.com";
// Build-flag override exists for OTA rehearsals only: a test build can point
// at a throwaway repo's releases without touching this file. Production builds
// never set it, so the shipped constant remains the sealed yat-hk/yat path.
#ifndef YAT_OTA_REPO_PATH
#define YAT_OTA_REPO_PATH "/yat-hk/yat/releases/download"
#endif
static const char* const OTA_REPO_PATH = YAT_OTA_REPO_PATH;
static const char* const OTA_ASSET = "firmware-" YAT_MODEL_NAME ".bin";

const char* otaRepoSlug() {
  // "/yat-hk/yat/releases/download" -> "yat-hk/yat", computed once. The
  // constant's shape is fixed at compile time, so the string surgery here is
  // mechanical: drop the leading slash, cut at "/releases".
  static char slug[64] = {0};
  if (!slug[0]) {
    const char* p = OTA_REPO_PATH;
    if (*p == '/') p++;
    const char* cut = strstr(p, "/releases");
    size_t n = cut ? (size_t)(cut - p) : strlen(p);
    if (n >= sizeof slug) n = sizeof slug - 1;
    memcpy(slug, p, n);
    slug[n] = 0;
  }
  return slug;
}

// A redirect may only land inside GitHub's own asset domain. Today that is
// release-assets.githubusercontent.com; it was objects.githubusercontent.com
// until GitHub moved release downloads, and pinning the exact host once
// already meant a firmware that could not update itself. The suffix is the
// real boundary — combined with the CA pinning below, a Location header can
// only ever send this device to a host GitHub holds a certificate for.
static const char* const OTA_REDIRECT_SUFFIX = ".githubusercontent.com";

// The image cannot be larger than the slot it is going into (6 MB, see
// partitions_ota_32mb.csv), and a "firmware" of a few KB is an error page or a
// truncated upload rather than something to erase a working app slot for. The
// real image is ~2.95 MB as of v0.3.
static constexpr size_t OTA_MIN_IMAGE = 256 * 1024;

// How much of the body to move per otaPump(). The portal loop must come back
// round to server.handleClient() often enough that the phone's progress poll
// is answered rather than left in the TCP backlog, and 16 KB off an already-
// buffered TLS socket is a few milliseconds. It also bounds the stall when the
// far end goes quiet: read() is non-blocking here, so a silent socket costs one
// loop turn, not a timeout.
static constexpr size_t OTA_CHUNK = 16 * 1024;

// A download that stops moving. GitHub's CDN is fast enough that 30 s of no
// bytes at all means the connection is gone, whatever the socket still thinks.
static constexpr uint32_t OTA_STALL_MS = 30000;

// How long to hold the finished "ok" state before rebooting, when the phone
// never polls again to collect it. The portal normally polls every 1.5 s and
// otaOkWasSeen() cuts this short; this is the ceiling for a phone that walked
// out of WiFi range at exactly the wrong moment.
static constexpr uint32_t OTA_OK_LINGER_MS = 6000;

const char* const OTA_ERR_BADTAG = "badtag";
const char* const OTA_ERR_BUSY = "busy";
const char* const OTA_ERR_OFFLINE = "offline";
const char* const OTA_ERR_NOTFOUND = "notfound";
const char* const OTA_ERR_HTTP = "http";
const char* const OTA_ERR_REDIRECT = "redirect";
const char* const OTA_ERR_TOOBIG = "toobig";
const char* const OTA_ERR_BADIMAGE = "badimage";
const char* const OTA_ERR_FLASH = "flash";

// ---------------- state ----------------
//
// One update at a time, all of it driven from the portal's single-threaded
// loop, so plain statics are the whole synchronisation story. Nothing here is
// touched from an interrupt or a second task.

static OtaState g_state = OtaState::Idle;
static char g_tag[16] = {0};
static char g_detail[16] = {0};
static size_t g_total = 0;   // Content-Length of the image, once known
static size_t g_written = 0;  // bytes committed to flash
static uint32_t g_lastByteMs = 0;
static uint32_t g_okSinceMs = 0;
static bool g_okSeen = false;
static bool g_cardPending = false;  // the portal has not yet drawn the card

// Held open across pumps for the duration of one download. Heap rather than
// statics because a WiFiClientSecure carries an mbedTLS context of tens of KB
// and this path is used for a couple of minutes in the life of a device.
static WiFiClientSecure* g_client = nullptr;
static HTTPClient* g_http = nullptr;
static esp_ota_handle_t g_handle = 0;
static bool g_handleOpen = false;
static const esp_partition_t* g_target = nullptr;

// ---------------- accessors ----------------

OtaState otaState() { return g_state; }
const char* otaTag() { return g_tag; }
const char* otaDetail() { return g_detail; }

int otaPercent() {
  if (!g_total) return 0;
  uint64_t pct = (uint64_t)g_written * 100ULL / (uint64_t)g_total;
  return pct > 100 ? 100 : (int)pct;
}

bool otaBusy() {
  return g_state == OtaState::Drawing || g_state == OtaState::Downloading ||
         g_state == OtaState::Verifying || g_state == OtaState::Ok;
}

bool otaWantsCard() { return g_cardPending; }
void otaCardDrawn() {
  g_cardPending = false;
  if (g_state == OtaState::Drawing) g_state = OtaState::Downloading;
}
void otaOkWasSeen() { g_okSeen = true; }

// ---------------- teardown ----------------

// Every failure exit and the success exit both come through here, so there is
// exactly one place that can leak a TLS context or leave an OTA handle open.
// esp_ota_abort() is what makes an interrupted download harmless: it releases
// the handle WITHOUT touching otadata, so the boot partition still points at
// the app that is running this code. The half-written bytes sit in the
// inactive slot until the next update erases them again, which is precisely
// what that slot is for.
static void otaRelease() {
  if (g_handleOpen) {
    esp_ota_abort(g_handle);
    g_handleOpen = false;
  }
  if (g_http) {
    g_http->end();
    delete g_http;
    g_http = nullptr;
  }
  if (g_client) {
    delete g_client;
    g_client = nullptr;
  }
}

// `detail` is one of the OTA_ERR_* tokens and never anything from the wire: a
// server's own words are a debugging detail, and the household's phone has a
// sentence prepared for each of these nine. The tag is safe to log — it got
// past otaTagValid() — but nothing else from the request ever is, and neither
// is the redirect URL (see fetchRedirected).
static void otaFail(const char* detail) {
  otaRelease();
  errCopyField(g_detail, sizeof g_detail, detail);
  g_state = OtaState::Error;
  g_cardPending = false;
  LOGF("[ota] update to %s failed: %s\n", g_tag[0] ? g_tag : "?", g_detail);
  recordError(ERR_OTA, g_detail);
  beepError();
}

// ---------------- the tag ----------------

// ^v[0-9]+\.[0-9]+\.[0-9]+$, by hand — there is no regex engine on the device
// and this is the single check standing between a LAN request and a URL.
// Deliberately strict about the things a looser reader would wave through:
// no leading zeros rule (harmless), but also no empty components, no fourth
// component, no pre-release suffix, and a hard length cap, so nothing here can
// walk off the end of the 16-byte buffer it is copied into.
bool otaTagValid(const char* tag) {
  if (!tag || tag[0] != 'v') return false;
  const size_t n = strnlen(tag, sizeof g_tag);
  if (n >= sizeof g_tag) return false;  // 15 chars is v99999.99999.9 and then some
  size_t i = 1;
  for (int part = 0; part < 3; part++) {
    if (part) {
      if (tag[i] != '.') return false;
      i++;
    }
    size_t digits = 0;
    while (tag[i] >= '0' && tag[i] <= '9') {
      i++;
      digits++;
    }
    if (!digits || digits > 5) return false;
  }
  return tag[i] == '\0';
}

bool otaArm(const char* tag) {
  if (otaBusy()) {
    errCopyField(g_detail, sizeof g_detail, OTA_ERR_BUSY);
    return false;
  }
  if (!otaTagValid(tag)) {
    errCopyField(g_detail, sizeof g_detail, OTA_ERR_BADTAG);
    return false;
  }
  // The AP is not a route to GitHub. A phone that reached the portal over the
  // device's own access point (no home WiFi, or credentials that stopped
  // working) can see this button but the download behind it cannot happen, and
  // saying so now is better than erasing nothing and failing in ten seconds.
  if (WiFi.status() != WL_CONNECTED) {
    errCopyField(g_detail, sizeof g_detail, OTA_ERR_OFFLINE);
    return false;
  }

  errCopyField(g_tag, sizeof g_tag, tag);
  g_detail[0] = 0;
  g_total = 0;
  g_written = 0;
  g_okSeen = false;
  g_okSinceMs = 0;
  // The card goes up BEFORE a byte is fetched, not after the download
  // finishes. A panel refresh takes about thirty seconds and the download
  // takes minutes: drawing first means the ink says 「更新緊韌體」 for the
  // whole of the wait, which is the only window in which a household could
  // otherwise decide the device has died and pull its power. Drawing at the
  // end would put the card up during the reboot it is meant to explain and
  // then immediately have the new firmware's first cycle paint over it.
  g_state = OtaState::Drawing;
  g_cardPending = true;
  LOGF("[ota] update to %s accepted\n", g_tag);
  return true;
}

// ---------------- the download ----------------

// The Location header of the 302, split into a host and the rest, with the
// checks that make a redirect safe to follow. Nothing here trusts a length or
// a delimiter it has not seen: `url` is a String off the wire and may be any
// shape at all.
//
// Rejecting userinfo outright is what closes the one genuinely confusing case:
// in https://release-assets.githubusercontent.com@evil.example/x the host is
// evil.example, and a reader that stops at the first plausible hostname gets
// it exactly backwards. GitHub has never sent userinfo, so refusing every '@'
// costs nothing and removes the need to parse it correctly.
static bool splitRedirect(const String& url, String& hostOut, String& pathOut) {
  if (!url.startsWith("https://")) return false;
  const int authStart = 8;  // strlen("https://")
  int authEnd = url.length();
  for (int i = authStart; i < (int)url.length(); i++) {
    const char c = url[i];
    if (c == '/' || c == '?' || c == '#') {
      authEnd = i;
      break;
    }
  }
  String host = url.substring(authStart, authEnd);
  if (host.length() == 0 || host.length() > 128) return false;
  if (host.indexOf('@') >= 0) return false;  // see above
  if (host.indexOf(':') >= 0) return false;  // no port games; GitHub sends none
  if (!host.endsWith(OTA_REDIRECT_SUFFIX)) return false;
  hostOut = host;
  pathOut = authEnd < (int)url.length() ? url.substring(authEnd) : String("/");
  return true;
}

// Opens the body of the release asset, following GitHub's one redirect by
// hand. Both requests verify their server against the pinned bundle in
// certs.h: github.com terminates on Sectigo Root E46 (anchor 9) and the asset
// CDN on ISRG Root X1 by way of the cross-signed ISRG Root YR (anchor 3).
//
// Following the redirect explicitly rather than with HTTPClient's
// setFollowRedirects() is not caution for its own sake — it is the only way
// the host check above happens at all, and it keeps this path shaped like
// httpsFetch(), which also disables redirects and for the same reason.
//
// The redirect URL is NEVER logged. It carries a signed SAS query string and a
// bearer JWT that are good for the next hour, and a serial log is a thing
// people paste into support threads.
static bool openAssetStream() {
  char url[160];
  snprintf(url, sizeof url, "https://%s%s/%s/%s", OTA_HOST, OTA_REPO_PATH, g_tag, OTA_ASSET);
  LOGF("[ota] GET %s\n", url);

  g_client = new WiFiClientSecure();
  if (!g_client) {
    otaFail(OTA_ERR_OFFLINE);
    return false;
  }
  g_client->setCACert(YAT_CA_BUNDLE);
  g_client->setTimeout(20);

  g_http = new HTTPClient();
  if (!g_http) {
    otaFail(OTA_ERR_OFFLINE);
    return false;
  }
  g_http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  g_http->setConnectTimeout(20000);
  g_http->setTimeout(20000);
  g_http->setUserAgent("yat/0.4 (ota)");
  // HTTPClient discards every response header it was not asked for in advance,
  // so the redirect this whole function exists to follow would otherwise read
  // back as an empty string.
  static const char* kWanted[] = {"Location"};
  g_http->collectHeaders(kWanted, 1);

  if (!g_http->begin(*g_client, String(url))) {
    otaFail(OTA_ERR_OFFLINE);
    return false;
  }
  int code = g_http->GET();
  if (code == HTTP_CODE_NOT_FOUND) {
    // No release under that tag, or a release that carries no image for this
    // model. Both are "the update you were offered is not there", and the
    // phone's copy says exactly that rather than blaming the network.
    otaFail(OTA_ERR_NOTFOUND);
    return false;
  }
  if (code < 0) {
    otaFail(OTA_ERR_OFFLINE);
    return false;
  }

  // GitHub answers a release download with a 302 to its asset CDN. 200 is
  // accepted too so that a future GitHub that serves the bytes directly does
  // not need a firmware change to keep working.
  if (code == HTTP_CODE_FOUND || code == HTTP_CODE_MOVED_PERMANENTLY ||
      code == HTTP_CODE_TEMPORARY_REDIRECT || code == HTTP_CODE_PERMANENT_REDIRECT) {
    String host, path;
    if (!splitRedirect(g_http->header("Location"), host, path)) {
      otaFail(OTA_ERR_REDIRECT);
      return false;
    }
    LOGF("[ota] 302 -> %s (asset CDN)\n", host.c_str());  // host only, never the signed URL

    // A second host means a second TLS session; the old one is closed rather
    // than left to be garbage collected on a device with ~300 KB of internal
    // heap. The new one is pinned against the same bundle, which is the half
    // of this that actually matters: the redirect target is verified, not
    // merely followed.
    g_http->end();
    delete g_http;
    g_http = nullptr;
    delete g_client;
    g_client = nullptr;

    g_client = new WiFiClientSecure();
    g_http = new HTTPClient();
    if (!g_client || !g_http) {
      otaFail(OTA_ERR_OFFLINE);
      return false;
    }
    g_client->setCACert(YAT_CA_BUNDLE);
    g_client->setTimeout(20);
    g_http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    g_http->setConnectTimeout(20000);
    g_http->setTimeout(20000);
    g_http->setUserAgent("yat/0.4 (ota)");
    if (!g_http->begin(*g_client, "https://" + host + path)) {
      otaFail(OTA_ERR_OFFLINE);
      return false;
    }
    code = g_http->GET();
    if (code < 0) {
      otaFail(OTA_ERR_OFFLINE);
      return false;
    }
  }

  if (code != HTTP_CODE_OK) {
    LOGF("[ota] asset request answered HTTP %d\n", code);
    otaFail(code == HTTP_CODE_NOT_FOUND ? OTA_ERR_NOTFOUND : OTA_ERR_HTTP);
    return false;
  }

  const int len = g_http->getSize();
  if (len <= 0) {
    // esp_ota_begin() can erase the whole slot for an unknown size, but an
    // app image with no Content-Length is not something GitHub serves, and
    // guessing here would mean erasing 6 MB on the word of a chunked reply.
    otaFail(OTA_ERR_HTTP);
    return false;
  }
  g_total = (size_t)len;
  return true;
}

// Why esp_ota_* directly rather than Arduino's Update.h or esp_https_ota:
//
//   - Update.h writes with esp_partition_write() and only calls into the OTA
//     layer at the very end (esp_ota_set_boot_partition), so it never puts the
//     image through esp_ota_end()'s esp_image_verify() — its own check is the
//     0xE9 magic byte of the first sector. On a path whose whole job is "do
//     not brick the device", verifying the image is the point.
//   - esp_https_ota owns its own transfer loop. It would either block this
//     task for the whole download — which stops the portal answering the very
//     progress poll this feature promises — or need its non-blocking handle
//     API, at which point it is the same amount of code with a second HTTP
//     stack and no explicit redirect check.
//   - esp_ota_begin() is also the documented rollback-aware entry point: it
//     refuses to start while the running app is still PENDING_VERIFY, which is
//     a state this firmware can genuinely be in (see otaMarkAppValidIfPending).
static bool openFlashSlot() {
  g_target = esp_ota_get_next_update_partition(nullptr);
  if (!g_target) {
    otaFail(OTA_ERR_FLASH);
    return false;
  }
  if (g_total > g_target->size || g_total < OTA_MIN_IMAGE) {
    LOGF("[ota] image is %u bytes; the %s slot holds %u\n", (unsigned)g_total, g_target->label,
         (unsigned)g_target->size);
    otaFail(OTA_ERR_TOOBIG);
    return false;
  }
  LOGF("[ota] writing %u bytes into %s (running from %s)\n", (unsigned)g_total, g_target->label,
       esp_ota_get_running_partition() ? esp_ota_get_running_partition()->label : "?");

  esp_err_t err = esp_ota_begin(g_target, g_total, &g_handle);
  if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
    // The running app has not been confirmed yet, which can only mean this is
    // the first cycle after an update and the household has come straight back
    // into setup to install another one. The app is plainly working — it is
    // serving this request — so confirming it is both true and the only way
    // forward.
    LOGF("[ota] running image was still pending; confirming it before starting another update\n");
    esp_ota_mark_app_valid_cancel_rollback();
    err = esp_ota_begin(g_target, g_total, &g_handle);
  }
  if (err != ESP_OK) {
    LOGF("[ota] esp_ota_begin failed: %s\n", esp_err_to_name(err));
    otaFail(OTA_ERR_FLASH);
    return false;
  }
  g_handleOpen = true;
  g_lastByteMs = millis();
  return true;
}

// One slice of body -> flash. Returns false when the update is over, either
// way, so the caller stops.
static bool pumpBody() {
  WiFiClient* stream = g_http->getStreamPtr();
  if (!stream) {
    otaFail(OTA_ERR_OFFLINE);
    return false;
  }

  static uint8_t buf[OTA_CHUNK];
  size_t moved = 0;
  while (moved < OTA_CHUNK && g_written < g_total) {
    const int avail = stream->available();
    if (avail <= 0) break;
    size_t want = OTA_CHUNK - moved;
    if (want > (size_t)avail) want = (size_t)avail;
    if (want > g_total - g_written) want = g_total - g_written;
    const int got = stream->readBytes(buf, want);
    if (got <= 0) break;
    const esp_err_t err = esp_ota_write(g_handle, buf, (size_t)got);
    if (err != ESP_OK) {
      LOGF("[ota] esp_ota_write failed at %u bytes: %s\n", (unsigned)g_written,
           esp_err_to_name(err));
      otaFail(OTA_ERR_FLASH);
      return false;
    }
    g_written += (size_t)got;
    moved += (size_t)got;
  }

  if (moved) {
    g_lastByteMs = millis();
    // Every ~10% rather than every chunk: at 16 KB a slice a 3 MB image is
    // nearly 200 pumps, and a serial log that scrolls that fast hides
    // everything either side of it.
    static int lastLogged = -1;
    const int pct = otaPercent();
    if (pct / 10 != lastLogged) {
      lastLogged = pct / 10;
      LOGF("[ota] %d%% (%u / %u bytes)\n", pct, (unsigned)g_written, (unsigned)g_total);
    }
  } else if (millis() - g_lastByteMs > OTA_STALL_MS) {
    // A connection that has gone away without saying so. The socket may sit
    // there politely reporting itself connected for a great deal longer than
    // a person is willing to watch a progress bar stand still.
    LOGF("[ota] no data for %u s — giving up at %u / %u bytes\n", (unsigned)(OTA_STALL_MS / 1000),
         (unsigned)g_written, (unsigned)g_total);
    otaFail(OTA_ERR_OFFLINE);
    return false;
  } else if (!stream->connected() && g_written < g_total) {
    LOGF("[ota] connection closed early at %u / %u bytes\n", (unsigned)g_written,
         (unsigned)g_total);
    otaFail(OTA_ERR_OFFLINE);
    return false;
  }

  if (g_written < g_total) return true;

  // All there. esp_ota_end() is the real check: it re-reads what was written
  // and runs esp_image_verify() over it, so a body that arrived corrupted, was
  // truncated by a proxy, or was never an app image at all is caught HERE,
  // before anything is pointed at it.
  g_state = OtaState::Verifying;
  LOGF("[ota] download complete, verifying the image\n");
  esp_err_t err = esp_ota_end(g_handle);
  g_handleOpen = false;
  if (err != ESP_OK) {
    LOGF("[ota] esp_ota_end rejected the image: %s\n", esp_err_to_name(err));
    otaFail(err == ESP_ERR_OTA_VALIDATE_FAILED ? OTA_ERR_BADIMAGE : OTA_ERR_FLASH);
    return false;
  }
  err = esp_ota_set_boot_partition(g_target);
  if (err != ESP_OK) {
    LOGF("[ota] esp_ota_set_boot_partition failed: %s\n", esp_err_to_name(err));
    otaFail(OTA_ERR_FLASH);
    return false;
  }

  otaRelease();
  g_state = OtaState::Ok;
  g_okSinceMs = millis();
  LOGF("[ota] %s installed into %s — rebooting into it\n", g_tag, g_target->label);
  // Informational-critical, in the same register as beepHoldConfirmed: the
  // household is standing there watching a progress bar, and the next thing
  // that happens is the device going dark for a reboot and then a thirty
  // second refresh. A rising pair says "that worked" through the silence.
  // Never muted — see the taxonomy at the top of yat_beep.cpp — because the
  // reboot is otherwise indistinguishable from the device having died.
  beepUpdateDone();
  return false;
}

void otaPump() {
  switch (g_state) {
    case OtaState::Idle:
    case OtaState::Error:
      return;

    case OtaState::Drawing:
      // The portal owns the panel: it has to drop the AP before a refresh
      // (power rule) and put it back afterwards, so it draws the card and
      // calls otaCardDrawn(). Nothing happens here until it has.
      return;

    case OtaState::Downloading:
      if (!g_http) {
        if (!openAssetStream()) return;
        if (!openFlashSlot()) return;
      }
      pumpBody();
      return;

    case OtaState::Verifying:
      return;  // pumpBody() does not return while verifying

    case OtaState::Ok:
      // Reboot once the phone has collected the "ok" — otherwise the page it
      // is polling just stops answering, and "the device went silent" is
      // exactly the wrong last impression for a successful update. The linger
      // is a ceiling, not a wait: one poll ends it.
      if (g_okSeen || millis() - g_okSinceMs > OTA_OK_LINGER_MS) {
        LOGF("[ota] restarting into the new firmware\n");
        delay(50);  // let the socket flush the answer the phone is reading
        ESP.restart();
      }
      return;
  }
}

// ---------------- the panel ----------------
//
// Same chrome and the same prov:: helpers as the firmware's other cards
// (yat_screens.cpp) so the device still looks like itself while it does the
// single most alarming thing it ever does. Every codepoint here was checked
// against the generated 16px/24px subset tables in engine/src/fonts_data_*.cpp
// before it was written: a glyph the face does not carry draws as NOTHING, and
// that is how the safe-mode card once shipped with a word missing from it.
//
// This card is deliberately NOT registered in ScreenCard/NVS the way the
// low-battery and safe-mode cards are. Those persist because their condition
// outlives a sleep; this one is replaced within a minute either way — by the
// new firmware's first render on success, or by the portal's own exit render
// (portalRenderContent, which runs on both doors out of setup) on failure.
void otaDrawCard() {
  LOGF("[epd] init (updating card)...\n");
  epaper.begin();
  g_panelReady = true;

  EPaperCanvas canvas;
  yat::DefaultFontProvider font;
  const int W = SCREEN_W, H = SCREEN_H;
  canvas.fillRect(0, 0, W, H, yat::Ink::White);

  prov::drawText(canvas, font, 16, 7, "更新緊韌體   Updating firmware", 24, yat::Ink::Black, true);
  canvas.fillRect(0, 43, W, 1, yat::Ink::Black);

  const int iconSize = 96;
  prov::drawIcon(canvas, "info", (W - iconSize) / 2, 84, iconSize, yat::Ink::Black);

  auto centerLine = [&](int y, const char* zh, const char* en) {
    prov::drawText(canvas, font, (W - prov::textWidth(zh, 16)) / 2, y, zh, 16, yat::Ink::Black);
    prov::drawText(canvas, font, (W - prov::textWidth(en, 16)) / 2, y + 18, en, 16,
                   yat::Ink::Black);
  };
  centerLine(210, "部機而家喺度下載新版本，跟住會自己重開。",
             "The device is downloading the new version and will restart itself.");
  centerLine(256, "唔好熄電源，唔好扯咗個插頭。", "Leave it powered on — do not unplug it.");
  centerLine(302, "大約要幾分鐘。個芒會停喺呢一版，唔郁係正常嘅。",
             "This takes a few minutes. The screen stays on this page — that is normal.");

  drawStandardFooter(canvas, font);

  logStackHeadroom("the update card");
  panelRefresh("");
}

// ---------------- boot validation ----------------
//
// ROLLBACK IS WIRED, and this function is the half of it that lives in the
// application. The prebuilt pioarduino 55.03.39 sdkconfig for esp32s3 sets
// both CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y and CONFIG_APP_ROLLBACK_ENABLE
// =y (framework-arduinoespressif32-libs/esp32s3/sdkconfig), so no custom
// sdkconfig was needed. What that buys:
//
//   esp_ota_set_boot_partition() marks the new slot ESP_OTA_IMG_NEW; the
//   bootloader flips it to ESP_OTA_IMG_PENDING_VERIFY on its first boot; and
//   if the device resets AGAIN while it is still PENDING_VERIFY, the
//   bootloader marks that image ABORTED and boots the previous slot instead.
//
// So the question is only where the app says "I am fine". That is HERE, from
// lowLevelSleep() — the one funnel every cycle exits through — rather than at
// the top of setup(). The difference matters on this device more than on most:
// the panel is the entire product, and an image that boots but faults halfway
// through its first render would confirm itself under a boot-time mark and
// then own the device. Confirming at the end of the cycle means the evidence
// is "this image got all the way from cold boot to sleep", and anything short
// of that — a panic, a hang the task watchdog kills, a brownout mid-render —
// leaves it unconfirmed and the next reset takes the household back to the
// firmware they had.
//
// It also has to be within the FIRST cycle, which is why it cannot wait for
// something more ambitious: every wake from deep sleep is a reset, so a mark
// deferred past this sleep would be a rollback on the next alarm.
//
// What this deliberately does NOT do is treat a failed FETCH as a failed
// image. A household whose router is off gets a cycle that renders nothing and
// still reaches this sleep, and reverting a perfectly good firmware over that
// would be its own bug. Crash-shaped failures are what rollback is for; the
// crash-loop breaker (panic_n -> safe mode) remains the second net under it,
// for the case where the OLD firmware is the one that cannot cope.
void otaMarkAppValidIfPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(running, &st) != ESP_OK) return;
  if (st != ESP_OTA_IMG_PENDING_VERIFY) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    LOGF("[ota] first cycle on %s completed — image confirmed, rollback cancelled\n",
         running->label);
  } else {
    LOGF("[ota] could not confirm %s — the bootloader will roll back on the next reset\n",
         running->label);
  }
}
