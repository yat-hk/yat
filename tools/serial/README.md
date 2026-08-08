# yat.py — YAT serial client

A host-side CLI for talking to a YAT device (reTerminal E1002) over its USB
serial console. It speaks the `YAT <CMD> ...` line protocol implemented in
`firmware/src/main.cpp`'s console section: file transfer, status/reboot, and
the page/voice commands (`PAGES`/`USE`/`SAY`/`SECRET`).

## Install

```sh
pip install pyserial
```

The PlatformIO venv already has it — you can run the tool with that
interpreter directly if you'd rather not install anything system-wide:

```sh
~/.local/pipx/venvs/platformio/bin/python yat.py status
```

## Usage

```sh
python3 yat.py [--port PORT] [--baud 115200] <command> [args...]
```

`--port` defaults to autodetecting a single `/dev/cu.usbserial*` device (or,
if none, a single `/dev/ttyUSB*` device). If more than one candidate matches,
or none do, the tool errors out and lists what it saw — pass `--port`
explicitly in that case.

Commands:

| Command | Description |
|---|---|
| `ls` | List files on the device (`/config.json`, `/packs/*.yat-pack.json`) |
| `get <path> [-o file]` | Download a file; writes to stdout if `-o` is omitted |
| `put <path> <localfile>` | Upload a local file to a device path |
| `rm <path>` | Remove a file on the device |
| `status` | Print the device's `YAT STATUS` JSON (pretty-printed as-is) |
| `reboot` | Reboot the device |
| `pages` | List configured pages (`*` marks the active one) |
| `use <id-or-index>` | Switch the active page |
| `say <text...>` | Test a voice phrase match against configured pages |
| `secret <name> <value>` | Set a named secret (`value` of `-` clears it) |
| `install <packfile.json> [--id X] [--params-json '{}'] [--aliases 'a,b,c']` | Upload a pack and add/update its page entry in `config.json` |
| `set-config <localfile>` | Validate a local file as JSON and PUT it as `/config.json` |

Only `/config.json` and `/packs/<id>.yat-pack.json` are writable — the
firmware enforces this path whitelist itself (`pathAllowed()` in
`firmware/src/main.cpp`), so `get`/`put`/`rm` against anything else comes
back as `ERR path not allowed`.

## Weekend test flow

A worked example using packs from a sibling `yat-packs` checkout (clone
`github.com/yat-hk/yat-packs` next to this repo) and a device whose `hko-now`
page has been given the id `weather` (so `use weather` / `say 天氣` line up):

```sh
# Install the weather pack under the page id "weather"
python3 yat.py install ../../../yat-packs/official/hko-now.yat-pack.json --id weather

# Install the Sushiro queue pack (page id defaults to the pack's own "id")
python3 yat.py install ../../../yat-packs/official/sushiro-queue.yat-pack.json

# Switch to it, then back to weather
python3 yat.py use sushiro-queue
python3 yat.py use weather

# Voice-match test — hko-now's built-in zh-Hant alias for weather is 天氣
python3 yat.py say 天氣
# -> MATCH 0 weather

# Set the STT key for voice (ElevenLabs or similar); "-" clears it
python3 yat.py secret stt sk-your-real-key-here

python3 yat.py status
python3 yat.py pages
```

## Notes / gotchas discovered from the firmware

- **Framing.** `YAT GET <path>` responds `OK <size>` followed by exactly
  `<size>` raw bytes (no terminator). `YAT PUT <path> <size>` must be
  immediately followed by exactly `<size>` raw bytes — the firmware calls
  `Serial.readBytes()` for that count right after parsing the command line,
  so nothing else may be interleaved. Pack files are capped at 65536 bytes
  (`cmdPut`'s `PACK-SPEC §12.1` check); `yat.py` also rejects oversize PUTs
  client-side before writing anything.
- **Every PUT is JSON-validated on the device** (`deserializeJson` structural
  check) before it's written — both `config.json` and pack files are JSON,
  so a malformed upload comes back `ERR invalid json, not written` and never
  touches the filesystem.
- **Debug log lines share the response stream.** The firmware's `LOGF()`
  macro mirrors every debug message to *both* `Serial` and `Serial1` — so
  lines like `[console] window open 10000 ms` can appear interleaved with
  protocol responses on the same USB serial stream at any time. No real
  protocol response ever starts with `[`, so `yat.py` skips any line with
  that prefix rather than treating it as a parse error. If you're debugging
  and see a response that looks "off by one line," this is why — it's
  filtering log noise, not eating a real reply.
- **`YAT USE <id-or-index>`** accepts either a page id (`weather`) or a
  0-based numeric index (`1`); `yat.py use` passes whatever you type through
  as-is and lets the device resolve it, returning `ERR no such page` for
  either kind of miss.
- **Console window / wake poke.** The console window opens for ~10 s after
  wake (timer, button, or serial activity) and any byte received *extends*
  it, so a slow human typing a command doesn't get cut off mid-session. If
  the device isn't already in that window, a stray byte does nothing (it's
  not a real hardware wake source — only the RTC timer and KEY0/1/2 wake the
  device from deep sleep). `yat.py` sends a bare newline and pauses
  (`--boot-wait`, default 0.3 s) right after opening the port, before its
  first real command, purely to catch/extend an already-open window; it
  can't wake a sleeping device by itself.
- **USB-serial auto-reset risk.** Some USB-serial adapters tie DTR/RTS to the
  board's reset/boot pins and reset the MCU whenever a host opens the port
  (common on classic ESP32 dev boards; unconfirmed for this specific
  reTerminal E1002 USB bridge). If you see the device reboot every time you
  run `yat.py`, that's almost certainly it — bump `--boot-wait` to give the
  reboot + `Serial.begin()` warm-up (up to ~2 s per `setup()`) time to finish
  before the first command is sent.
- **`install`'s config.json schema is best-effort.** The exact shape of the
  page list inside `config.json` is being finalized by the firmware config
  work landing in parallel with this tool. `yat.py install` assumes a
  top-level `"pages"` array of `{"id", "pack", "params"?, "aliases"?}`
  objects (matching PRD.md's `pages[] (pack + params + voice-alias
  overrides)` description) and merges into it non-destructively — unknown
  top-level keys and unknown per-page fields are preserved untouched. If the
  landed firmware schema differs, only `do_install()` in `yat.py` should
  need adjusting; the wire framing (LS/GET/PUT/RM/PAGES/USE/SAY/SECRET) is
  unaffected either way.

## Testing (no hardware required)

```sh
python3 -m py_compile yat.py test_yat.py
python3 test_yat.py
```

`test_yat.py` implements `FakeDevice`, a small in-process mock that
re-creates the firmware's console behavior (including the `LOGF` debug-line
interleaving quirk above) closely enough to exercise every command's framing
without a real serial port — including a byte-for-byte CJK pack
PUT/GET round trip and a full `install` flow (pack upload → config.json
read-modify-write → `YAT PAGES` reflecting the new page). 27 tests, all
passing, runtime a few milliseconds (the mock never actually blocks on I/O).

The device itself should **not** be connected while running these — they
exercise `yat.py`'s parsing logic against the mock only, never a real port.
