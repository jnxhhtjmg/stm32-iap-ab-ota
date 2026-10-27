import struct

import pytest

import iap_proto as P


def test_frame_layout():
    frame = P.build_frame(P.CMD_HELLO, 1, b"")
    assert frame[:2] == b"\xAA\x55"
    assert frame[2] == P.PROTO_VERSION
    assert frame[3] == P.CMD_HELLO
    assert struct.unpack_from("<H", frame, 4)[0] == 1
    assert struct.unpack_from("<H", frame, 6)[0] == 0
    assert len(frame) == 12
    crc = struct.unpack_from("<I", frame, 8)[0]
    assert crc == P.crc32(frame[2:8])


def test_crc_known_vector():
    assert P.crc32(b"123456789") == 0xCBF43926


def test_payload_roundtrip():
    payload = bytes(range(256)) * 2
    frame = P.build_frame(P.CMD_DATA, 0x1234, payload)
    assert struct.unpack_from("<H", frame, 6)[0] == len(payload)
    assert frame[8 : 8 + len(payload)] == payload
    crc = struct.unpack_from("<I", frame, 8 + len(payload))[0]
    assert crc == P.crc32(frame[2 : 8 + len(payload)])


def test_payload_limit():
    with pytest.raises(P.ProtocolError):
        P.build_frame(P.CMD_DATA, 1, b"\x00" * (P.MAX_PAYLOAD + 1))


def test_version_codec():
    for text in ("1.0.1", "1.0.6", "2.3.4"):
        value = P.parse_version(text)
        assert P.format_version(value) == text
