"""Shared protocol helpers for the A/B IAP bootloader.

Used by both the CLI (iap_update.py) and the GUI (iap_gui.py):
  - binary frame encode/decode (AA 55 | ver | type | seq | len | payload | crc32)
  - image loading (.hex / .bin), linked-slot detection
  - metadata record generation (factory provisioning)
"""
from __future__ import annotations

import hashlib
import struct
import time
import zlib
from pathlib import Path
from typing import Callable, Optional

SYNC = b"\xAA\x55"
PROTO_VERSION = 2
SIG_BYTES = 64
MAX_PAYLOAD = 512
CHUNK_SIZE = 508  # 4-byte offset + 508 bytes data = 512

# commands
CMD_HELLO = 0x01
CMD_START = 0x02
CMD_DATA = 0x03
CMD_END = 0x04
CMD_STATUS = 0x05
CMD_CONFIRM = 0x06
CMD_REBOOT = 0x07
CMD_SET_BAUD = 0x08
CMD_ABORT = 0x09
CMD_INFO = 0x0A
RSP = 0x80

# status codes
ST_OK = 0
STATUS_TEXT = {
    0: "OK",
    1: "CRC error",
    2: "length error",
    3: "state error",
    4: "size error",
    5: "flash error",
    6: "verify error",
    7: "version error",
    8: "signature invalid",
    9: "version below floor",
}

STATE_TEXT = {0: "EMPTY", 1: "TRIAL", 2: "CONFIRMED", 3: "BAD", 4: "DOWNLOADING"}

SLOT_A = 0
SLOT_B = 1
SLOT_NONE = 0xFF

SLOT_A_BASE = 0x0800C000
SLOT_A_SIZE = 0x34000
SLOT_B_BASE = 0x08040000
SLOT_B_SIZE = 0x40000
IMAGE_MAX = SLOT_A_SIZE

META_ADDR = 0x08008000
META_MAGIC = 0x41424946  # "ABIF"
REC_SIZE = 96

BAUDS = (115200,)  # only this rate is verified on the board


class ProtocolError(Exception):
    pass


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def slot_base(slot: int) -> int:
    return SLOT_B_BASE if slot == SLOT_B else SLOT_A_BASE


def slot_size(slot: int) -> int:
    return SLOT_B_SIZE if slot == SLOT_B else SLOT_A_SIZE


def slot_name(slot: int) -> str:
    return "B" if slot == SLOT_B else "A" if slot == SLOT_A else "-"


def linked_slot(image: bytes) -> Optional[int]:
    """Which slot was this image linked for?  Derived from the reset vector."""
    if len(image) < 8:
        return None
    reset = struct.unpack_from("<I", image, 4)[0] & ~1
    if SLOT_A_BASE <= reset < SLOT_A_BASE + SLOT_A_SIZE:
        return SLOT_A
    if SLOT_B_BASE <= reset < SLOT_B_BASE + SLOT_B_SIZE:
        return SLOT_B
    return None


def build_frame(ftype: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ProtocolError(f"payload too large: {len(payload)}")
    body = struct.pack("<BBHH", PROTO_VERSION, ftype, seq, len(payload)) + payload
    return SYNC + body + struct.pack("<I", crc32(body))


def parse_version(text: str) -> int:
    parts = [int(p) for p in text.strip().split(".")]
    while len(parts) < 4:
        parts.append(0)
    major, minor, patch, build = parts[:4]
    if not all(0 <= p <= 255 for p in (major, minor, patch, build)):
        raise ValueError("version parts must be 0..255")
    return (major << 24) | (minor << 16) | (patch << 8) | build


def format_version(value: int) -> str:
    parts = [(value >> shift) & 0xFF for shift in (24, 16, 8, 0)]
    if parts[3] == 0:
        parts = parts[:3]
    return ".".join(str(p) for p in parts)


def parse_hex(text: str) -> bytes:
    memory: dict[int, int] = {}
    upper = 0
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        raw = bytes.fromhex(line[1:])
        count = raw[0]
        address = (raw[1] << 8) | raw[2]
        rtype = raw[3]
        payload = raw[4 : 4 + count]
        if rtype == 0x00:
            for offset, value in enumerate(payload):
                memory[upper + address + offset] = value
        elif rtype == 0x01:
            break
        elif rtype == 0x02:
            upper = ((payload[0] << 8) | payload[1]) << 4
        elif rtype == 0x04:
            upper = ((payload[0] << 8) | payload[1]) << 16
        elif rtype in (0x03, 0x05):
            continue
        else:
            raise ValueError(f"unsupported hex record {rtype:#x}")
    if not memory:
        raise ValueError("hex file contains no data")
    start, end = min(memory), max(memory) + 1
    out = bytearray(b"\xFF" * (end - start))
    for addr, value in memory.items():
        out[addr - start] = value
    return bytes(out)


def load_image(path: str | Path) -> bytes:
    data = Path(path).read_bytes()
    if Path(path).suffix.lower() == ".hex" or data[:1] == b":":
        return parse_hex(data.decode("ascii", errors="ignore"))
    return data


def analyze_image(image: bytes) -> dict:
    if len(image) < 8:
        raise ValueError("image too small")
    sp, reset = struct.unpack_from("<II", image, 0)
    return {"sp": sp, "reset": reset, "slot": linked_slot(image), "size": len(image)}


def make_meta_record(image: bytes, slot: int, version: int, seq: int = 1) -> bytes:
    """Build one 96-byte CONFIRMED record (factory provisioning)."""
    rec = bytearray(REC_SIZE)
    struct.pack_into("<I", rec, 0, META_MAGIC)
    struct.pack_into("<H", rec, 4, REC_SIZE)
    rec[6] = slot
    rec[7] = 2  # CONFIRMED
    struct.pack_into("<I", rec, 8, seq)
    struct.pack_into("<I", rec, 12, version)
    struct.pack_into("<I", rec, 16, len(image))
    struct.pack_into("<I", rec, 20, crc32(image))
    struct.pack_into("<H", rec, 24, 0)
    struct.pack_into("<H", rec, 26, 0)
    rec[28:60] = hashlib.sha256(image).digest()
    struct.pack_into("<I", rec, 60, version)      # anti-rollback floor
    struct.pack_into("<I", rec, 64, 0)            # sec_flags: factory flash
    struct.pack_into("<I", rec, 92, crc32(bytes(rec[:92])))
    return bytes(rec)


class Link:
    """One serial connection speaking the bootloader protocol."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.05):
        import serial

        self.port = port
        self.baud = baud
        self.ser = serial.Serial(port, baud, timeout=timeout, write_timeout=2.0)
        self.seq = 0

    def close(self) -> None:
        try:
            self.ser.close()
        except Exception:
            pass

    # ---------------------------------------------------------------- frame IO
    def _read_byte(self, deadline: float) -> Optional[int]:
        while time.monotonic() < deadline:
            b = self.ser.read(1)
            if b:
                return b[0]
        return None

    def _read_exact(self, count: int, deadline: float) -> Optional[bytes]:
        buf = bytearray()
        while len(buf) < count:
            b = self.ser.read(count - len(buf))
            if b:
                buf += b
            elif time.monotonic() >= deadline:
                return None
        return bytes(buf)

    def _read_frame(self, deadline: float):
        while time.monotonic() < deadline:
            b = self._read_byte(deadline)
            if b is None:
                return None
            if b != 0xAA:
                continue
            b2 = self._read_byte(deadline)
            if b2 != 0x55:
                continue
            hdr = self._read_exact(6, deadline)
            if hdr is None:
                return None
            ver, ftype, seq, length = struct.unpack("<BBHH", hdr)
            if ver != PROTO_VERSION or length > MAX_PAYLOAD:
                continue
            body = self._read_exact(length + 4, deadline)
            if body is None:
                return None
            payload = body[:length]
            if crc32(hdr + payload) != struct.unpack("<I", body[length:])[0]:
                continue
            return ftype, seq, payload
        return None

    def request(self, ftype: int, payload: bytes = b"", timeout: float = 1.0, retries: int = 3) -> bytes:
        last = "timeout"
        for _ in range(retries):
            self.seq = (self.seq + 1) & 0xFFFF
            frame = build_frame(ftype, self.seq, payload)
            try:
                self.ser.reset_input_buffer()
                self.ser.write(frame)
                self.ser.flush()
            except Exception as exc:  # serial write failure
                last = str(exc)
                time.sleep(0.05)
                continue
            deadline = time.monotonic() + timeout
            while True:
                got = self._read_frame(deadline)
                if got is None:
                    break
                rtype, rseq, rpayload = got
                if rtype == (ftype | RSP) and rseq == self.seq:
                    if not rpayload:
                        raise ProtocolError("empty response")
                    if rpayload[0] != ST_OK:
                        raise ProtocolError(
                            STATUS_TEXT.get(rpayload[0], f"error {rpayload[0]}")
                        )
                    return rpayload
        raise ProtocolError(f"no response to 0x{ftype:02X}: {last}")

    # ---------------------------------------------------------------- commands
    def hello(self, timeout: float = 1.0) -> dict:
        p = self.request(CMD_HELLO, timeout=timeout)
        proto = p[1]
        boot_version = struct.unpack_from("<I", p, 2)[0]
        active, state, attempts = p[6], p[7], p[8]
        max_image, flags = struct.unpack_from("<II", p, 10)
        return {
            "proto": proto,
            "boot_version": boot_version,
            "active": active,
            "state": state,
            "max_attempts": attempts,
            "max_image": max_image,
            "flags": flags,
        }

    def status(self) -> dict:
        p = self.request(CMD_STATUS)
        active, state, count = p[1], p[2], p[3]
        slots = []
        for i in range(2):
            base = 4 + i * 12
            present, sstate, attempts, _pad, version, size = struct.unpack_from(
                "<BBBBII", p, base
            )
            slots.append(
                {
                    "present": bool(present),
                    "state": sstate,
                    "attempts": attempts,
                    "version": version,
                    "size": size,
                }
            )
        return {"active": active, "state": state, "count": count, "slots": slots}

    def start(
        self,
        version: int,
        size: int,
        crc: int,
        sha: bytes,
        slot: int,
        signature: bytes,
    ) -> tuple[int, int]:
        if len(sha) != 32 or len(signature) != SIG_BYTES:
            raise ValueError("START needs a 32-byte SHA-256 and a 64-byte signature")
        payload = (
            struct.pack("<B3xII", slot, version, size)
            + crc.to_bytes(4, "little")
            + sha
            + signature
        )
        p = self.request(CMD_START, payload, timeout=30.0, retries=2)
        return p[1], struct.unpack_from("<I", p, 2)[0]

    def info(self) -> dict:
        p = self.request(CMD_INFO, timeout=1.0)
        if p[0] != ST_OK:
            raise ProtocolError(f"INFO failed: {p[0]}")
        version = struct.unpack_from("<I", p, 8)[0]
        min_version = struct.unpack_from("<I", p, 12)[0]
        size = struct.unpack_from("<I", p, 16)[0]
        attempts = struct.unpack_from("<H", p, 20)[0]
        return {
            "protocol": p[1],
            "boot_version": struct.unpack_from("<I", p, 2)[0],
            "active": p[6],
            "state": p[7],
            "version": version,
            "min_version": min_version,
            "size": size,
            "attempts": attempts,
        }

    def data(self, offset: int, chunk: bytes, timeout: float = 1.5) -> int:
        p = self.request(CMD_DATA, struct.pack("<I", offset) + chunk, timeout=timeout)
        return struct.unpack_from("<I", p, 1)[0]

    def end(self, crc: int, sha: bytes) -> tuple[int, int]:
        p = self.request(CMD_END, crc.to_bytes(4, "little") + sha, timeout=4.0)
        return p[1], struct.unpack_from("<I", p, 2)[0]

    def confirm(self) -> None:
        self.request(CMD_CONFIRM)

    def reboot(self) -> None:
        try:
            self.request(CMD_REBOOT, timeout=0.5, retries=1)
        except ProtocolError:
            pass

    def abort(self) -> None:
        try:
            self.request(CMD_ABORT)
        except ProtocolError:
            pass

    def set_baud(self, baud: int) -> None:
        self.request(CMD_SET_BAUD, struct.pack("<I", baud))
        time.sleep(0.05)
        self.close()
        time.sleep(0.05)
        import serial

        self.ser = serial.Serial(self.port, baud, timeout=0.05, write_timeout=2.0)
        self.baud = baud


def manifest(version: int, size: int, crc: int, digest: bytes) -> bytes:
    """Bytes covered by the image signature: version|size|crc|sha256."""
    if len(digest) != 32:
        raise ValueError("digest must be 32 bytes")
    return struct.pack("<III", version, size, crc) + digest


def sign_message(seed: bytes, message: bytes) -> bytes:
    """Ed25519-sign an arbitrary message with a 32-byte seed (PyNaCl)."""
    from nacl.signing import SigningKey

    if len(seed) != 32:
        raise ValueError("signing key must be a 32-byte seed")
    return SigningKey(seed).sign(message).signature


def sign_digest(seed: bytes, digest: bytes) -> bytes:
    """Sign a 32-byte digest (kept for tests)."""
    if len(digest) != 32:
        raise ValueError("digest must be 32 bytes")
    return sign_message(seed, digest)


def upload(
    link: Link,
    image: bytes,
    slot: int,
    version: int,
    progress: Optional[Callable[[int, int], None]] = None,
    log: Optional[Callable[[str], None]] = None,
    signer: Optional[Callable[[bytes], bytes]] = None,
) -> dict:
    """Full START/DATA/END sequence.  Returns a summary dict."""
    digest = hashlib.sha256(image).digest()
    checksum = crc32(image)
    if signer is None:
        raise ProtocolError("image signing key required")
    signature = signer(manifest(version, len(image), checksum, digest))
    target, offset = link.start(version, len(image), checksum, digest, slot, signature)
    if offset > len(image):
        raise ProtocolError(f"device asked to resume at {offset} > {len(image)}")
    if offset:
        if log:
            log(f"  resuming at {offset}/{len(image)} bytes")
        if progress:
            progress(offset, len(image))
    sent = offset
    while sent < len(image):
        chunk = image[sent : sent + CHUNK_SIZE]
        nxt = link.data(sent, chunk)
        sent += len(chunk)
        if nxt != sent:
            raise ProtocolError(f"offset mismatch: device {nxt}, host {sent}")
        if progress:
            progress(sent, len(image))
        if log and sent % (CHUNK_SIZE * 8) == 0:
            log(f"  sent {sent}/{len(image)} bytes")
    slot_out, version_out = link.end(checksum, digest)
    return {
        "slot": slot_out,
        "version": version_out,
        "size": len(image),
        "crc32": checksum,
        "sha256": digest.hex(),
    }
