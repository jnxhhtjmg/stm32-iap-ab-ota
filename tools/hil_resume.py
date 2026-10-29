#!/usr/bin/env python3
"""HIL check: interrupt a transfer and resume from the persisted offset.

Usage:
    python tools/hil_resume.py --port COM6
"""
from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import iap_proto as P  # noqa: E402


def sign_start(link: P.Link, image: bytes, slot: int, version: int, key: str):
    digest = hashlib.sha256(image).digest()
    checksum = P.crc32(image)
    seed = bytes.fromhex(Path(key).read_text(encoding="utf-8").strip())
    sig = P.sign_message(seed, P.manifest(version, len(image), checksum, digest))
    target, offset = link.start(version, len(image), checksum, digest, slot, sig)
    return digest, checksum, target, offset


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM6")
    ap.add_argument("--image", default="release/firmware/app_slot_a.hex")
    ap.add_argument("--version", default="1.2.7")
    ap.add_argument("--key", default="keys/demo_signing.key")
    args = ap.parse_args()

    image = P.load_image(args.image)
    slot = P.analyze_image(image)["slot"]
    version = P.parse_version(args.version)

    link = P.Link(args.port)
    hello = link.hello()
    if slot == hello["active"]:
        print("image targets the active slot; use the other image")
        link.close()
        return 2

    digest, checksum, _, offset = sign_start(link, image, slot, version, args.key)
    if offset != 0:
        print(f"unexpected initial offset {offset}")
        link.close()
        return 3

    # Send a bit more than one resume granule, then reboot without END.
    stop = min(len(image), 5000)
    sent = 0
    while sent < stop:
        chunk = image[sent : sent + P.CHUNK_SIZE]
        nxt = link.data(sent, chunk)
        sent += len(chunk)
        if nxt != sent:
            print(f"FAIL offset {nxt} != {sent}")
            link.close()
            return 4
    print(f"interrupted at {sent}, persisted should be 4096")
    link.reboot()
    link.close()
    time.sleep(1.5)

    # The bootloader should stay in update mode because DOWNLOADING exists.
    link = P.Link(args.port)
    hello = link.hello()
    digest, checksum, _, offset = sign_start(link, image, slot, version, args.key)
    print(f"resume offset = {offset}")
    if offset != 4096:
        print("FAIL: expected resume at 4096")
        link.close()
        return 5

    sent = offset
    while sent < len(image):
        chunk = image[sent : sent + P.CHUNK_SIZE]
        nxt = link.data(sent, chunk)
        sent += len(chunk)
        if nxt != sent:
            print(f"FAIL offset {nxt} != {sent}")
            link.close()
            return 6

    link.end(checksum, digest)
    print("END ok after resume")
    link.reboot()
    link.close()
    print("PASS: transfer resumed from 4096")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
