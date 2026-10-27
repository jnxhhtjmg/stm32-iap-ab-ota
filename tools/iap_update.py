#!/usr/bin/env python3
"""Command line tool for the A/B IAP bootloader.

Examples:
    python tools/iap_update.py status -p COM5
    python tools/iap_update.py flash app/MDK/app_slot_b/app_slot_b.hex -p COM5 --version 1.0.2
    python tools/iap_update.py provision app/MDK/app_slot_a/app_slot_a.hex -s a --version 1.0.1 -o meta_a.bin
    python tools/iap_update.py selftest
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import time
from pathlib import Path

import iap_proto as P


def log(msg: str) -> None:
    print(msg, flush=True)


def open_link(port: str, baud: int, wait_s: float) -> P.Link:
    link = P.Link(port, 115200)
    deadline = time.monotonic() + wait_s
    last = None
    while True:
        try:
            link.hello(timeout=0.5)
            break
        except P.ProtocolError as exc:
            last = exc
            if time.monotonic() >= deadline:
                link.close()
                raise SystemExit(
                    f"设备无响应（{last}）。\n"
                    "请按一下板子复位键，或确认 BOOT0=0、串口没被别的程序占用。"
                )
            time.sleep(0.2)
    if baud != 115200:
        log(f"切换到 {baud} ...")
        link.set_baud(baud)
        link.hello(timeout=2.0)
    return link


def print_status(st: dict, hello: dict) -> None:
    log(f"bootloader : protocol {hello['proto']}  build {P.format_version(hello['boot_version'])}")
    log(f"active     : slot {P.slot_name(st['active'])}  state {P.STATE_TEXT.get(st['state'], '?')}")
    for slot in (P.SLOT_A, P.SLOT_B):
        info = st["slots"][slot]
        if info["present"]:
            size_txt = f"{info['size']} bytes" if info["size"] else "size unknown"
            log(
                f"slot {P.slot_name(slot)}     : v{P.format_version(info['version'])}  "
                f"{size_txt}  {P.STATE_TEXT.get(info['state'], '?')}  "
                f"attempts {info['attempts']}"
            )
        else:
            log(f"slot {P.slot_name(slot)}     : empty")


def cmd_status(args) -> int:
    link = open_link(args.port, args.baud, args.wait)
    try:
        hello = link.hello()
        st = link.status()
        print_status(st, hello)
    finally:
        link.close()
    return 0


def cmd_flash(args) -> int:
    image = P.load_image(args.file)
    info = P.analyze_image(image)
    if info["size"] > P.IMAGE_MAX:
        raise SystemExit(f"镜像 {info['size']} 字节，超过上限 {P.IMAGE_MAX}")
    link_slot = info["slot"]
    if link_slot is None:
        raise SystemExit("无法从向量表判断链接槽，文件可能不是本项目的应用")
    log(f"image      : {info['size']} bytes, linked for slot {P.slot_name(link_slot)}")
    log(f"version    : {args.version}")

    try:
        seed = bytes.fromhex(Path(args.key).read_text(encoding="utf-8").strip())
    except OSError as exc:
        raise SystemExit(f"读不到签名私钥 {args.key}: {exc}") from exc
    except ValueError as exc:
        raise SystemExit(f"私钥格式不对（应为 64 位十六进制）: {exc}") from exc
    log(f"signing    : {args.key}")

    link = open_link(args.port, args.baud, args.wait)
    try:
        hello = link.hello()
        active = hello["active"]
        target = link_slot
        if target == active:
            link.close()
            raise SystemExit(
                f"这个文件链接到 slot {P.slot_name(target)}，正是当前运行槽，不能覆盖。\n"
                "请使用另一个槽编译出的镜像。"
            )
        log(f"target     : slot {P.slot_name(target)} (active is {P.slot_name(active)})")
        log("erasing and uploading ...")
        started = time.monotonic()
        summary = P.upload(
            link,
            image,
            target,
            P.parse_version(args.version),
            progress=lambda sent, total: None,
            log=log,
            signer=lambda digest: P.sign_message(seed, digest),
        )
        elapsed = time.monotonic() - started
        log(
            f"done       : slot {P.slot_name(summary['slot'])} v{P.format_version(summary['version'])} "
            f"in {elapsed:.1f}s"
        )
        if args.reboot:
            log("rebooting into the trial image ...")
            link.reboot()
    finally:
        link.close()
    return 0


def cmd_info(args) -> int:
    link = open_link(args.port, args.baud, args.wait)
    try:
        info = link.info()
        log(f"boot       : {P.format_version(info['boot_version'])} (proto {info['protocol']})")
        log(f"active     : slot {P.slot_name(info['active'])}, {P.STATE_TEXT.get(info['state'], '?')}")
        log(f"version    : {P.format_version(info['version'])}")
        log(f"min version: {P.format_version(info['min_version'])}")
        log(f"image      : {info['size']} bytes, attempts {info['attempts']}")
    finally:
        link.close()
    return 0


def cmd_confirm(args) -> int:
    link = open_link(args.port, args.baud, args.wait)
    try:
        link.confirm()
        log("confirmed")
    finally:
        link.close()
    return 0


def cmd_reboot(args) -> int:
    link = open_link(args.port, args.baud, args.wait)
    try:
        link.reboot()
        log("reboot requested")
    finally:
        link.close()
    return 0


def cmd_provision(args) -> int:
    image = P.load_image(args.file)
    info = P.analyze_image(image)
    slot = args.slot_index if args.slot_index is not None else info["slot"]
    if slot is None:
        raise SystemExit("无法判断槽位，请用 --slot a|b")
    record = P.make_meta_record(image, slot, P.parse_version(args.version), args.seq)
    Path(args.out).write_bytes(record)
    log(f"wrote {args.out} ({len(record)} bytes) for slot {P.slot_name(slot)} v{args.version}")
    return 0


def cmd_selftest(args) -> int:
    ok = True

    def check(name: str, cond: bool, detail: str = "") -> None:
        nonlocal ok
        log(f"[{'PASS' if cond else 'FAIL'}] {name} {detail}")
        ok = ok and cond

    check("crc32(123456789)", P.crc32(b"123456789") == 0xCBF43926, hex(P.crc32(b"123456789")))
    check(
        "sha256(abc)",
        hashlib.sha256(b"abc").hexdigest().startswith("ba7816bf8f01cfea"),
        hashlib.sha256(b"abc").hexdigest()[:16],
    )
    check("version roundtrip", P.parse_version("1.2.3") == 0x01020300)
    check("version format", P.format_version(0x01020300) == "1.2.3.0")

    frame = P.build_frame(P.CMD_HELLO, 0x1234)
    check("frame sync", frame[:2] == P.SYNC)
    check("frame length", len(frame) == 2 + 6 + 4)

    # linked slot detection: fake image with reset vector inside slot B
    fake = bytearray(64)
    struct.pack_into("<II", fake, 0, 0x20000438, P.SLOT_B_BASE + 0x101)
    check("detect slot B", P.linked_slot(bytes(fake)) == P.SLOT_B)

    rec = P.make_meta_record(bytes(fake), P.SLOT_A, 0x00010001)
    check("record size", len(rec) == P.REC_SIZE)
    check(
        "record crc",
        struct.unpack_from("<I", rec, 60)[0] == P.crc32(rec[:60]),
    )
    check("record magic", struct.unpack_from("<I", rec, 0)[0] == P.META_MAGIC)

    log("OK" if ok else "FAILED")
    return 0 if ok else 1


def main(argv: list[str] | None = None) -> int:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("-p", "--port", default="COM3", help="serial port, e.g. COM5")
    common.add_argument("--baud", type=int, default=115200, choices=P.BAUDS)
    common.add_argument("--wait", type=float, default=3.0, help="seconds to wait for HELLO")

    parser = argparse.ArgumentParser(description="A/B IAP host tool", parents=[common])
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", parents=[common]).set_defaults(func=cmd_status)
    sub.add_parser("info", parents=[common]).set_defaults(func=cmd_info)
    sub.add_parser("confirm", parents=[common]).set_defaults(func=cmd_confirm)
    sub.add_parser("reboot", parents=[common]).set_defaults(func=cmd_reboot)
    sub.add_parser("selftest", parents=[common]).set_defaults(func=cmd_selftest)

    p_flash = sub.add_parser("flash", parents=[common])
    p_flash.add_argument("file")
    p_flash.add_argument("--version", default="1.0.1")
    p_flash.add_argument(
        "--key",
        default="keys/demo_signing.key",
        help="Ed25519 signing seed in hex (never commit a production key)",
    )
    p_flash.add_argument("--no-reboot", dest="reboot", action="store_false")
    p_flash.set_defaults(func=cmd_flash, reboot=True)

    p_prov = sub.add_parser("provision", parents=[common])
    p_prov.add_argument("file")
    p_prov.add_argument("-o", "--out", required=True)
    p_prov.add_argument("--version", default="1.0.1")
    p_prov.add_argument("--seq", type=int, default=1)
    group = p_prov.add_mutually_exclusive_group()
    group.add_argument("-a", "--slot-a", dest="slot_index", action="store_const", const=P.SLOT_A)
    group.add_argument("-b", "--slot-b", dest="slot_index", action="store_const", const=P.SLOT_B)
    p_prov.set_defaults(func=cmd_provision, slot_index=None)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except P.ProtocolError as exc:
        log(f"protocol error: {exc}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
