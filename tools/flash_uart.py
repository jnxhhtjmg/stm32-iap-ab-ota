"""Flash an STM32 via the built-in UART bootloader (AN3155).

Usage:
    python tools/flash_uart.py --port COM5 --hex MDK-ARM/f407_gesture_ai/f407_gesture_ai.hex

Before running:
    1. Set BOOT0 = 1 (3.3V), BOOT1 = 0 (GND).
    2. Press the board reset button.
    3. Run this script.
    4. After it finishes, set BOOT0 = 0 and press reset again.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: python -m pip install pyserial")

ACK = 0x79
NACK = 0x1F
FLASH_BASE = 0x08000000
FLASH_END = 0x08080000  # 512 KB for STM32F407VE


def parse_hex(path: Path) -> tuple[int, bytearray]:
    memory: dict[int, int] = {}
    upper = 0
    with path.open("r", encoding="ascii") as handle:
        for line in handle:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            if len(raw) < 5:
                raise ValueError(f"bad hex record: {line}")
            count = raw[0]
            address = (raw[1] << 8) | raw[2]
            record_type = raw[3]
            payload = raw[4 : 4 + count]
            if record_type == 0x00:
                for offset, value in enumerate(payload):
                    memory[upper + address + offset] = value
            elif record_type == 0x01:
                break
            elif record_type == 0x02:
                upper = ((payload[0] << 8) | payload[1]) << 4
            elif record_type == 0x04:
                upper = ((payload[0] << 8) | payload[1]) << 16
            elif record_type in (0x03, 0x05):
                continue
            else:
                raise ValueError(f"unsupported hex record type {record_type:#x}")

    if not memory:
        raise ValueError("hex file contains no data")

    start = min(memory)
    end = max(memory) + 1
    if start < FLASH_BASE or end > FLASH_END:
        raise ValueError(
            f"data range {start:#010x}-{end:#010x} is outside "
            f"{FLASH_BASE:#010x}-{FLASH_END:#010x}"
        )

    image = bytearray(b"\xff" * (end - start))
    for address, value in memory.items():
        image[address - start] = value
    return start, image


def xor_bytes(values: bytes) -> int:
    result = 0
    for value in values:
        result ^= value
    return result


class Bootloader:
    def __init__(self, port: str, baud: int, parity: str, timeout: float) -> None:
        parity_map = {"even": serial.PARITY_EVEN, "none": serial.PARITY_NONE}
        self.ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=parity_map[parity],
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
            write_timeout=2,
        )
        self.ser.reset_input_buffer()

    def close(self) -> None:
        self.ser.close()

    def read_ack(self, timeout: float | None = None) -> bool:
        if timeout is not None:
            self.ser.timeout = timeout
        value = self.ser.read(1)
        if timeout is not None:
            self.ser.timeout = 1.0
        return len(value) == 1 and value[0] == ACK

    def command(self, code: int) -> None:
        self.ser.write(bytes([code]))
        if not self.read_ack():
            raise RuntimeError(f"command {code:#x} was not acknowledged")

    def sync(self) -> None:
        self.ser.reset_input_buffer()
        self.ser.write(b"\x7f")
        if not self.read_ack(timeout=3.0):
            raise RuntimeError("bootloader did not answer 0x7F (check BOOT0 and reset)")

    def erase_all(self) -> None:
        self.command(0x43)
        self.ser.write(b"\xff\x00")
        if not self.read_ack(timeout=20.0):
            raise RuntimeError("mass erase was not acknowledged")

    def write_chunk(self, address: int, data: bytes) -> None:
        self.command(0x31)
        address_bytes = address.to_bytes(4, "big")
        self.ser.write(address_bytes + bytes([xor_bytes(address_bytes)]))
        if not self.read_ack():
            raise RuntimeError(f"address {address:#010x} was not acknowledged")
        packet = bytes([len(data) - 1]) + data
        self.ser.write(packet + bytes([xor_bytes(packet)]))
        if not self.read_ack(timeout=4.0):
            raise RuntimeError(f"data write at {address:#010x} was not acknowledged")

    def go(self, address: int) -> None:
        self.command(0x21)
        address_bytes = address.to_bytes(4, "big")
        self.ser.write(address_bytes + bytes([xor_bytes(address_bytes)]))
        self.read_ack(timeout=2.0)


def flash(port: str, baud: int, hex_path: Path, parity: str) -> None:
    start, image = parse_hex(hex_path)
    # Align to 4 bytes and pad the tail; the F4 bootloader writes words.
    pad_start = start % 4
    if pad_start:
        image = bytearray(b"\xff" * (4 - pad_start)) + image
        start -= 4 - pad_start
    if len(image) % 4:
        image.extend(b"\xff" * (4 - len(image) % 4))

    print(f"image: {hex_path}")
    print(f"range: {start:#010x} - {start + len(image):#010x} ({len(image)} bytes)")
    print(f"port : {port} @ {baud} 8{parity[0].upper()}1")

    link = Bootloader(port, baud, parity, timeout=1.0)
    try:
        print("sync ...", end="", flush=True)
        try:
            link.sync()
        except RuntimeError:
            if parity == "even":
                print(" retry 8N1 ...", end="", flush=True)
                link.close()
                link = Bootloader(port, baud, "none", timeout=1.0)
                link.sync()
                parity = "none"
            else:
                raise
        print(" ok")

        print("erase ...", end="", flush=True)
        link.erase_all()
        print(" ok")

        total = len(image)
        written = 0
        chunk_size = 256
        t0 = time.time()
        for offset in range(0, total, chunk_size):
            chunk = bytes(image[offset : offset + chunk_size])
            link.write_chunk(start + offset, chunk)
            written += len(chunk)
            percent = written * 100 // total
            print(f"\rwrite {written}/{total} ({percent}%)", end="", flush=True)
        elapsed = time.time() - t0
        print(f"\rwrite {written}/{total} (100%)  {elapsed:.1f}s")

        print("go ...", end="", flush=True)
        link.go(FLASH_BASE)
        print(" ok")
        print("done. Set BOOT0=0 and press reset.")
    finally:
        link.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--hex",
        type=Path,
        default=Path("MDK-ARM/f407_gesture_ai/f407_gesture_ai.hex"),
    )
    parser.add_argument("--parity", choices=("even", "none"), default="even")
    parser.add_argument("--dry-run", action="store_true", help="Only parse the hex file")
    args = parser.parse_args()

    if args.dry_run:
        start, image = parse_hex(args.hex)
        print(f"range: {start:#010x} - {start + len(image):#010x} ({len(image)} bytes)")
        return

    flash(args.port, args.baud, args.hex, args.parity)


if __name__ == "__main__":
    main()
