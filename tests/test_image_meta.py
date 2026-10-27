import struct

import iap_proto as P


def make_image(base: int, size: int = 256) -> bytes:
    image = bytearray(b"\xFF" * size)
    struct.pack_into("<I", image, 0, 0x20000880)      # initial SP
    struct.pack_into("<I", image, 4, base + 0x19D)    # reset, Thumb bit set
    for i in range(8, size):
        image[i] = i & 0xFF
    return bytes(image)


def test_linked_slot_a():
    assert P.linked_slot(make_image(P.SLOT_A_BASE)) == P.SLOT_A


def test_linked_slot_b():
    assert P.linked_slot(make_image(P.SLOT_B_BASE)) == P.SLOT_B


def test_linked_slot_unknown():
    image = bytearray(b"\xFF" * 64)
    struct.pack_into("<I", image, 4, 0x08000000)
    assert P.linked_slot(bytes(image)) is None


def test_analyze_image():
    image = make_image(P.SLOT_A_BASE)
    info = P.analyze_image(image)
    assert info["sp"] == 0x20000880
    assert info["slot"] == P.SLOT_A
    assert info["size"] == len(image)


def test_meta_record_layout():
    image = make_image(P.SLOT_A_BASE)
    rec = P.make_meta_record(image, P.SLOT_A, P.parse_version("1.0.1"), seq=1)

    assert len(rec) == P.REC_SIZE
    assert struct.unpack_from("<I", rec, 0)[0] == P.META_MAGIC
    assert struct.unpack_from("<H", rec, 4)[0] == P.REC_SIZE
    assert rec[6] == P.SLOT_A
    assert rec[7] == 2  # CONFIRMED
    assert struct.unpack_from("<I", rec, 16)[0] == len(image)
    assert struct.unpack_from("<I", rec, 20)[0] == P.crc32(image)
    assert rec[28:60] == __import__("hashlib").sha256(image).digest()
    assert struct.unpack_from("<I", rec, 60)[0] == P.parse_version("1.0.1")
    assert struct.unpack_from("<I", rec, 92)[0] == P.crc32(rec[:92])
