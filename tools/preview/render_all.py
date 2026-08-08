#!/usr/bin/env python3
"""Render every pack with its fixtures into one directory — the fleet audit.

Wraps the native engine binary (yat-preview). Zero dependencies.

  cd tools/preview && make && python3 render_all.py
  → out/render-all/*.png  +  a per-pack summary on stdout

The goldens in run-tests.sh pin a handful of pages byte-for-byte; this renders
*all* of them at once so a change to the engine, the fonts or the chrome can be
eyeballed across the whole fleet. Nothing here asserts — it renders, reports
what the engine said about each page, and exits non-zero only if a pack failed
to render at all. `--strict` also fails on render warnings (overflow, E_BIND,
placeholder boxes), which is the mode worth wiring into CI.

`--profile e1002|e1001` (default e1002) picks the device capability profile to
render against, same names yat-preview's own --profile / deviceProfileFromName
accept (ROADMAP "device capability profiles"). A full mono fleet audit is one
command: `python3 render_all.py --profile e1001`, which writes to
out/render-all-e1001/ instead of the default out/render-all/ — pass --out to
override either way.

The 17 real-world example packs now live in the yat-hk/yat-packs community
repo; this repo only carries packs/internal (firmware-embedded) and
packs/examples/render-test.yat-pack.json (the engine-conformance pack). By
default this script also audits `../yat-packs/official` (a sibling checkout)
for the real fleet — pass `--packs-dir` to point elsewhere, or ignore the
printed note if you don't have that checkout and just want the local packs.
`--include-testpacks` adds the frozen engine-test fixtures under testpacks/
(same packs tools/preview/run-tests.sh pins for engine-mechanics coverage,
not appearance) to the audit.

Fixtures follow the simulator's convention, `fixtures/<packid>.<sourceid>.<ext>`
with ext json|xml|csv, plus:
  * a `for_each` source takes the numbered set `<packid>.<sourceid>.<n>.<ext>`,
    passed as the comma list the engine expects;
  * an `image` widget has no source id — the engine fetches it as the pseudo-id
    "image", so `<packid>.image.png` wires it up (§9.10);
  * `<packid>.params.json`, if present, is passed as --params, which is how a
    pack whose defaults do not render anything interesting (photo-frame) gets a
    meaningful page here.
Variant fixtures (`<packid>.<sourceid>-something.<ext>`) are deliberately NOT
picked up: they exist for the targeted cases in run-tests.sh, and guessing
between them would make this tool's output depend on directory order.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BINARY = os.path.join(HERE, "yat-preview")
FIXTURES = os.path.join(HERE, "fixtures")
TESTPACKS = os.path.join(HERE, "testpacks")
# The 17 real-world example packs moved to the yat-hk/yat-packs community
# repo; this core repo now only carries packs/internal (firmware-embedded,
# never user-installed) and packs/examples/render-test.yat-pack.json (the
# engine-conformance pack). The fleet's real packs are found via --packs-dir
# below, a sibling checkout of yat-packs.
PACK_DIRS = [
    os.path.join(ROOT, "packs", "examples"),
    os.path.join(ROOT, "packs", "internal"),
]
DEFAULT_PACKS_DIR = os.path.join(ROOT, "..", "yat-packs", "official")
DEFAULT_OUT = os.path.join(HERE, "out", "render-all")
# The same fixed instant run-tests.sh pins, so a page rendered here and the same
# page rendered by a golden test differ only where the pack differs.
DEFAULT_NOW = 1785142800
EXTS = ("json", "xml", "csv")


def find_packs(pack_dirs):
    out = []
    for d in pack_dirs:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".yat-pack.json"):
                out.append(os.path.join(d, name))
    return out


def fixture_args(pack_id, pack):
    """--doc flags for one pack, plus the fixture-less source ids."""
    args, missing = [], []
    for src in pack.get("data", {}).get("sources", []):
        sid = src.get("id", "")
        # An `inline` source carries its data in the pack; it never fetches, so
        # it is not a fixture gap.
        if not sid or src.get("type") == "inline":
            continue
        if "for_each" in src:
            # Numbered set, in order: .1 .2 .3 … Stop at the first gap so a
            # half-captured set never silently renders short.
            group = []
            for n in range(1, 33):
                hit = next((p for p in (os.path.join(FIXTURES, f"{pack_id}.{sid}.{n}.{e}")
                                        for e in EXTS) if os.path.isfile(p)), None)
                if hit is None:
                    break
                group.append(hit)
            if group:
                args += ["--doc", f"{sid}=" + ",".join(group)]
            else:
                missing.append(sid)
            continue
        hit = next((p for p in (os.path.join(FIXTURES, f"{pack_id}.{sid}.{e}")
                                for e in EXTS) if os.path.isfile(p)), None)
        if hit:
            args += ["--doc", f"{sid}={hit}"]
        else:
            missing.append(sid)
    img = os.path.join(FIXTURES, f"{pack_id}.image.png")
    if os.path.isfile(img):
        args += ["--doc", f"image={img}"]
    return args, missing


def render(path, out_dir, now, profile, extra):
    pack_id = os.path.basename(path)[: -len(".yat-pack.json")]
    pack = json.load(open(path, encoding="utf-8"))
    png = os.path.join(out_dir, pack_id + ".png")
    cmd = [BINARY, path, "--now", str(now), "--profile", profile, "--out", png]
    docs, missing = fixture_args(pack_id, pack)
    cmd += docs
    params = os.path.join(FIXTURES, f"{pack_id}.params.json")
    if os.path.isfile(params):
        cmd += ["--params", open(params, encoding="utf-8").read().strip()]
    cmd += extra
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    warns = [ln for ln in r.stderr.splitlines() if ln.startswith("render warn:")]
    ok = r.returncode == 0 and os.path.isfile(png)
    return {
        "id": pack_id,
        "ok": ok,
        "png": png if ok else None,
        "warns": warns,
        "missing": missing,
        # The engine prints the whole extraction to stderr; on a hard failure the
        # last line is the one that says why.
        "error": "" if ok else (r.stderr.strip().splitlines() or ["render failed"])[-1],
    }


def main():
    ap = argparse.ArgumentParser(description="Render every pack with its fixtures (fleet audit).")
    ap.add_argument("--out", default=None,
                    help=f"output directory (default: {DEFAULT_OUT} for e1002, "
                         f"out/render-all-<profile> for any other --profile)")
    ap.add_argument("--now", type=int, default=DEFAULT_NOW,
                    help=f"pinned epoch seconds for a deterministic render (default: {DEFAULT_NOW})")
    ap.add_argument("--profile", default="e1002",
                    help="device capability profile to render against — same names the "
                         "yat-preview binary's --profile takes (deviceProfileFromName), e.g. "
                         "e1002, e1001 (default: e1002)")
    ap.add_argument("--only", default="", help="render just the packs whose id contains this substring")
    ap.add_argument("--strict", action="store_true", help="exit non-zero on render warnings too")
    ap.add_argument("--packs-dir", default=DEFAULT_PACKS_DIR,
                    help="directory of real-world packs to audit alongside packs/internal and "
                         f"packs/examples (default: {DEFAULT_PACKS_DIR}, a sibling yat-packs "
                         "checkout's official/ directory — clone github.com/yat-hk/yat-packs "
                         "next to this repo, or pass a path of your own). Missing is not fatal: "
                         "the audit falls back to the local packs only.")
    ap.add_argument("--include-testpacks", action="store_true",
                    help="also audit the frozen engine-test fixtures under testpacks/ "
                         "(tools/preview/testpacks/README.md explains what they're for)")
    ap.add_argument("--", dest="_sep", nargs="?", help=argparse.SUPPRESS)
    args, extra = ap.parse_known_args()

    out_dir = args.out or (DEFAULT_OUT if args.profile == "e1002"
                            else os.path.join(HERE, "out", f"render-all-{args.profile}"))

    if not os.path.isfile(BINARY):
        raise SystemExit("build first: cd tools/preview && make")
    os.makedirs(out_dir, exist_ok=True)

    pack_dirs = list(PACK_DIRS)
    if os.path.isdir(args.packs_dir):
        pack_dirs.append(args.packs_dir)
    else:
        print(f"  (note) --packs-dir {args.packs_dir!r} not found — auditing "
              "packs/internal and packs/examples only. Clone github.com/yat-hk/yat-packs "
              "next to this repo for the full fleet.")
    if args.include_testpacks:
        pack_dirs.append(TESTPACKS)

    packs = [p for p in find_packs(pack_dirs) if args.only in os.path.basename(p)]
    if not packs:
        raise SystemExit(f"no packs matched --only {args.only!r}")

    results = [render(p, out_dir, args.now, args.profile, extra) for p in packs]
    width = max(len(r["id"]) for r in results)
    failed = warned = 0
    for r in results:
        if not r["ok"]:
            failed += 1
            print(f"  {r['id']:<{width}}  FAILED   {r['error']}")
            continue
        notes = []
        if r["missing"]:
            notes.append("no fixture for " + ", ".join(r["missing"]))
        if r["warns"]:
            warned += 1
            notes.append(f"{len(r['warns'])} render warning(s)")
        print(f"  {r['id']:<{width}}  ok       {'; '.join(notes)}")
        for w in r["warns"]:
            print(f"  {'':<{width}}           {w}")

    print(f"\n{len(results)} pack(s) → {out_dir}   {failed} failed, {warned} with warnings")
    if failed or (args.strict and warned):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
