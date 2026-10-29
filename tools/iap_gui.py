#!/usr/bin/env python3
"""Tkinter GUI for the A/B IAP bootloader.

Run:
    python tools/iap_gui.py

Workflow:
    1. pick the COM port, click 连接
    2. click 刷新状态 to see active slot / versions
    3. choose the .hex built for the *other* slot, click 开始升级
    4. device reboots, trial image confirms itself after the self-test
"""
from __future__ import annotations

import queue
import sys
import threading
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import iap_proto as P

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    serial = None
    list_ports = None


class App:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.link: P.Link | None = None
        self.busy = False
        self.q: queue.Queue = queue.Queue()
        self.image: bytes | None = None
        self.image_path = ""

        root.title("A/B IAP 升级工具")
        root.geometry("820x600")
        root.minsize(760, 540)

        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="115200")
        self.file_var = tk.StringVar()
        self.version_var = tk.StringVar(value="1.0.1")
        self.auto_reboot = tk.BooleanVar(value=True)
        self.status_var = tk.StringVar(value="未连接")
        self.target_var = tk.StringVar(value="-")

        self._build()
        self.refresh_ports()
        self.root.after(100, self._poll)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------------------------------------------------------------- layout
    def _build(self) -> None:
        pad = {"padx": 6, "pady": 4}

        top = ttk.LabelFrame(self.root, text="连接")
        top.pack(fill="x", padx=10, pady=(10, 4))

        ttk.Label(top, text="串口").grid(row=0, column=0, **pad)
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=12, state="readonly")
        self.port_box.grid(row=0, column=1, **pad)
        ttk.Button(top, text="刷新", command=self.refresh_ports).grid(row=0, column=2, **pad)
        ttk.Label(top, text="波特率").grid(row=0, column=3, **pad)
        ttk.Combobox(
            top, textvariable=self.baud_var, width=10, state="readonly",
            values=[str(b) for b in P.BAUDS],
        ).grid(row=0, column=4, **pad)
        self.btn_connect = ttk.Button(top, text="连接", command=self.toggle_connect)
        self.btn_connect.grid(row=0, column=5, **pad)
        ttk.Button(top, text="应用→升级模式", command=self.enter_bootloader).grid(
            row=0, column=6, **pad
        )
        ttk.Label(top, textvariable=self.status_var, foreground="#1a5").grid(
            row=0, column=7, padx=10
        )
        top.columnconfigure(7, weight=1)

        dev = ttk.LabelFrame(self.root, text="设备状态")
        dev.pack(fill="x", padx=10, pady=4)
        self.slot_a_var = tk.StringVar(value="slot A: -")
        self.slot_b_var = tk.StringVar(value="slot B: -")
        self.boot_var = tk.StringVar(value="bootloader: -")
        ttk.Label(dev, textvariable=self.boot_var).grid(row=0, column=0, sticky="w", **pad)
        ttk.Label(dev, textvariable=self.slot_a_var).grid(row=1, column=0, sticky="w", **pad)
        ttk.Label(dev, textvariable=self.slot_b_var).grid(row=2, column=0, sticky="w", **pad)
        ttk.Button(dev, text="刷新状态", command=self.refresh_status).grid(row=0, column=1, **pad)
        ttk.Button(dev, text="确认当前固件", command=self.confirm).grid(row=1, column=1, **pad)
        ttk.Button(dev, text="重启设备", command=self.reboot).grid(row=2, column=1, **pad)

        up = ttk.LabelFrame(self.root, text="升级")
        up.pack(fill="x", padx=10, pady=4)
        ttk.Label(up, text="镜像").grid(row=0, column=0, **pad)
        ttk.Entry(up, textvariable=self.file_var).grid(row=0, column=1, sticky="ew", **pad)
        ttk.Button(up, text="浏览", command=self.choose_file).grid(row=0, column=2, **pad)
        ttk.Label(up, text="版本").grid(row=1, column=0, **pad)
        ttk.Entry(up, textvariable=self.version_var, width=12).grid(row=1, column=1, sticky="w", **pad)
        ttk.Label(up, text="目标槽").grid(row=1, column=2, sticky="e", **pad)
        ttk.Label(up, textvariable=self.target_var, foreground="#06c").grid(
            row=1, column=3, sticky="w", **pad
        )
        ttk.Checkbutton(up, text="升级后自动重启", variable=self.auto_reboot).grid(
            row=1, column=4, **pad
        )
        self.btn_flash = ttk.Button(up, text="开始升级", command=self.start_flash)
        self.btn_flash.grid(row=1, column=5, **pad)
        up.columnconfigure(1, weight=1)

        self.progress = ttk.Progressbar(up, maximum=100)
        self.progress.grid(row=2, column=0, columnspan=6, sticky="ew", padx=6, pady=4)
        self.progress_var = tk.StringVar(value="")
        ttk.Label(up, textvariable=self.progress_var).grid(row=3, column=0, columnspan=6, **pad)

        logf = ttk.LabelFrame(self.root, text="日志")
        logf.pack(fill="both", expand=True, padx=10, pady=(4, 10))
        self.log_text = tk.Text(logf, height=12, wrap="none")
        self.log_text.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(logf, command=self.log_text.yview)
        scroll.pack(side="right", fill="y")
        self.log_text.configure(yscrollcommand=scroll.set, state="disabled")

        self.actions = [
            self.btn_connect, self.btn_flash,
        ]

    # ---------------------------------------------------------------- helpers
    def _log(self, msg: str) -> None:
        self.q.put(("log", msg))

    def _write_log(self, msg: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", time.strftime("[%H:%M:%S] ") + msg + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _poll(self) -> None:
        try:
            while True:
                try:
                    kind, payload = self.q.get_nowait()
                except queue.Empty:
                    break
                try:
                    if kind == "log":
                        self._write_log(payload)
                    elif kind == "progress":
                        sent, total = payload
                        pct = 100.0 * sent / max(total, 1)
                        self.progress["value"] = pct
                        self.progress_var.set(f"{sent}/{total} bytes  {pct:.0f}%")
                    elif kind == "status":
                        self._show_status(*payload)
                    elif kind == "connected":
                        self.btn_connect.configure(text="断开")
                        self.status_var.set("已连接")
                    elif kind == "disconnected":
                        self.btn_connect.configure(text="连接")
                        self.status_var.set("未连接")
                    elif kind == "busy":
                        self._set_busy(payload)
                    elif kind == "error":
                        # Keep it non-modal: a hidden dialog made the app look
                        # frozen / closed when the error happened in the worker.
                        self._write_log(f"错误: {payload}")
                        self.status_var.set("出错，详见日志")
                        self.root.bell()
                    elif kind == "info":
                        messagebox.showinfo("提示", payload)
                except Exception as exc:  # never let one bad message kill the UI
                    self._write_log(f"界面异常: {exc}")
        finally:
            self.root.after(100, self._poll)

    def _set_busy(self, busy: bool) -> None:
        self.busy = busy
        state = "disabled" if busy else "normal"
        for widget in self.actions:
            widget.configure(state=state)

    def _run(self, fn) -> None:
        if self.busy:
            return
        self.q.put(("busy", True))

        def wrapper() -> None:
            try:
                fn()
            except Exception as exc:  # noqa: BLE001
                if isinstance(exc, OSError):   # pyserial raises this on unplug
                    if self.link is not None:
                        try:
                            self.link.close()
                        except Exception:
                            pass
                        self.link = None
                        self.q.put(("disconnected", None))
                    self.q.put(("error",
                                "串口已断开（设备断电或 USB 被拔）。\n"
                                "给板子上电后，点『连接』重试。"))
                else:
                    self.q.put(("error", str(exc)))
            finally:
                self.q.put(("busy", False))

        threading.Thread(target=wrapper, daemon=True).start()

    def _show_status(self, st: dict, hello: dict) -> None:
        self.boot_var.set(
            f"bootloader: protocol {hello['proto']}  build {P.format_version(hello['boot_version'])}"
            f"   active={P.slot_name(st['active'])}  state={P.STATE_TEXT.get(st['state'], '?')}"
        )
        for slot, var in ((P.SLOT_A, self.slot_a_var), (P.SLOT_B, self.slot_b_var)):
            info = st["slots"][slot]
            if info["present"]:
                size_txt = f"{info['size']} bytes" if info["size"] else "size unknown"
                var.set(
                    f"slot {P.slot_name(slot)}: v{P.format_version(info['version'])}  "
                    f"{size_txt}  {P.STATE_TEXT.get(info['state'], '?')}  "
                    f"attempts={info['attempts']}"
                )
            else:
                var.set(f"slot {P.slot_name(slot)}: 空")

    # ---------------------------------------------------------------- actions
    def refresh_ports(self) -> None:
        ports = []
        if list_ports is not None:
            ports = [p.device for p in list_ports.comports()]
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])
        if not ports:
            self._write_log("没有检测到串口，请插上 CH340 / USB 线")

    def choose_file(self) -> None:
        path = filedialog.askopenfilename(
            title="选择固件",
            filetypes=[("Firmware", "*.hex *.bin"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            image = P.load_image(path)
            info = P.analyze_image(image)
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("错误", f"读不了这个文件：{exc}")
            return
        self.image = image
        self.image_path = path
        self.file_var.set(path)
        slot = info["slot"]
        self.target_var.set(f"slot {P.slot_name(slot)}" if slot is not None else "未知")
        self._write_log(
            f"选择 {Path(path).name}: {info['size']} bytes, "
            f"link slot {P.slot_name(slot) if slot is not None else '?'}, "
            f"SP={info['sp']:#010x}, reset={info['reset']:#010x}"
        )

    def toggle_connect(self) -> None:
        if self.link is not None:
            self.link.close()
            self.link = None
            self.q.put(("disconnected", None))
            return
        if not self.port_var.get():
            messagebox.showwarning("提示", "请先选择串口")
            return
        self._run(self._do_connect)

    def _do_connect(self) -> None:
        port = self.port_var.get()
        baud = int(self.baud_var.get())
        self._log(f"打开 {port} @ 115200")
        link = P.Link(port, 115200)

        hello = None
        for attempt in range(3):
            # The application redraws the OLED in a blocking ~50 ms loop, so a
            # single "boot" line can be missed.  Keep sending it for 2.5 s.
            self._log("已发送 boot，等待应用复位 ...")
            for _ in range(10):
                try:
                    link.ser.write(b"\r\nboot\r\n")
                    link.ser.flush()
                except Exception:
                    pass
                time.sleep(0.25)
            try:
                hello = link.hello(timeout=2.0)
                break
            except P.ProtocolError:
                hello = None

        if hello is None:
            link.close()
            raise RuntimeError(
                "设备没响应。\n\n"
                "1) 按一下板子复位键再点连接\n"
                "2) 确认 BOOT0=0、CH340 接在 PA9/PA10\n"
                "3) 串口没被其他软件占用"
            )

        if baud != 115200:
            self._log(f"切换到 {baud} 波特率")
            link.set_baud(baud)
            hello = link.hello(timeout=2.0)
        self.link = link
        self.q.put(("connected", None))
        self._log("已连接")
        self._log("提示：bootloader 不驱动 OLED，屏幕会冻结；点『重启设备』恢复动画")
        st = link.status()
        self.q.put(("status", (st, hello)))

    def enter_bootloader(self) -> None:
        port = self.port_var.get()
        if not port:
            messagebox.showwarning("提示", "请先选择串口")
            return

        def work() -> None:
            if self.link is not None:
                self.link.close()
                self.link = None
                self.q.put(("disconnected", None))
            self._log("向应用发送 boot 命令 ...")
            with serial.Serial(port, 115200, timeout=0.2) as ser:
                ser.write(b"boot\r\n")
                ser.flush()
            time.sleep(1.5)
            self._log("应用应已复位到 bootloader，现在点『连接』")

        self._run(work)

    def refresh_status(self) -> None:
        if self.link is None:
            messagebox.showinfo("提示", "请先连接")
            return

        def work() -> None:
            assert self.link is not None
            try:
                st = self.link.status()
            except P.ProtocolError:
                self._log("设备已回到应用，正在切回 bootloader ...")
                self.link.ser.write(b"\r\nboot\r\n")
                self.link.ser.flush()
                time.sleep(2.5)
                st = self.link.status()
            hello = self.link.hello()
            self.q.put(("status", (st, hello)))
            self._log(
                f"状态: active={P.slot_name(st['active'])} "
                f"state={P.STATE_TEXT.get(st['state'], '?')}"
            )

        self._run(work)

    def confirm(self) -> None:
        if self.link is None:
            messagebox.showinfo("提示", "请先连接")
            return

        def work() -> None:
            assert self.link is not None
            self.link.confirm()
            self._log("已确认当前试用固件")
            self.q.put(("status", (self.link.status(), self.link.hello())))

        self._run(work)

    def reboot(self) -> None:
        if self.link is None:
            messagebox.showinfo("提示", "请先连接")
            return

        def work() -> None:
            assert self.link is not None
            self.link.reboot()
            self._log("已发送重启命令")

        self._run(work)

    def start_flash(self) -> None:
        if self.link is None:
            messagebox.showinfo("提示", "请先连接设备")
            return
        if self.image is None:
            messagebox.showinfo("提示", "请先选择固件")
            return
        try:
            version = P.parse_version(self.version_var.get())
        except ValueError as exc:
            messagebox.showerror("错误", f"版本号格式不对：{exc}")
            return

        image = self.image
        slot = P.analyze_image(image)["slot"]
        if slot is None:
            messagebox.showerror("错误", "这个镜像的复位向量不在 A/B 槽范围内")
            return

        def work() -> None:
            assert self.link is not None
            hello = self.link.hello()
            active = hello["active"]
            if slot == active:
                raise RuntimeError(
                    f"这个文件链接到 slot {P.slot_name(slot)}，正是当前运行槽，不能覆盖。\n"
                    "请使用另一个槽的镜像。"
                )
            self._log(
                f"升级 slot {P.slot_name(slot)}，版本 {self.version_var.get()}，"
                f"{len(image)} bytes（当前运行 slot {P.slot_name(active)}）"
            )
            started = time.monotonic()
            last = [0]

            def progress(sent: int, total: int) -> None:
                if sent - last[0] >= P.CHUNK_SIZE * 4 or sent == total:
                    last[0] = sent
                    self.q.put(("progress", (sent, total)))

            key_path = Path("keys/demo_signing.key")
            if not key_path.exists():
                raise RuntimeError(
                    f"缺少签名私钥 {key_path}，先用 tools/gen_key.py 生成一对密钥"
                )
            seed = bytes.fromhex(key_path.read_text(encoding="utf-8").strip())
            summary = P.upload(
                self.link,
                image,
                slot,
                version,
                progress=progress,
                log=self._log,
                signer=lambda digest: P.sign_message(seed, digest),
            )
            elapsed = time.monotonic() - started
            self._log(
                f"完成: slot {P.slot_name(summary['slot'])} "
                f"v{P.format_version(summary['version'])} {elapsed:.1f}s "
                f"({len(image) / max(elapsed, 0.001) / 1024:.1f} KB/s)"
            )
            self.q.put(("progress", (len(image), len(image))))
            if self.auto_reboot.get():
                self._log("重启进入试用固件 ...")
                self.link.reboot()
                time.sleep(0.2)
                self.link.close()
                self.link = None
                self.q.put(("disconnected", None))
                self._log("设备已重启，点『连接』继续")

        self._run(work)

    def _on_close(self) -> None:
        if self.link is not None:
            self.link.close()
        self.root.destroy()


def main() -> int:
    if serial is None:
        print("pyserial is required: python -m pip install pyserial")
        return 1
    root = tk.Tk()
    App(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
