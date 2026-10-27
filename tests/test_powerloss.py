import powerloss_sim as S


def blank_sector() -> bytearray:
    return bytearray(b"\xFF" * S.META_SIZE)


def test_scan_blank_is_none():
    assert S.scan(bytes(blank_sector())) is None


def test_scan_returns_newest():
    sector = blank_sector()
    records = [
        S.build_record(1, 0, 2, 0, 0x00010001),
        S.build_record(2, 1, 1, 8192, 0x00010006),
        S.build_record(3, 1, 2, 8192, 0x00010006),
    ]
    for i, rec in enumerate(records):
        sector[i * S.REC_SIZE : (i + 1) * S.REC_SIZE] = rec

    assert S.scan(bytes(sector)) == (3, 2)


def test_scan_stops_at_torn_record():
    sector = blank_sector()
    good = S.build_record(1, 0, 2, 0, 0x00010001)
    torn = bytearray(S.build_record(2, 1, 1, 8192, 0x00010006))
    torn[10] ^= 0xFF  # corrupt seq, CRC no longer matches

    sector[: S.REC_SIZE] = good
    sector[S.REC_SIZE : 2 * S.REC_SIZE] = torn

    assert S.scan(bytes(sector)) == (1, 2)


def test_record_key():
    rec = S.build_record(7, 1, 1, 4096, 0x00010002, attempts=2)
    assert S.record_key(rec) == (7, 1)
