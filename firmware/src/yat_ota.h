#pragma once
// Firmware update over the household's own WiFi (ROADMAP: "no computer after
// the first flash"). New in v0.4; unlike its neighbours this file is not a
// slice of the old single-TU main.cpp.

#include <stdint.h>

// Where a download can be. The portal serves these as the "state" word of
// GET /api/update, and they are a sealed cross-lane contract in the same way
// portalSendError's kinds are: the website's device-portal module carries copy for each one
// by name, so a seventh state cannot appear here without telling the site lane.
enum class OtaState : uint8_t {
  Idle,         // nothing has been asked for this session
  Drawing,      // the panel is being given the "updating" card, before a byte is fetched
  Downloading,  // streaming the image into the inactive slot; otaPercent() is meaningful
  Verifying,    // esp_ota_end() is checking what landed
  Ok,           // the image is good and marked bootable; the reboot is moments away
  Error,        // gave up; otaDetail() says which way
};

// True iff `tag` is exactly ^v[0-9]+\.[0-9]+\.[0-9]+$ — see otaArm().
bool otaTagValid(const char* tag);

// Accept a request to install release `tag`. False when the tag is malformed,
// something is already running, or there is no route to the internet;
// otaDetail() then names which. Nothing is fetched here: the work happens in
// otaPump(), from the portal's own loop.
bool otaArm(const char* tag);

// One bounded slice of the update, called every turn of the portal loop.
// Returns quickly (well under the ~100 ms a phone would notice) so the portal
// keeps answering GET /api/update while the download runs.
void otaPump();

// True from otaArm() until the device reboots or gives up — i.e. whenever the
// portal loop must keep calling otaPump() and must not go to sleep.
bool otaBusy();

OtaState otaState();
int otaPercent();        // 0-100, meaningful while Downloading
const char* otaTag();    // the validated tag, or "" before one is accepted
const char* otaDetail(); // an OTA_ERR_* token when Error, "" otherwise

// "owner/repo" this build downloads updates from — the compile-time constant
// with its path furniture stripped, so /api/status can tell the portal which
// repo's releases to check (rehearsal builds point somewhere temporary).
const char* otaRepoSlug();

// True once the panel should be given the update card and the download may
// start — the portal owns the AP/refresh dance (power rule), so it asks.
bool otaWantsCard();
void otaDrawCard();      // the firmware-owned bilingual "updating" card
void otaCardDrawn();     // the portal reporting the refresh is finished

// Called from the portal's GET /api/update handler so the reboot can wait
// until the phone has actually seen the "ok" state once.
void otaOkWasSeen();

// The other half of boot validation, called from lowLevelSleep(): a cycle that
// reached its sleep on a freshly-installed image is the evidence that image
// works. See its definition for what happens when it is never reached.
void otaMarkAppValidIfPending();

// The `detail` words GET /api/update can carry, and the same sealed contract as
// the states above: the website's device-portal module has a bilingual sentence per token
// and generic copy for anything else.
extern const char* const OTA_ERR_BADTAG;   // the tag was not vX.Y.Z
extern const char* const OTA_ERR_BUSY;     // an update is already running
extern const char* const OTA_ERR_OFFLINE;  // no LAN/internet, or the connection died
extern const char* const OTA_ERR_NOTFOUND; // no such release, or no asset for this model
extern const char* const OTA_ERR_HTTP;     // some other status from GitHub
extern const char* const OTA_ERR_REDIRECT; // the 302 was missing or pointed off GitHub
extern const char* const OTA_ERR_TOOBIG;   // the image does not fit the app slot
extern const char* const OTA_ERR_BADIMAGE; // it downloaded, and it is not a bootable image
extern const char* const OTA_ERR_FLASH;    // a write to the inactive slot failed
