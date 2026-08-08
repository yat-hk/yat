#!/usr/bin/env python3
"""yat.py — host-side serial client for the YAT device's line protocol.

Talks to a YAT (reTerminal E1002) device over its USB serial console: file
transfer (LS/GET/PUT/RM), status/reboot, and the page/voice commands
(PAGES/USE/SAY/SECRET) — see README.md in this directory for the full
protocol writeup and a worked "weekend test" example.

Requires pyserial:  pip install pyserial
(already present in the PlatformIO venv: ~/.local/pipx/venvs/platformio/bin/python)
"""
from __future__ import annotations

import argparse
import glob
import json
import sys
import time
from typing import Any, Iterable, NamedTuple, Optional, Protocol, Tuple


# --------------------------------------------------------------------------
# Errors
# --------------------------------------------------------------------------

class YatError(Exception):
    """A clean, user-facing protocol/usage error (ERR response, bad args, ...)."""


class YatTimeout(YatError):
    """Raised when a response didn't arrive within the allotted time."""


# --------------------------------------------------------------------------
# Transport
# --------------------------------------------------------------------------
#
# Anything that looks like this (read/write/timeout) works as `link` below —
# a real serial.Serial, or the FakeDevice mock in test_yat.py. This is the
# seam that lets the protocol layer be tested with no real serial port.

class Transport(Protocol):
    timeout: float

    def read(self, size: int = 1) -> bytes: ...
    def write(self, data: bytes) -> int: ...


def _send_line(link: Transport, text: str) -> None:
    link.write(text.encode("utf-8") + b"\n")
    flush = getattr(link, "flush", None)
    if flush:
        flush()


def _read_line(link: Transport, overall_timeout: float) -> str:
    """Read one '\\n'-terminated line, skipping firmware debug log lines.

    The firmware's LOGF() macro mirrors every debug message to the *same*
    Serial stream the YAT protocol runs on (see firmware/src/main.cpp — LOGF
    writes to both Serial and Serial1), so lines like
    "[console] window open 10000 ms" can appear interleaved with protocol
    responses at any time. No real protocol line ever starts with '[', so
    lines with that prefix are treated as noise and skipped rather than
    causing a parse error.
    """
    deadline = time.monotonic() + overall_timeout
    while True:
        buf = bytearray()
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise YatTimeout("timed out waiting for a response line")
            try:
                link.timeout = max(0.05, min(0.5, remaining))
            except AttributeError:
                pass
            chunk = link.read(1)
            if not chunk:
                continue
            if chunk == b"\n":
                break
            if chunk != b"\r":
                buf.extend(chunk)
        line = buf.decode("utf-8", errors="replace")
        if line.startswith("["):
            continue  # firmware debug log line, not a protocol response
        return line


def _read_exact(link: Transport, n: int, overall_timeout: float) -> bytes:
    deadline = time.monotonic() + overall_timeout
    buf = bytearray()
    while len(buf) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise YatTimeout(f"timed out reading payload ({len(buf)}/{n} bytes)")
        try:
            link.timeout = max(0.05, min(1.0, remaining))
        except AttributeError:
            pass
        chunk = link.read(n - len(buf))
        if chunk:
            buf.extend(chunk)
    return bytes(buf)


def _err_message(line: str) -> str:
    return line[4:] if line.startswith("ERR ") else line


# --------------------------------------------------------------------------
# Protocol commands (each takes an already-open `link`)
# --------------------------------------------------------------------------

def wake(link: Transport, settle_s: float = 0.2) -> None:
    """Poke the console window: any byte during an open window extends it.

    A bare newline decodes to an empty command line, which the firmware
    silently ignores (handleConsoleLine returns early on an empty line) —
    so this never produces a response, it just proves there's a byte in
    the buffer for the firmware's console-window logic to notice.
    """
    link.write(b"\n")
    time.sleep(settle_s)


def cmd_ls(link: Transport, cto: float) -> list[Tuple[str, int]]:
    _send_line(link, "YAT LS")
    entries: list[Tuple[str, int]] = []
    while True:
        line = _read_line(link, cto)
        if line.startswith("ERR"):
            raise YatError(_err_message(line))
        if line.startswith("OK "):
            break  # "OK <n> files" terminator
        path, sep, size = line.rpartition(" ")
        if not sep:
            raise YatError(f"unexpected LS line: {line!r}")
        try:
            entries.append((path, int(size)))
        except ValueError:
            raise YatError(f"unexpected LS line: {line!r}")
    return entries


def cmd_get(link: Transport, path: str, cto: float, pto: float) -> bytes:
    _send_line(link, f"YAT GET {path}")
    line = _read_line(link, cto)
    if line.startswith("ERR"):
        raise YatError(_err_message(line))
    if not line.startswith("OK "):
        raise YatError(f"unexpected response: {line!r}")
    try:
        size = int(line[3:].strip())
    except ValueError:
        raise YatError(f"unexpected response: {line!r}")
    return _read_exact(link, size, pto)


def cmd_put(link: Transport, path: str, data: bytes, cto: float, pto: float) -> int:
    if len(data) <= 0 or len(data) > 65536:
        raise YatError(f"bad length: {len(data)} bytes (must be 1..65536)")
    _send_line(link, f"YAT PUT {path} {len(data)}")
    link.write(data)
    flush = getattr(link, "flush", None)
    if flush:
        flush()
    line = _read_line(link, pto)
    if line.startswith("ERR"):
        raise YatError(_err_message(line))
    if not line.startswith("OK "):
        raise YatError(f"unexpected response: {line!r}")
    parts = line.split()
    try:
        return int(parts[1])
    except (IndexError, ValueError):
        raise YatError(f"unexpected response: {line!r}")


def cmd_rm(link: Transport, path: str, cto: float) -> None:
    _send_line(link, f"YAT RM {path}")
    line = _read_line(link, cto)
    if line.startswith("OK"):
        return
    raise YatError(_err_message(line))


def cmd_status(link: Transport, cto: float) -> dict:
    _send_line(link, "YAT STATUS")
    line = _read_line(link, cto)
    if line.startswith("ERR"):
        raise YatError(_err_message(line))
    try:
        obj = json.loads(line)
    except json.JSONDecodeError as e:
        raise YatError(f"bad STATUS json ({e}): {line!r}")
    if not isinstance(obj, dict):
        raise YatError(f"unexpected STATUS json: {line!r}")
    return obj


def cmd_reboot(link: Transport, cto: float) -> None:
    _send_line(link, "YAT REBOOT")
    line = _read_line(link, cto)
    if not line.startswith("OK"):
        raise YatError(_err_message(line))


def cmd_pages(link: Transport, cto: float) -> list[dict]:
    _send_line(link, "YAT PAGES")
    out: list[dict] = []
    while True:
        line = _read_line(link, cto)
        if line.strip() == "OK":
            break
        if line.startswith("ERR"):
            raise YatError(_err_message(line))
        active = line.startswith("*")
        rest = line[1:].lstrip() if active else line
        parts = rest.split()
        if len(parts) < 3:
            raise YatError(f"unexpected PAGES line: {line!r}")
        idx_s, pid, pack = parts[0], parts[1], parts[2]
        try:
            idx = int(idx_s)
        except ValueError:
            raise YatError(f"unexpected PAGES line: {line!r}")
        out.append({"idx": idx, "id": pid, "pack": pack, "active": active})
    return out


def cmd_use(link: Transport, id_or_index: str, cto: float) -> Tuple[int, str]:
    _send_line(link, f"YAT USE {id_or_index}")
    line = _read_line(link, cto)
    if line.startswith("OK "):
        parts = line.split()
        if len(parts) >= 3:
            try:
                return int(parts[1]), parts[2]
            except ValueError:
                pass
        raise YatError(f"unexpected response: {line!r}")
    raise YatError(_err_message(line))


class SayResult(NamedTuple):
    """What one `YAT SAY` did on the device.

    `kind` is "page" for the page switch voice has always done, or one of
    "add"/"done"/"clear" for a todo-list intent (firmware yat_voice.cpp, voice
    intents section). A refusal of any sort is NOMATCH on the wire and None
    here — the device makes the same "didn't catch that" sound for all of them,
    so there is nothing finer to report.
    """
    kind: str
    page_idx: int = -1     # "page"
    page_id: str = ""      # "page"
    item_idx: int = -1     # "done": which item in the list was marked
    removed: int = 0       # "clear": how many done items were dropped
    items: int = 0         # list length after the change
    open_items: int = 0    # items still open after the change


def cmd_say(link: Transport, text: str, cto: float) -> Optional[SayResult]:
    _send_line(link, f"YAT SAY {text}")
    line = _read_line(link, cto)
    if line.strip() == "NOMATCH":
        return None
    parts = line.split()
    try:
        if line.startswith("MATCH ") and len(parts) >= 3:
            return SayResult("page", page_idx=int(parts[1]), page_id=parts[2])
        if line.startswith("TODO ADD ") and len(parts) >= 4:
            return SayResult("add", items=int(parts[2]), open_items=int(parts[3]))
        if line.startswith("TODO DONE ") and len(parts) >= 5:
            return SayResult("done", item_idx=int(parts[2]), items=int(parts[3]),
                             open_items=int(parts[4]))
        if line.startswith("TODO CLEAR ") and len(parts) >= 5:
            return SayResult("clear", removed=int(parts[2]), items=int(parts[3]),
                             open_items=int(parts[4]))
    except ValueError:
        raise YatError(f"unexpected response: {line!r}")
    if line.startswith("MATCH ") or line.startswith("TODO "):
        raise YatError(f"unexpected response: {line!r}")
    raise YatError(_err_message(line))


def cmd_secret(link: Transport, name: str, value: str, cto: float) -> None:
    _send_line(link, f"YAT SECRET {name} {value}")
    line = _read_line(link, cto)
    if line.startswith("OK"):
        return
    raise YatError(_err_message(line))


# --------------------------------------------------------------------------
# Higher-level flows: install / set-config
# --------------------------------------------------------------------------

def do_install(
    link: Transport,
    packfile_path: str,
    page_id_override: Optional[str],
    params_json_str: Optional[str],
    aliases_str: Optional[str],
    cto: float,
    pto: float,
) -> Tuple[str, str, list[dict]]:
    """PUT a pack file, then insert-or-replace its page entry in config.json.

    Returns (page_id, packid, resulting YAT PAGES listing).
    """
    with open(packfile_path, "rb") as f:
        raw = f.read()
    try:
        pack_obj = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise YatError(f"{packfile_path} is not valid utf-8 JSON: {e}")
    packid = pack_obj.get("id") if isinstance(pack_obj, dict) else None
    if not isinstance(packid, str) or not packid:
        raise YatError(f'{packfile_path} has no string "id" field')

    remote_pack_path = f"/packs/{packid}.yat-pack.json"
    written = cmd_put(link, remote_pack_path, raw, cto, pto)
    if written != len(raw):
        raise YatError(f"pack upload short: wrote {written}/{len(raw)} bytes")

    page_id = page_id_override or packid

    try:
        cfg_raw = cmd_get(link, "/config.json", cto, pto)
        cfg = json.loads(cfg_raw.decode("utf-8"))
        if not isinstance(cfg, dict):
            raise YatError("/config.json did not deserialize to a JSON object")
    except YatError as e:
        if "not found" in str(e).lower():
            cfg = {"v": 1, "pages": []}
        else:
            raise

    pages = cfg.setdefault("pages", [])
    if not isinstance(pages, list):
        raise YatError('/config.json "pages" is not a list')

    entry = None
    for p in pages:
        if isinstance(p, dict) and p.get("id") == page_id:
            entry = p
            break
    if entry is None:
        entry = {"id": page_id}
        pages.append(entry)
    entry["pack"] = packid

    if params_json_str is not None:
        try:
            entry["params"] = json.loads(params_json_str)
        except json.JSONDecodeError as e:
            raise YatError(f"--params-json is not valid JSON: {e}")

    if aliases_str is not None:
        entry["aliases"] = [a.strip() for a in aliases_str.split(",") if a.strip()]

    new_cfg_bytes = json.dumps(cfg, ensure_ascii=False, indent=2).encode("utf-8")
    written = cmd_put(link, "/config.json", new_cfg_bytes, cto, pto)
    if written != len(new_cfg_bytes):
        raise YatError(f"config upload short: wrote {written}/{len(new_cfg_bytes)} bytes")

    return page_id, packid, cmd_pages(link, cto)


def do_set_config(link: Transport, localfile: str, cto: float, pto: float) -> int:
    with open(localfile, "rb") as f:
        raw = f.read()
    try:
        json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise YatError(f"{localfile} is not valid utf-8 JSON: {e}")
    written = cmd_put(link, "/config.json", raw, cto, pto)
    if written != len(raw):
        raise YatError(f"config upload short: wrote {written}/{len(raw)} bytes")
    return written


# --------------------------------------------------------------------------
# Port autodetection
# --------------------------------------------------------------------------

def autodetect_port() -> str:
    for pattern in ("/dev/cu.usbserial*", "/dev/ttyUSB*"):
        matches = sorted(glob.glob(pattern))
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise YatError(
                f"multiple candidate serial ports match {pattern}: "
                f"{', '.join(matches)} — pass --port explicitly"
            )
    raise YatError(
        "no serial port found (looked for /dev/cu.usbserial* and /dev/ttyUSB*) "
        "— pass --port explicitly"
    )


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _fmt_pages(pages: Iterable[dict]) -> str:
    lines = []
    for p in pages:
        mark = "*" if p["active"] else " "
        lines.append(f"{mark} {p['idx']:>2}  {p['id']:<24} {p['pack']}")
    return "\n".join(lines) if lines else "(no pages)"


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="yat.py",
        description="Host-side serial client for the YAT device line protocol.",
    )
    p.add_argument("--port", default=None,
                    help="serial device (default: autodetect /dev/cu.usbserial* then /dev/ttyUSB*)")
    p.add_argument("--baud", type=int, default=115200, help="baud rate (default: 115200)")
    p.add_argument("--timeout", type=float, default=5.0,
                    help="control-line read timeout in seconds (default: 5.0)")
    p.add_argument("--payload-timeout", type=float, default=20.0,
                    help="bulk transfer (GET/PUT payload) timeout in seconds (default: 20.0)")
    p.add_argument("--boot-wait", type=float, default=0.3,
                    help="seconds to pause after opening the port before the wake poke "
                         "(bump this if your USB-serial adapter resets the board on open)")

    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("ls", help="list files on the device")

    sp = sub.add_parser("get", help="download a file from the device")
    sp.add_argument("path")
    sp.add_argument("-o", "--output", default=None, help="local file to write (default: stdout)")

    sp = sub.add_parser("put", help="upload a file to the device")
    sp.add_argument("path")
    sp.add_argument("localfile")

    sp = sub.add_parser("rm", help="remove a file on the device")
    sp.add_argument("path")

    sub.add_parser("status", help="print device status (JSON)")
    sub.add_parser("reboot", help="reboot the device")
    sub.add_parser("pages", help="list configured pages")

    sp = sub.add_parser("use", help="switch the active page")
    sp.add_argument("id_or_index")

    sp = sub.add_parser("say", help="test a voice phrase match")
    sp.add_argument("text", nargs="+")

    sp = sub.add_parser("secret", help="set (or clear with '-') a named secret")
    sp.add_argument("name")
    sp.add_argument("value")

    sp = sub.add_parser(
        "install",
        help="PUT a pack file and insert-or-replace its page entry in config.json",
    )
    sp.add_argument("packfile", help="local *.yat-pack.json file")
    sp.add_argument("--id", default=None, help="page id (default: the pack's own \"id\")")
    sp.add_argument("--params-json", default=None, help="page params, as a JSON object string")
    sp.add_argument("--aliases", default=None,
                    help="comma-separated extra voice aliases for this page, e.g. 'a,b,c'")

    sp = sub.add_parser("set-config", help="validate and PUT a local file as /config.json")
    sp.add_argument("localfile")

    return p


def _connect(args: argparse.Namespace):
    import serial  # imported lazily so protocol/test code has no hard dependency

    port = args.port or autodetect_port()
    link = serial.Serial(port=port, baudrate=args.baud, timeout=0.2)
    time.sleep(args.boot_wait)
    return link


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    cto = args.timeout
    pto = args.payload_timeout

    try:
        link = _connect(args)
    except YatError as e:
        print(f"yat: error: {e}", file=sys.stderr)
        return 1
    except Exception as e:  # pyserial raises its own SerialException etc.
        print(f"yat: could not open serial port: {e}", file=sys.stderr)
        return 1

    try:
        wake(link)

        if args.command == "ls":
            for path, size in cmd_ls(link, cto):
                print(f"{size:>8}  {path}")

        elif args.command == "get":
            data = cmd_get(link, args.path, cto, pto)
            if args.output:
                with open(args.output, "wb") as f:
                    f.write(data)
                print(f"wrote {len(data)} bytes to {args.output}", file=sys.stderr)
            else:
                sys.stdout.buffer.write(data)

        elif args.command == "put":
            with open(args.localfile, "rb") as f:
                data = f.read()
            written = cmd_put(link, args.path, data, cto, pto)
            print(f"OK {written} bytes written to {args.path}")

        elif args.command == "rm":
            cmd_rm(link, args.path, cto)
            print(f"OK removed {args.path}")

        elif args.command == "status":
            print(json.dumps(cmd_status(link, cto), indent=2, ensure_ascii=False))

        elif args.command == "reboot":
            cmd_reboot(link, cto)
            print("OK rebooting")

        elif args.command == "pages":
            print(_fmt_pages(cmd_pages(link, cto)))

        elif args.command == "use":
            idx, pid = cmd_use(link, args.id_or_index, cto)
            print(f"OK {idx} {pid}")

        elif args.command == "say":
            result = cmd_say(link, " ".join(args.text), cto)
            if result is None:
                print("NOMATCH")
                return 1
            if result.kind == "page":
                print(f"MATCH {result.page_idx} {result.page_id}")
            elif result.kind == "add":
                print(f"TODO ADD — {result.items} item(s), {result.open_items} open")
            elif result.kind == "done":
                print(f"TODO DONE item {result.item_idx} — {result.open_items} open left")
            else:
                print(f"TODO CLEAR — {result.removed} removed, {result.items} left")

        elif args.command == "secret":
            cmd_secret(link, args.name, args.value, cto)
            print("OK")

        elif args.command == "install":
            page_id, packid, pages = do_install(
                link, args.packfile, args.id, args.params_json, args.aliases, cto, pto
            )
            print(f"OK installed {packid} as page \"{page_id}\"")
            print(_fmt_pages(pages))

        elif args.command == "set-config":
            written = do_set_config(link, args.localfile, cto, pto)
            print(f"OK {written} bytes written to /config.json")

        else:  # pragma: no cover - argparse guards this
            parser.error(f"unknown command {args.command!r}")

        return 0

    except YatError as e:
        print(f"yat: error: {e}", file=sys.stderr)
        return 1
    except FileNotFoundError as e:
        print(f"yat: error: {e}", file=sys.stderr)
        return 1
    finally:
        close = getattr(link, "close", None)
        if close:
            close()


if __name__ == "__main__":
    sys.exit(main())
