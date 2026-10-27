"""Compute or verify SHA-256 sums of release artifacts.

Usage:
    python tools/verify_release.py --write
    python tools/verify_release.py --check
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = [
    ROOT / "release" / "firmware",
    ROOT / "release" / "tools",
    ROOT / "release" / "keys",
    ROOT / "release",
]
SUMS = ROOT / "release" / "SHA256SUMS"
EXTS = {".hex", ".bin", ".py", ".exe", ".ps1", ".txt"}


def collect() -> list[Path]:
    files: set[Path] = set()
    for target in TARGETS:
        if target.is_dir():
            for path in target.iterdir():
                if path.is_file() and path.suffix.lower() in EXTS:
                    files.add(path)
        elif target.is_file():
            files.add(target)
    return sorted(files)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def write() -> int:
    lines = []
    for path in collect():
        lines.append(f"{digest(path)}  {path.relative_to(ROOT).as_posix()}")
    SUMS.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {SUMS} ({len(lines)} files)")
    return 0


def check() -> int:
    if not SUMS.exists():
        print("SHA256SUMS missing; run --write first")
        return 1
    failures = 0
    for line in SUMS.read_text(encoding="utf-8").splitlines():
        expected, rel = line.split("  ", 1)
        path = ROOT / rel
        if not path.exists():
            print(f"MISSING {rel}")
            failures += 1
            continue
        actual = digest(path)
        if actual != expected:
            print(f"FAIL    {rel}\n  expected {expected}\n  actual   {actual}")
            failures += 1
        else:
            print(f"OK      {rel}")
    print(f"{failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true")
    group.add_argument("--check", action="store_true")
    args = parser.parse_args()
    return write() if args.write else check()


if __name__ == "__main__":
    sys.exit(main())
