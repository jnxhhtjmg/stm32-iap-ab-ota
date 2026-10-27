"""Exhaustive power-loss simulation for the metadata log.

The bootloader writes 96-byte records into one 16 KB sector and never erases
during normal operation.  A power loss can therefore land on any byte of a
record write.  This script emulates the sector and, for every possible cut
position, checks that the scan still returns the previous valid record.

Run:
    python tools/powerloss_sim.py
"""
from __future__ import annotations

import struct
import zlib

META_SIZE = 0x4000
REC_SIZE = 96
MAGIC = 0x41424946
ERASED = b"\xFF"


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_record(seq: int, slot: int, state: int, size: int, version: int, attempts: int = 0) -> bytes:
    rec = bytearray(REC_SIZE)
    struct.pack_into("<I", rec, 0, MAGIC)
    struct.pack_into("<H", rec, 4, REC_SIZE)
    rec[6] = slot
    rec[7] = state
    struct.pack_into("<I", rec, 8, seq)
    struct.pack_into("<I", rec, 12, version)
    struct.pack_into("<I", rec, 16, size)
    struct.pack_into("<I", rec, 20, 0x12345678 + seq)
    struct.pack_into("<H", rec, 24, attempts)
    struct.pack_into("<H", rec, 26, 0)
    rec[28:60] = bytes([seq & 0xFF]) * 32
    struct.pack_into("<I", rec, 60, version)       # min_version
    struct.pack_into("<I", rec, 64, 0)
    struct.pack_into("<I", rec, 92, crc32(bytes(rec[:92])))
    return bytes(rec)


def scan(sector: bytes) -> tuple[int, int] | None:
    """Return (seq, state) of the newest valid record, or None."""
    off = 0
    latest = None
    while off + REC_SIZE <= len(sector):
        rec = sector[off : off + REC_SIZE]
        if rec[:4] == ERASED * 4:
            break
        magic, rec_size = struct.unpack_from("<IH", rec, 0)
        crc = struct.unpack_from("<I", rec, 92)[0]
        if magic != MAGIC or rec_size != REC_SIZE or crc != crc32(rec[:92]):
            break
        latest = (struct.unpack_from("<I", rec, 8)[0], rec[7])
        off += REC_SIZE
    return latest


def record_key(rec: bytes) -> tuple[int, int]:
    return struct.unpack_from("<I", rec, 8)[0], rec[7]


def main() -> int:
    records = [
        build_record(1, 0, 2, 0, 0x00010001),
        build_record(2, 1, 1, 4384, 0x00010002),
        build_record(3, 1, 2, 4384, 0x00010002),
        build_record(4, 0, 2, 4384, 0x00010003),
    ]

    sector = bytearray(ERASED * META_SIZE)
    failures = 0
    cuts = 0

    for index, rec in enumerate(records):
        base = index * REC_SIZE
        previous = record_key(records[index - 1]) if index > 0 else None
        current = record_key(rec)
        for cut in range(REC_SIZE + 1):
            trial = bytearray(sector)
            trial[base : base + cut] = rec[:cut]
            got = scan(bytes(trial))
            # The record CRC sits at offset 92..95 and covers offsets 0..91.
            # A record is valid only once its CRC is fully written, so every
            # cut before 96 bytes must fall back to the previous record.
            expected = current if cut >= REC_SIZE else previous
            if got != expected:
                failures += 1
                print(f"  FAIL record {index + 1} cut {cut}: got {got}, expected {expected}")
            cuts += 1
        sector[base : base + REC_SIZE] = rec
        assert scan(bytes(sector)) == current

    # Sector full: the 171st append needs a compaction, which is out of scope
    # for this simulation but is logged as a known window in the design doc.
    print(f"records: {len(records)}, cut points: {cuts}, failures: {failures}")
    print("PASS" if failures == 0 else "FAILED")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
