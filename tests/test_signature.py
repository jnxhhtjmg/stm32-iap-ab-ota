import hashlib

import pytest

nacl_signing = pytest.importorskip("nacl.signing")
from nacl.exceptions import BadSignatureError

import iap_proto as P


def test_sign_digest_roundtrip():
    signing_key = nacl_signing.SigningKey.generate()
    seed = bytes(signing_key)
    digest = hashlib.sha256(b"firmware-image").digest()

    signature = P.sign_digest(seed, digest)

    assert len(signature) == P.SIG_BYTES
    signing_key.verify_key.verify(digest, signature)


def test_tampered_digest_rejected():
    signing_key = nacl_signing.SigningKey.generate()
    seed = bytes(signing_key)
    digest = hashlib.sha256(b"firmware-image").digest()
    signature = P.sign_digest(seed, digest)

    bad = bytearray(digest)
    bad[0] ^= 0xFF

    with pytest.raises(BadSignatureError):
        signing_key.verify_key.verify(bytes(bad), signature)


def test_signing_key_must_be_32_bytes():
    with pytest.raises(ValueError):
        P.sign_digest(b"\x00" * 31, b"\x00" * 32)


def test_manifest_covers_version():
    signing_key = nacl_signing.SigningKey.generate()
    seed = bytes(signing_key)
    digest = hashlib.sha256(b"image").digest()

    msg_v1 = P.manifest(P.parse_version("1.0.7"), 8144, 0x11223344, digest)
    msg_v2 = P.manifest(P.parse_version("1.0.8"), 8144, 0x11223344, digest)
    signature = P.sign_message(seed, msg_v1)

    signing_key.verify_key.verify(msg_v1, signature)
    with pytest.raises(BadSignatureError):
        signing_key.verify_key.verify(msg_v2, signature)
