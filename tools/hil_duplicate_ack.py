#!/usr/bin/env python3
"""HIL check: a repeated DATA frame (lost ACK) must not be written twice.

Usage:
    python tools/hil_duplicate_ack.py --port COM6
    python tools/hil_duplicate_ack.py --port COM6 --image release/firmware/app_slot_a.hex
"""
from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import iap_proto as P  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM6")
    ap.add_argument("--image", default="release/firmware/app_slot_a.hex")
    ap.add_argument("--version", default="1.2.2")
    ap.add_argument("--key", default="keys/demo_signing.key")
    ap.add_argument("--keep", action="store_true", help="do not reboot after END")
    args = ap.parse_args()

    image = P.load_image(args.image)
    slot = P.analyze_image(image)["slot"]
    if slot is None:
        print("image is not linked for slot A/B")
        return 2
    digest = hashlib.sha256(image).digest()
    checksum = P.crc32(image)
    version = P.parse_version(args.version)
    seed = bytes.fromhex(Path(args.key).read_text(encoding="utf-8").strip())
    signature = P.sign_message(seed, P.manifest(version, len(image), checksum, digest))

    link = P.Link(args.port)
    hello = link.hello()
    if slot == hello["active"]:
        print(f"image targets active slot {P.slot_name(slot)}; use the other slot")
        link.close()
        return 2

    print(f"target slot {P.slot_name(slot)}, {len(image)} bytes, v{args.version}")
    target, offset = link.start(version, len(image), checksum, digest, slot, signature)
    if offset != 0:
        print(f"unexpected resume offset {offset}")
        link.close()
        return 3

    first = image[: P.CHUNK_SIZE]
    a = link.data(0, first)
    b = link.data(0, first)          # duplicate frame: ACK must not advance
    print(f"first frame -> {a}, duplicate -> {b}")
    if a != len(first) or b != len(first):
        print("FAIL: duplicate frame changed the write offset")
        link.close()
        return 4

    sent = len(first)
    while sent < len(image):
        chunk = image[sent : sent + P.CHUNK_SIZE]
        nxt = link.data(sent, chunk)
        sent += len(chunk)
        if nxt != sent:
            print(f"FAIL: offset mismatch {nxt} != {sent}")
            link.close()
            return 5

    slot_out, version_out = link.end(checksum, digest)
    print(f"END ok: slot {P.slot_name(slot_out)} v{P.format_version(version_out)}")

    if not args.keep:
        link.reboot()
        time.sleep(1.0)
    link.close()

    if not args.keep:
        time.sleep(4.0)               # selftest + auto confirm
        link = P.Link(args.port)
        time.sleep(0.3)
        try:
            st = link.status()
            print("status after reboot:", st)
        except Exception as exc:      # noqa: BLE001
            print("status read failed (device may be running the app):", exc)
        link.close()

    print("PASS: duplicate DATA frame was acknowledged without rewriting")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
