#!/usr/bin/env python3
"""Unit-style tests for yat.py, against a mocked serial device (no hardware).

Run with:  python3 test_yat.py
(or the PlatformIO venv:  ~/.local/pipx/venvs/platformio/bin/python test_yat.py)

FakeDevice below re-implements just enough of firmware/src/main.cpp's console
protocol (LS/GET/PUT/RM/STATUS/REBOOT, plus the PAGES/USE/SAY/SECRET contract
given for this task) to exercise yat.py's framing without a real port. It
also injects a firmware-style "[tag] ..." debug log line before some
responses, mirroring LOGF() mirroring to the same Serial stream — this is
the quirk that motivated _read_line()'s "skip lines starting with '[' "
behavior in yat.py.
"""
from __future__ import annotations

import json
import os
import re
import string
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import yat  # noqa: E402


# --------------------------------------------------------------------------
# The simplified -> traditional table, read from the ONE copy the firmware and
# the engine share (engine/third_party/s2t_table.h).
# --------------------------------------------------------------------------
#
# Not a hand-written stub: normalization is the whole reason 「买菜」 spoken into
# a device holding 「買菜」 works at all, and a mock with its own idea of which
# characters fold would prove nothing about the device. If the header moves or
# changes shape this raises at import rather than quietly testing ASCII only.

def _load_s2t() -> dict[int, int]:
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.normpath(os.path.join(here, "..", "..", "engine", "third_party", "s2t_table.h"))
    with open(path, encoding="utf-8") as f:
        src = f.read()
    frm = re.search(r"S2T_FROM\[\]\s*=\s*\{(.*?)\};", src, re.S)
    to = re.search(r"S2T_TO\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not frm or not to:
        raise RuntimeError(f"{path}: could not find S2T_FROM/S2T_TO")
    a = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+", frm.group(1))]
    b = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+", to.group(1))]
    if not a or len(a) != len(b):
        raise RuntimeError(f"{path}: S2T_FROM/S2T_TO are {len(a)}/{len(b)} entries")
    return dict(zip(a, b))


S2T = _load_s2t()

# firmware/src/yat_voice.cpp isCjkPunct()
CJK_PUNCT = "，。！？、：；「」『』（）"
ASCII_PUNCT = set(string.punctuation)


def normalize(s: str) -> tuple[str, list[int]]:
    """Port of voice::normalizeWithSourceMap (firmware/src/yat_voice.cpp).

    Lowercases ASCII, drops ASCII + CJK punctuation and all whitespace, maps
    simplified to traditional. Returns the normalized string plus, for each of
    its characters, the index in `s` of the character it came from — with a
    final len(s) entry, so norm[:k] always corresponds to s[:src[k]].
    """
    out: list[str] = []
    src: list[int] = []
    for i, ch in enumerate(s):
        code = ord(ch)
        if code < 0x80:
            if ch.isspace() or ch in ASCII_PUNCT:
                continue
            out.append(ch.lower())
        else:
            if ch in CJK_PUNCT:
                continue
            out.append(chr(S2T.get(code, code)))
        src.append(i)
    src.append(len(s))
    return "".join(out), src


# firmware/src/yat_voice.cpp kTodoVerbs — the same rows in the same order.
TODO_VERBS = [
    ("記住", "add"), ("加", "add"), ("add", "add"),
    ("搞掂", "done"), ("完成", "done"), ("done", "done"),
    ("清理", "clear"), ("清咗", "clear"), ("clear", "clear"),
]
# PACK-SPEC §3.2/§12.1 caps any array param at 20 entries, so the todo pack's
# schema says 20 and firmware/src/yat_config.h's TODO_MAX_ITEMS says 20 —
# voice has to refuse at the same number the settings page does.
TODO_MAX_ITEMS = 20
TODO_MAX_TEXT_CP = 48


class FakeDevice:
    """Duck-types serial.Serial closely enough for yat.py's Transport protocol."""

    def __init__(self):
        self.timeout = 0.2
        self._in = bytearray()   # bytes written by the client, not yet consumed
        self._out = bytearray()  # bytes queued for the client to read
        self._awaiting_put = None  # (path, nbytes) while mid-PUT payload

        self.files: dict[str, bytes] = {
            "/config.json": b'{"v": 1, "pages": []}',
        }
        self.secrets: dict[str, str] = {}
        self.fs_ready = True
        self.log_noise_next = False  # when True, prefix the next response with a "[..]" line

    # -- Transport protocol -------------------------------------------------

    def write(self, data: bytes) -> int:
        self._in.extend(data)
        self._pump()
        return len(data)

    def read(self, size: int = 1) -> bytes:
        n = min(size, len(self._out))
        if n == 0:
            return b""
        chunk = bytes(self._out[:n])
        del self._out[:n]
        return chunk

    def flush(self) -> None:
        pass

    def close(self) -> None:
        pass

    # -- internal device model ----------------------------------------------

    def _emit(self, line: str) -> None:
        if self.log_noise_next:
            self._out.extend(b"[console] activity during test - entering console window\n")
            self.log_noise_next = False
        self._out.extend(line.encode("utf-8") + b"\n")

    def _emit_raw(self, data: bytes) -> None:
        self._out.extend(data)

    def _pump(self) -> None:
        while True:
            if self._awaiting_put is not None:
                path, need = self._awaiting_put
                if len(self._in) < need:
                    return
                payload = bytes(self._in[:need])
                del self._in[:need]
                self._awaiting_put = None
                self._handle_put_payload(path, payload)
                continue
            nl = self._in.find(b"\n")
            if nl < 0:
                return
            raw_line = bytes(self._in[:nl])
            del self._in[: nl + 1]
            line = raw_line.decode("utf-8", errors="replace").rstrip("\r")
            self._handle_line(line)

    @staticmethod
    def _path_allowed(path: str) -> bool:
        if ".." in path:
            return False
        if path == "/config.json":
            return True
        if path.startswith("/packs/") and len(path) > 7:
            return True
        return False

    def _handle_line(self, line: str) -> None:
        line = line.strip()
        if not line:
            return  # empty line (e.g. the wake poke) -> silently ignored, like firmware
        if not line.startswith("YAT "):
            self._emit('ERR expected "YAT <CMD>"')
            return
        rest = line[4:].strip()
        sp = rest.find(" ")
        cmd = rest if sp < 0 else rest[:sp]
        args = "" if sp < 0 else rest[sp + 1 :].strip()

        if cmd == "LS":
            self._cmd_ls()
        elif cmd == "GET":
            self._cmd_get(args)
        elif cmd == "PUT":
            self._cmd_put_start(args)
        elif cmd == "RM":
            self._cmd_rm(args)
        elif cmd == "STATUS":
            self._cmd_status()
        elif cmd == "REBOOT":
            self._emit("OK rebooting")
        elif cmd == "PAGES":
            self._cmd_pages()
        elif cmd == "USE":
            self._cmd_use(args)
        elif cmd == "SAY":
            self._cmd_say(args)
        elif cmd == "SECRET":
            self._cmd_secret(args)
        else:
            self._emit("ERR unknown command")

    def _cmd_ls(self) -> None:
        if not self.fs_ready:
            self._emit("ERR filesystem not mounted")
            return
        count = 0
        for path, data in self.files.items():
            self._emit(f"{path} {len(data)}")
            count += 1
        self._emit(f"OK {count} files")

    def _cmd_get(self, path: str) -> None:
        path = path.strip()
        if not self._path_allowed(path):
            self._emit("ERR path not allowed")
            return
        if not self.fs_ready:
            self._emit("ERR filesystem not mounted")
            return
        if path not in self.files:
            self._emit("ERR not found")
            return
        data = self.files[path]
        self._emit(f"OK {len(data)}")
        self._emit_raw(data)

    def _cmd_put_start(self, args: str) -> None:
        sp = args.find(" ")
        if sp < 0:
            self._emit("ERR usage: PUT <path> <bytes>")
            return
        path = args[:sp]
        try:
            length = int(args[sp + 1 :])
        except ValueError:
            length = -1
        if length <= 0 or length > 65536:
            self._emit("ERR bad length")
            return
        if not self._path_allowed(path):
            self._emit("ERR path not allowed")
            return
        if not self.fs_ready:
            self._emit("ERR filesystem not mounted")
            return
        self._awaiting_put = (path, length)

    def _handle_put_payload(self, path: str, payload: bytes) -> None:
        try:
            json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._emit("ERR invalid json, not written")
            return
        self.files[path] = payload
        self._emit(f"OK {len(payload)} written")

    def _cmd_rm(self, path: str) -> None:
        path = path.strip()
        if not self._path_allowed(path):
            self._emit("ERR path not allowed")
            return
        if not self.fs_ready:
            self._emit("ERR filesystem not mounted")
            return
        if path not in self.files:
            self._emit("ERR not found")
            return
        del self.files[path]
        self._emit("OK removed")

    def _cmd_status(self) -> None:
        cfg = json.loads(self.files.get("/config.json", b"{}").decode("utf-8"))
        pages = cfg.get("pages", [])
        page_id = cfg.get("page_id")
        status = {
            "fw": "0.3.0-dev",
            "battery_mv": 4100,
            "last_hash": "0x1234abcd",
            "next_wake_s": 900,
            "pages": len(pages),
            "page_id": page_id,
            "voice_ready": bool(self.secrets.get("stt")),
        }
        self._emit(json.dumps(status))

    def _config_pages(self) -> list[dict]:
        cfg = json.loads(self.files.get("/config.json", b"{}").decode("utf-8"))
        return cfg.get("pages", []), cfg.get("page_id")

    def _cmd_pages(self) -> None:
        pages, active_id = self._config_pages()
        active_idx = 0
        for i, p in enumerate(pages):
            if p.get("id") == active_id:
                active_idx = i
                break
        for i, p in enumerate(pages):
            prefix = "*" if i == active_idx else ""
            self._emit(f"{prefix}{i} {p.get('id')} {p.get('pack')}")
        self._emit("OK")

    def _cmd_use(self, arg: str) -> None:
        pages, _ = self._config_pages()
        target = None
        if arg.isdigit():
            idx = int(arg)
            if 0 <= idx < len(pages):
                target = idx
        if target is None:
            for i, p in enumerate(pages):
                if p.get("id") == arg:
                    target = i
                    break
        if target is None:
            self._emit("ERR no such page")
            return
        cfg = json.loads(self.files["/config.json"].decode("utf-8"))
        cfg["page_id"] = pages[target]["id"]
        self.files["/config.json"] = json.dumps(cfg).encode("utf-8")
        self._emit(f"OK {target} {pages[target]['id']}")

    # -- voice: alias matching + todo intents ---------------------------------
    #
    # Mirrors firmware/src/yat_voice.cpp: matchPageByText() (longest normalized
    # alias found INSIDE the normalized transcript wins, ties to the lowest page
    # index), then the todo intent grammar, then the outcome written back to
    # /config.json exactly as savePageParams() does. The LLM tier is not
    # modelled: YAT SAY does not take it on the device either.

    def _page_aliases(self, page: dict) -> list[str]:
        """config.json's per-page override if it has one, else the pack's own."""
        own = page.get("aliases") or []
        if own:
            return [str(a) for a in own]
        raw = self.files.get(f"/packs/{page.get('pack')}.yat-pack.json")
        if not raw:
            return []
        try:
            pack = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            return []
        out: list[str] = []
        for group in ("en", "zh-Hant", "jyutping"):
            out.extend(str(a) for a in (pack.get("aliases") or {}).get(group, []))
        return out

    def _match_page(self, text: str) -> int:
        needle, _ = normalize(text)
        if not needle:
            return -1
        best, best_len = -1, 0
        for i, p in enumerate(self._config_pages()[0]):
            for alias in self._page_aliases(p):
                norm, _ = normalize(alias)
                if norm and norm in needle and len(norm) > best_len:
                    best, best_len = i, len(norm)
        return best

    def _todo_page(self) -> int:
        for i, p in enumerate(self._config_pages()[0]):
            if p.get("pack") == "todo":
                return i
        return -1

    @staticmethod
    def _parse_verb(text: str) -> tuple[str, str]:
        """todoParseTier1(): a verb at the front of the NORMALIZED transcript,
        payload sliced out of the ORIGINAL one so it keeps its spaces."""
        norm, src = normalize(text)
        for verb, kind in TODO_VERBS:
            if norm.startswith(verb):
                return kind, text[src[len(verb)]:]
        return "", ""

    def _save_todo(self, idx: int, items: list[dict]) -> None:
        cfg = json.loads(self.files["/config.json"].decode("utf-8"))
        cfg["pages"][idx].setdefault("params", {})["items"] = items
        cfg["page_id"] = cfg["pages"][idx]["id"]  # savePageIdx: the list is what shows
        self.files["/config.json"] = json.dumps(cfg, ensure_ascii=False).encode("utf-8")

    def _apply_todo(self, kind: str, payload: str) -> bool:
        idx = self._todo_page()
        if idx < 0:
            return False
        page = self._config_pages()[0][idx]
        items = list((page.get("params") or {}).get("items") or [])

        if kind == "add":
            item = payload.strip()[:TODO_MAX_TEXT_CP]
            if not item or len(items) >= TODO_MAX_ITEMS:
                return False
            if any(it.get("text") == item for it in items):
                return False
            items.append({"text": item, "done": False})
            self._save_todo(idx, items)
            open_n = sum(1 for it in items if not it.get("done"))
            self._emit(f"TODO ADD {len(items)} {open_n}")
            return True

        if kind == "done":
            needle, _ = normalize(payload)
            if not needle:
                return False
            hits = [
                i for i, it in enumerate(items)
                if not it.get("done")
                and normalize(str(it.get("text", "")))[0]
                and (needle in normalize(str(it.get("text", "")))[0]
                     or normalize(str(it.get("text", "")))[0] in needle)
            ]
            if len(hits) != 1:
                return False
            items[hits[0]] = dict(items[hits[0]], done=True)
            self._save_todo(idx, items)
            open_n = sum(1 for it in items if not it.get("done"))
            self._emit(f"TODO DONE {hits[0]} {len(items)} {open_n}")
            return True

        if kind == "clear":
            kept = [it for it in items if not it.get("done")]
            removed = len(items) - len(kept)
            if removed == 0:
                return False
            self._save_todo(idx, kept)
            self._emit(f"TODO CLEAR {removed} {len(kept)} {len(kept)}")
            return True
        return False

    def _cmd_say(self, text: str) -> None:
        page_idx = self._match_page(text)
        kind, payload = ("", "")
        if self._todo_page() >= 0:
            kind, payload = self._parse_verb(text)
        # An alias beats a bare 加; DONE and CLEAR are never second-guessed.
        if kind == "add" and page_idx >= 0:
            kind = ""
        if kind:
            if not self._apply_todo(kind, payload):
                self._emit("NOMATCH")
            return
        if page_idx >= 0:
            pages, _ = self._config_pages()
            cfg = json.loads(self.files["/config.json"].decode("utf-8"))
            cfg["page_id"] = pages[page_idx].get("id")
            self.files["/config.json"] = json.dumps(cfg, ensure_ascii=False).encode("utf-8")
            self._emit(f"MATCH {page_idx} {pages[page_idx].get('id')}")
            return
        self._emit("NOMATCH")

    def _cmd_secret(self, args: str) -> None:
        sp = args.find(" ")
        if sp < 0:
            self._emit("ERR usage: SECRET <name> <value>")
            return
        name = args[:sp]
        value = args[sp + 1 :]
        if value == "-":
            self.secrets.pop(name, None)
        else:
            self.secrets[name] = value
        self._emit("OK")


CTO = 2.0   # control-line timeout for tests
PTO = 5.0   # payload timeout for tests


class FramingTests(unittest.TestCase):
    def test_ls_empty_then_files(self):
        dev = FakeDevice()
        entries = yat.cmd_ls(dev, CTO)
        self.assertEqual(entries, [("/config.json", len(dev.files["/config.json"]))])

    def test_get_roundtrip_ascii(self):
        dev = FakeDevice()
        data = yat.cmd_get(dev, "/config.json", CTO, PTO)
        self.assertEqual(data, dev.files["/config.json"])

    def test_get_not_found(self):
        dev = FakeDevice()
        with self.assertRaises(yat.YatError) as ctx:
            yat.cmd_get(dev, "/packs/nope.yat-pack.json", CTO, PTO)
        self.assertIn("not found", str(ctx.exception))

    def test_get_path_not_allowed(self):
        dev = FakeDevice()
        with self.assertRaises(yat.YatError) as ctx:
            yat.cmd_get(dev, "/etc/passwd", CTO, PTO)
        self.assertIn("not allowed", str(ctx.exception))

    def test_put_and_get_roundtrip_cjk(self):
        dev = FakeDevice()
        payload = json.dumps(
            {"id": "cjk-test", "name": {"zh-Hant": "壽司郎排隊", "en": "Sushiro Queue"}},
            ensure_ascii=False,
        ).encode("utf-8")
        written = yat.cmd_put(dev, "/packs/cjk-test.yat-pack.json", payload, CTO, PTO)
        self.assertEqual(written, len(payload))
        fetched = yat.cmd_get(dev, "/packs/cjk-test.yat-pack.json", CTO, PTO)
        self.assertEqual(fetched, payload)  # byte-for-byte, not just JSON-equal
        self.assertIn("壽司郎排隊".encode("utf-8"), fetched)

    def test_put_invalid_json_rejected(self):
        dev = FakeDevice()
        with self.assertRaises(yat.YatError) as ctx:
            yat.cmd_put(dev, "/config.json", b"not json", CTO, PTO)
        self.assertIn("invalid json", str(ctx.exception))

    def test_put_rejects_oversize_locally(self):
        dev = FakeDevice()
        with self.assertRaises(yat.YatError):
            yat.cmd_put(dev, "/config.json", b"{}" + b" " * 70000, CTO, PTO)

    def test_rm(self):
        dev = FakeDevice()
        dev.files["/packs/x.yat-pack.json"] = b'{"id":"x"}'
        yat.cmd_rm(dev, "/packs/x.yat-pack.json", CTO)
        self.assertNotIn("/packs/x.yat-pack.json", dev.files)

    def test_rm_not_found(self):
        dev = FakeDevice()
        with self.assertRaises(yat.YatError):
            yat.cmd_rm(dev, "/packs/ghost.yat-pack.json", CTO)

    def test_status_pretty_prints_arbitrary_fields(self):
        dev = FakeDevice()
        status = yat.cmd_status(dev, CTO)
        self.assertEqual(status["fw"], "0.3.0-dev")
        self.assertIn("pages", status)
        self.assertIn("page_id", status)
        self.assertIn("voice_ready", status)

    def test_reboot(self):
        dev = FakeDevice()
        yat.cmd_reboot(dev, CTO)  # must not raise

    def test_debug_log_noise_is_skipped(self):
        """LOGF() mirrors debug lines to the same stream as protocol responses
        (firmware/src/main.cpp) — verify yat.py skips a stray '[...]' line
        instead of choking on it."""
        dev = FakeDevice()
        dev.log_noise_next = True
        status = yat.cmd_status(dev, CTO)
        self.assertEqual(status["fw"], "0.3.0-dev")

    def test_unknown_command_surfaces_as_error(self):
        dev = FakeDevice()
        dev.write(b"YAT BOGUS\n")
        line = yat._read_line(dev, CTO)
        self.assertEqual(line, "ERR unknown command")


class PagesVoiceSecretTests(unittest.TestCase):
    def _seed_pages(self, dev: FakeDevice) -> None:
        dev.files["/packs/hko-now.yat-pack.json"] = json.dumps(
            {"id": "hko-now", "aliases": {"en": ["weather"], "zh-Hant": ["天氣"]}},
            ensure_ascii=False,
        ).encode("utf-8")
        dev.files["/packs/sushiro-queue.yat-pack.json"] = json.dumps(
            {"id": "sushiro-queue", "aliases": {"en": ["sushiro"]}}
        ).encode("utf-8")
        dev.files["/config.json"] = json.dumps(
            {
                "v": 1,
                "pages": [
                    {"id": "weather", "pack": "hko-now"},
                    {"id": "sushiro-queue", "pack": "sushiro-queue"},
                ],
                "page_id": "weather",
            }
        ).encode("utf-8")

    def test_pages_listing_and_active_marker(self):
        dev = FakeDevice()
        self._seed_pages(dev)
        pages = yat.cmd_pages(dev, CTO)
        self.assertEqual(len(pages), 2)
        self.assertEqual(pages[0]["id"], "weather")
        self.assertTrue(pages[0]["active"])
        self.assertFalse(pages[1]["active"])
        self.assertEqual(pages[1]["id"], "sushiro-queue")

    def test_use_by_id_and_by_index(self):
        dev = FakeDevice()
        self._seed_pages(dev)
        idx, pid = yat.cmd_use(dev, "sushiro-queue", CTO)
        self.assertEqual((idx, pid), (1, "sushiro-queue"))
        idx, pid = yat.cmd_use(dev, "0", CTO)
        self.assertEqual((idx, pid), (0, "weather"))

    def test_use_no_such_page(self):
        dev = FakeDevice()
        self._seed_pages(dev)
        with self.assertRaises(yat.YatError) as ctx:
            yat.cmd_use(dev, "does-not-exist", CTO)
        self.assertIn("no such page", str(ctx.exception))

    def test_say_matches_pack_alias_cjk(self):
        dev = FakeDevice()
        self._seed_pages(dev)
        result = yat.cmd_say(dev, "天氣", CTO)
        self.assertEqual((result.kind, result.page_idx, result.page_id), ("page", 0, "weather"))

    def test_say_matches_alias_inside_a_sentence(self):
        dev = FakeDevice()
        self._seed_pages(dev)
        result = yat.cmd_say(dev, "切去天氣嗰頁", CTO)
        self.assertEqual((result.kind, result.page_idx), ("page", 0))

    def test_say_nomatch(self):
        dev = FakeDevice()
        self._seed_pages(dev)
        result = yat.cmd_say(dev, "xyzzy-nonsense", CTO)
        self.assertIsNone(result)

    def test_secret_set_and_clear(self):
        dev = FakeDevice()
        yat.cmd_secret(dev, "stt", "sk-fake-key-123", CTO)
        self.assertEqual(dev.secrets["stt"], "sk-fake-key-123")
        yat.cmd_secret(dev, "stt", "-", CTO)
        self.assertNotIn("stt", dev.secrets)


class TodoIntentTests(unittest.TestCase):
    """Voice intents for the household todo list (PRD §4.4, firmware
    yat_voice.cpp "voice intents" section).

    These drive `YAT SAY` — the same decision the microphone path takes once it
    has a transcript — and then read /config.json back, because the contract is
    not what the device says over the wire, it is what ends up on the list.
    """

    def _seed(self, dev: FakeDevice, items=None, with_todo=True, extra_pages=True) -> None:
        dev.files["/packs/hko-now.yat-pack.json"] = json.dumps(
            {"id": "hko-now", "aliases": {"en": ["weather"], "zh-Hant": ["天氣"]}},
            ensure_ascii=False,
        ).encode("utf-8")
        dev.files["/packs/todo.yat-pack.json"] = json.dumps(
            {"id": "todo", "aliases": {"en": ["todo", "list"], "zh-Hant": ["待辦", "清單"]}},
            ensure_ascii=False,
        ).encode("utf-8")
        pages = []
        if extra_pages:
            pages.append({"id": "weather", "pack": "hko-now"})
        if with_todo:
            pages.append({
                "id": "todo",
                "pack": "todo",
                # The pack's real params shape: title and lang sit beside the
                # items, and voice reads and writes nothing but the items.
                "params": {"title": "屋企要做嘅嘢",
                           "lang": "zh-Hant",
                           "items": list(items if items is not None else [])},
            })
        dev.files["/config.json"] = json.dumps(
            {"v": 2, "pages": pages, "page_id": pages[0]["id"], "quiet_hours": ["23:30", "06:30"]},
            ensure_ascii=False,
        ).encode("utf-8")

    @staticmethod
    def _items(dev: FakeDevice) -> list[dict]:
        cfg = json.loads(dev.files["/config.json"].decode("utf-8"))
        for p in cfg["pages"]:
            if p.get("pack") == "todo":
                return (p.get("params") or {}).get("items") or []
        return []

    @staticmethod
    def _params(dev: FakeDevice) -> dict:
        cfg = json.loads(dev.files["/config.json"].decode("utf-8"))
        for p in cfg["pages"]:
            if p.get("pack") == "todo":
                return p.get("params") or {}
        return {}

    @staticmethod
    def _active(dev: FakeDevice) -> str:
        return json.loads(dev.files["/config.json"].decode("utf-8")).get("page_id")

    def test_every_verb_leaves_the_other_params_alone(self):
        """title and lang sit beside items in the pack's params. Voice reads
        and writes items and nothing else, so a change must come back with the
        rest of the object intact — including keys a later pack version adds
        that this firmware has never heard of."""
        # 買菜 open so 搞掂 has exactly one candidate, 交電費 done so 清理 has
        # something to tidy — all three verbs succeed against this fixture.
        for said in ("加洗車", "搞掂買菜", "清理"):
            dev = FakeDevice()
            self._seed(dev, items=[{"text": "買菜", "done": False},
                                   {"text": "交電費", "done": True}])
            cfg = json.loads(dev.files["/config.json"].decode("utf-8"))
            cfg["pages"][-1]["params"]["a_key_from_a_later_pack"] = {"x": 1}
            dev.files["/config.json"] = json.dumps(cfg, ensure_ascii=False).encode("utf-8")

            self.assertIsNotNone(yat.cmd_say(dev, said, CTO), said)
            params = self._params(dev)
            self.assertEqual(params.get("title"), "屋企要做嘅嘢", said)
            self.assertEqual(params.get("lang"), "zh-Hant", said)
            self.assertEqual(params.get("a_key_from_a_later_pack"), {"x": 1}, said)
            # ...and so does the rest of config.json
            cfg = json.loads(dev.files["/config.json"].decode("utf-8"))
            self.assertEqual(cfg.get("quiet_hours"), ["23:30", "06:30"], said)
            self.assertEqual(cfg.get("v"), 2, said)

    # -- ADD ----------------------------------------------------------------

    def test_add_cantonese_verb_puts_the_item_on_the_list(self):
        dev = FakeDevice()
        self._seed(dev)
        result = yat.cmd_say(dev, "加買菜", CTO)
        self.assertEqual((result.kind, result.items, result.open_items), ("add", 1, 1))
        self.assertEqual(self._items(dev), [{"text": "買菜", "done": False}])
        # The list is what the panel shows next — the render IS the confirmation.
        self.assertEqual(self._active(dev), "todo")

    def test_add_english_verb_keeps_the_spaces_in_the_item(self):
        """Normalization strips spaces for MATCHING; the stored text is sliced
        out of the original transcript, so this is "buy milk", not "buymilk"."""
        dev = FakeDevice()
        self._seed(dev)
        yat.cmd_say(dev, "add buy milk", CTO)
        self.assertEqual([it["text"] for it in self._items(dev)], ["buy milk"])

    def test_add_second_verb_form(self):
        dev = FakeDevice()
        self._seed(dev)
        yat.cmd_say(dev, "記住交電費", CTO)
        self.assertEqual([it["text"] for it in self._items(dev)], ["交電費"])

    def test_add_duplicate_is_a_refusal_not_a_second_row(self):
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": False}])
        self.assertIsNone(yat.cmd_say(dev, "加買菜", CTO))
        self.assertEqual(self._items(dev), [{"text": "買菜", "done": False}])

    def test_add_at_the_item_cap_is_refused(self):
        """The cap is the pack schema's own maxItems (PACK-SPEC §3.2/§12.1
        caps array params at 20), not a number the firmware chose: an item
        voice accepted past it is one the settings page would drop on the next
        save."""
        dev = FakeDevice()
        full = [{"text": f"item{i}", "done": False} for i in range(TODO_MAX_ITEMS)]
        self._seed(dev, items=full)
        self.assertIsNone(yat.cmd_say(dev, "加多一樣嘢", CTO))
        self.assertEqual(len(self._items(dev)), TODO_MAX_ITEMS)

    def test_add_one_below_the_cap_still_works(self):
        dev = FakeDevice()
        full = [{"text": f"item{i}", "done": False} for i in range(TODO_MAX_ITEMS - 1)]
        self._seed(dev, items=full)
        result = yat.cmd_say(dev, "加多一樣嘢", CTO)
        self.assertEqual((result.kind, result.items), ("add", TODO_MAX_ITEMS))

    def test_add_clips_text_to_48_characters(self):
        dev = FakeDevice()
        self._seed(dev)
        yat.cmd_say(dev, "加" + "菜" * 60, CTO)
        self.assertEqual(len(self._items(dev)[0]["text"]), 48)

    def test_add_with_nothing_after_the_verb_is_refused(self):
        dev = FakeDevice()
        self._seed(dev)
        self.assertIsNone(yat.cmd_say(dev, "加", CTO))
        self.assertEqual(self._items(dev), [])

    def test_a_page_alias_beats_a_bare_add_verb(self):
        """加 is one character and starts plenty of ordinary words. When the
        utterance also names a page, the page wins — page switching is the
        thing every household already relies on."""
        dev = FakeDevice()
        self._seed(dev)
        result = yat.cmd_say(dev, "加天氣", CTO)
        self.assertEqual(result.kind, "page")
        self.assertEqual(self._items(dev), [])

    # -- DONE ---------------------------------------------------------------

    def test_done_exact_text_marks_that_item(self):
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": False},
                               {"text": "交電費", "done": False}])
        result = yat.cmd_say(dev, "搞掂買菜", CTO)
        self.assertEqual((result.kind, result.item_idx, result.open_items), ("done", 0, 1))
        self.assertEqual([it["done"] for it in self._items(dev)], [True, False])

    def test_done_matches_a_simplified_transcript_against_a_traditional_item(self):
        """The transcription service may hand back 简体 for Cantonese speech.
        Both sides go through the same s2t normalization, so 「买菜」 finds
        「買菜」 — this is the whole reason the shared table is loaded above."""
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": False}])
        result = yat.cmd_say(dev, "完成买菜", CTO)
        self.assertEqual(result.kind, "done")
        self.assertEqual(self._items(dev)[0]["done"], True)

    def test_done_matches_a_partial_phrase(self):
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "去街市買菜", "done": False},
                               {"text": "交電費", "done": False}])
        result = yat.cmd_say(dev, "搞掂買菜", CTO)
        self.assertEqual((result.kind, result.item_idx), ("done", 0))

    def test_done_with_two_candidates_refuses_rather_than_guessing(self):
        """The failure that matters is the WRONG item marked done. Two open
        items could be meant, so nothing happens and the device asks again."""
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": False},
                               {"text": "買菜同買魚", "done": False}])
        self.assertIsNone(yat.cmd_say(dev, "搞掂買菜", CTO))
        self.assertEqual([it["done"] for it in self._items(dev)], [False, False])

    def test_done_with_no_candidate_changes_nothing(self):
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": False}])
        self.assertIsNone(yat.cmd_say(dev, "搞掂洗車", CTO))
        self.assertEqual([it["done"] for it in self._items(dev)], [False])

    def test_done_ignores_items_that_are_already_done(self):
        """A finished item is not a candidate, so the same phrase can still
        find the one open item that matches it."""
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": True},
                               {"text": "買菜同買魚", "done": False}])
        result = yat.cmd_say(dev, "搞掂買菜", CTO)
        self.assertEqual((result.kind, result.item_idx), ("done", 1))

    def test_done_reports_the_absolute_index_not_the_open_one(self):
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "洗衫", "done": True},
                               {"text": "買菜", "done": False}])
        result = yat.cmd_say(dev, "搞掂買菜", CTO)
        self.assertEqual(result.item_idx, 1)

    # -- CLEAR --------------------------------------------------------------

    def test_clear_removes_done_items_and_never_open_ones(self):
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": True},
                               {"text": "交電費", "done": False},
                               {"text": "洗衫", "done": True}])
        result = yat.cmd_say(dev, "清理", CTO)
        self.assertEqual((result.kind, result.removed, result.items), ("clear", 2, 1))
        self.assertEqual(self._items(dev), [{"text": "交電費", "done": False}])

    def test_clear_with_nothing_done_is_refused(self):
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "交電費", "done": False}])
        self.assertIsNone(yat.cmd_say(dev, "清咗", CTO))
        self.assertEqual(self._items(dev), [{"text": "交電費", "done": False}])

    def test_no_voice_path_deletes_an_open_item(self):
        """The promise the docs make, checked rather than asserted in prose:
        run every verb in the grammar at a list of open items and none of them
        can shorten it."""
        dev = FakeDevice()
        start = [{"text": "買菜", "done": False}, {"text": "交電費", "done": False}]
        for verb in ("加", "記住", "add", "搞掂", "完成", "done", "清理", "清咗", "clear"):
            self._seed(dev, items=[dict(it) for it in start])
            yat.cmd_say(dev, f"{verb}買菜", CTO)
            texts = [it["text"] for it in self._items(dev)]
            self.assertIn("買菜", texts, verb)
            self.assertIn("交電費", texts, verb)

    # -- dormancy -----------------------------------------------------------

    def test_dormant_with_no_todo_page_installed(self):
        """No page uses the todo pack, so the verbs mean nothing: every one of
        them takes the path it took before intents existed — alias match, then
        the try-again tone."""
        dev = FakeDevice()
        self._seed(dev, with_todo=False)
        for verb in ("加", "記住", "add", "搞掂", "完成", "done", "清理", "清咗", "clear"):
            self.assertIsNone(yat.cmd_say(dev, f"{verb}買菜", CTO), verb)
        before = dev.files["/config.json"]
        self.assertIsNone(yat.cmd_say(dev, "加牛奶", CTO))
        self.assertEqual(dev.files["/config.json"], before)

    def test_page_switching_still_works_with_a_todo_page_installed(self):
        dev = FakeDevice()
        self._seed(dev)
        result = yat.cmd_say(dev, "天氣", CTO)
        self.assertEqual((result.kind, result.page_id), ("page", "weather"))
        result = yat.cmd_say(dev, "清單", CTO)
        self.assertEqual((result.kind, result.page_id), ("page", "todo"))
        self.assertEqual(self._items(dev), [])

    def test_page_switching_is_unchanged_when_no_todo_is_installed(self):
        dev = FakeDevice()
        self._seed(dev, with_todo=False)
        result = yat.cmd_say(dev, "天氣", CTO)
        self.assertEqual((result.kind, result.page_idx, result.page_id), ("page", 0, "weather"))
        self.assertIsNone(yat.cmd_say(dev, "xyzzy-nonsense", CTO))

    def test_a_verb_that_cannot_be_carried_out_does_not_become_a_page_switch(self):
        """「搞掂天氣」 with nothing on the list matching: the household asked for
        something to happen to their list, and changing the page instead would
        be a different action wearing the same beep."""
        dev = FakeDevice()
        self._seed(dev, items=[{"text": "買菜", "done": False}])
        self.assertIsNone(yat.cmd_say(dev, "搞掂天氣", CTO))
        self.assertEqual(self._active(dev), "weather")  # the seeded active page, untouched

    def test_a_todo_only_device_still_answers_its_own_page_alias(self):
        dev = FakeDevice()
        self._seed(dev, with_todo=True, extra_pages=False)
        result = yat.cmd_say(dev, "待辦", CTO)
        self.assertEqual((result.kind, result.page_idx), ("page", 0))


class InstallFlowTests(unittest.TestCase):
    def test_install_new_page_then_visible_in_pages(self):
        dev = FakeDevice()
        pack = {
            "yat": 1,
            "id": "sushiro-queue",
            "name": {"en": "Sushiro Queues", "zh-Hant": "壽司郎排隊"},
            "aliases": {"en": ["sushiro"], "zh-Hant": ["壽司郎"]},
        }
        with tempfile.TemporaryDirectory() as d:
            packfile = os.path.join(d, "sushiro-queue.yat-pack.json")
            raw = json.dumps(pack, ensure_ascii=False, indent=2).encode("utf-8")
            with open(packfile, "wb") as f:
                f.write(raw)

            page_id, packid, pages = yat.do_install(
                dev, packfile, None, '{"branch": "causeway-bay"}', "queue,壽司", CTO, PTO
            )

        self.assertEqual(packid, "sushiro-queue")
        self.assertEqual(page_id, "sushiro-queue")

        # pack file landed byte-for-byte, CJK included
        stored_pack = dev.files["/packs/sushiro-queue.yat-pack.json"]
        self.assertEqual(stored_pack, raw)
        self.assertIn("壽司郎排隊".encode("utf-8"), stored_pack)

        # config.json now has the page entry
        cfg = json.loads(dev.files["/config.json"].decode("utf-8"))
        entries = [p for p in cfg["pages"] if p["id"] == "sushiro-queue"]
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["pack"], "sushiro-queue")
        self.assertEqual(entries[0]["params"], {"branch": "causeway-bay"})
        self.assertEqual(entries[0]["aliases"], ["queue", "壽司"])

        # and YAT PAGES reflects it
        self.assertTrue(any(p["id"] == "sushiro-queue" for p in pages))

    def test_install_with_custom_id_and_reinstall_replaces(self):
        dev = FakeDevice()
        pack = {"id": "hko-now", "name": {"en": "Weather"}}
        with tempfile.TemporaryDirectory() as d:
            packfile = os.path.join(d, "hko-now.yat-pack.json")
            with open(packfile, "wb") as f:
                f.write(json.dumps(pack).encode("utf-8"))

            page_id, packid, _ = yat.do_install(dev, packfile, "weather", None, None, CTO, PTO)
            self.assertEqual(page_id, "weather")
            self.assertEqual(packid, "hko-now")

            # Re-install with different params -> same page entry updated, not duplicated
            page_id2, _, pages2 = yat.do_install(
                dev, packfile, "weather", '{"district": "Central"}', None, CTO, PTO
            )
            self.assertEqual(page_id2, "weather")

        cfg = json.loads(dev.files["/config.json"].decode("utf-8"))
        weather_entries = [p for p in cfg["pages"] if p["id"] == "weather"]
        self.assertEqual(len(weather_entries), 1)
        self.assertEqual(weather_entries[0]["params"], {"district": "Central"})

    def test_install_rejects_pack_without_id(self):
        dev = FakeDevice()
        with tempfile.TemporaryDirectory() as d:
            packfile = os.path.join(d, "bad.yat-pack.json")
            with open(packfile, "wb") as f:
                f.write(b'{"name": "no id here"}')
            with self.assertRaises(yat.YatError):
                yat.do_install(dev, packfile, None, None, None, CTO, PTO)


class SetConfigTests(unittest.TestCase):
    def test_set_config_roundtrip(self):
        dev = FakeDevice()
        with tempfile.TemporaryDirectory() as d:
            localfile = os.path.join(d, "config.json")
            payload = {"v": 1, "pages": [], "quiet_hours": ["23:00", "07:00"]}
            with open(localfile, "wb") as f:
                f.write(json.dumps(payload).encode("utf-8"))
            written = yat.do_set_config(dev, localfile, CTO, PTO)
        self.assertEqual(written, len(json.dumps(payload).encode("utf-8")))
        self.assertEqual(json.loads(dev.files["/config.json"]), payload)

    def test_set_config_rejects_bad_json(self):
        dev = FakeDevice()
        with tempfile.TemporaryDirectory() as d:
            localfile = os.path.join(d, "config.json")
            with open(localfile, "wb") as f:
                f.write(b"{not valid json")
            with self.assertRaises(yat.YatError):
                yat.do_set_config(dev, localfile, CTO, PTO)


class AutodetectTests(unittest.TestCase):
    def test_no_ports_found(self):
        import glob as glob_mod

        orig = glob_mod.glob
        glob_mod.glob = lambda pattern: []
        try:
            with self.assertRaises(yat.YatError) as ctx:
                yat.autodetect_port()
            self.assertIn("no serial port found", str(ctx.exception))
        finally:
            glob_mod.glob = orig

    def test_ambiguous_ports_raise(self):
        import glob as glob_mod

        orig = glob_mod.glob
        glob_mod.glob = lambda pattern: (
            ["/dev/cu.usbserial-A", "/dev/cu.usbserial-B"] if "cu.usbserial" in pattern else []
        )
        try:
            with self.assertRaises(yat.YatError) as ctx:
                yat.autodetect_port()
            self.assertIn("multiple candidate", str(ctx.exception))
        finally:
            glob_mod.glob = orig

    def test_single_port_found(self):
        import glob as glob_mod

        orig = glob_mod.glob
        glob_mod.glob = lambda pattern: (
            ["/dev/cu.usbserial-XYZ"] if "cu.usbserial" in pattern else []
        )
        try:
            self.assertEqual(yat.autodetect_port(), "/dev/cu.usbserial-XYZ")
        finally:
            glob_mod.glob = orig


if __name__ == "__main__":
    unittest.main(verbosity=2)
