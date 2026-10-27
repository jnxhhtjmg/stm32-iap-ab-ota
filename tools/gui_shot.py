"""Render the Tk GUI to a PNG without needing it on the visible desktop.

Uses PrintWindow(PW_RENDERFULLCONTENT) so the window can be captured even when
it is behind other windows or on another virtual desktop.
"""
from __future__ import annotations

import ctypes
import sys
import tkinter as tk
from ctypes import wintypes
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import iap_gui  # noqa: E402
import iap_proto as P  # noqa: E402

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


def capture(hwnd: int, path: Path) -> None:
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w, h = rect.right - rect.left, rect.bottom - rect.top

    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 2)  # PW_RENDERFULLCONTENT

    bmi = BITMAPINFOHEADER()
    bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.biWidth = w
    bmi.biHeight = -h
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    bmi.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)

    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)
    img.convert("RGB").save(path)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)


def main() -> int:
    root = tk.Tk()
    app = iap_gui.App(root)
    root.geometry("900x660+40+40")

    # Fill the panels with a real exchange so the screenshot is meaningful.
    try:
        link = P.Link("COM6", 115200)
        hello = link.hello(timeout=1.0)
        st = link.status()
        app._show_status(st, hello)
        link.close()
    except Exception as exc:  # noqa: BLE001
        app._write_log(f"设备未连接：{exc}")
    app._write_log("connected COM6 @ 115200")
    app._write_log("升级 slot B，版本 1.0.4，4280 bytes（当前运行 slot A）")
    app._write_log("完成: slot B v1.0.4.0 1.5s (2.8 KB/s)")
    app._write_log("重启进入试用固件 ...")
    app.progress["value"] = 100
    app.progress_var.set("4280/4280 bytes  100%")
    app.status_var.set("已连接")
    app.btn_connect.configure(text="断开")
    app.file_var.set(r"app\MDK\app_slot_b\app_slot_b.hex")
    app.target_var.set("slot B")
    app.version_var.set("1.0.4")

    root.update_idletasks()
    root.update()
    hwnd = int(root.wm_frame(), 16)
    capture(hwnd, Path("docs/gui.png"))
    root.destroy()
    print("saved docs/gui.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
