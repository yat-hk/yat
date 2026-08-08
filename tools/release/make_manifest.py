#!/usr/bin/env python3
"""Generate manifest-<model>.json for a firmware release.

Usage:
    make_manifest.py --model e1002 --version v0.5.0 --output manifest-e1002.json \
        app=firmware-e1002.bin factory=firmware-e1002.factory.bin uf2=firmware-e1002.uf2

Each positional FILE arg is `role=path`. The manifest records, per role, the
artifact's on-disk name, size in bytes and sha256 — enough for a flasher site
(ROADMAP v0.5) to fetch and verify a release without re-deriving anything.
"""
import argparse
import hashlib
import json
import os
import sys


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_file_arg(arg):
    if "=" not in arg:
        raise argparse.ArgumentTypeError(
            f"expected role=path, got {arg!r}"
        )
    role, path = arg.split("=", 1)
    if not role:
        raise argparse.ArgumentTypeError(f"empty role in {arg!r}")
    return role, path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--model", required=True, help="model id, e.g. e1002")
    ap.add_argument(
        "--version", required=True, help="release tag or dev-<sha> string"
    )
    ap.add_argument("--output", required=True, help="path to write manifest JSON")
    ap.add_argument(
        "files",
        nargs="+",
        type=parse_file_arg,
        help="role=path pairs, e.g. app=firmware-e1002.bin",
    )
    args = ap.parse_args()

    manifest = {
        "model": args.model,
        "version": args.version,
        "files": {},
    }

    for role, path in args.files:
        if not os.path.isfile(path):
            print(f"error: {path!r} (role {role!r}) does not exist", file=sys.stderr)
            return 1
        manifest["files"][role] = {
            "name": os.path.basename(path),
            "size": os.path.getsize(path),
            "sha256": sha256_of(path),
        }

    with open(args.output, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
