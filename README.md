# AB-IAP · STM32F407 A/B 双区串口升级与试运行回滚

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![MCU](https://img.shields.io/badge/MCU-STM32F407VE-blue.svg)]()
[![Protocol](https://img.shields.io/badge/protocol-v1-green.svg)](docs/PROTOCOL.md)
[![Python](https://img.shields.io/badge/python-3.10%2B-3776AB.svg)]()
[![ci](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/actions/workflows/ci.yml/badge.svg)](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/jnxhhtjmg/stm32-iap-ab-ota)](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases)

一个跑在 **STM32F407VE** 上的 direct-xip A/B 双区 IAP：
两个槽各自链接、原地运行，升级只写非活动槽，切换只改 96 字节元数据；
新固件先试运行，**3 次未确认自动回滚**；升级中途断电，旧槽一个字节不动。

> An open-source A/B dual-bank serial IAP for STM32F407 with trial-run rollback.
> Two independently linked slots, append-only 96-byte metadata, crash-safe by design.

---

## 下载

预编译固件与校验文件见 [v1.2.1 Release](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases/tag/v1.2.1)：

| 文件 | 用途 |
| --- | --- |
| [f407_iap_boot.hex](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases/download/v1.2.1/f407_iap_boot.hex) | Bootloader → 0x08000000 |
| [app_slot_a.hex](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases/download/v1.2.1/app_slot_a.hex) | A 槽应用 → 0x0800C000 |
| [app_slot_b.hex](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases/download/v1.2.1/app_slot_b.hex) | B 槽应用 → 0x08040000 |
| [app_slot_b_bad.hex](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases/download/v1.2.1/app_slot_b_bad.hex) | 回滚演示（永不确认） |
| [meta_a.bin](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases/download/v1.2.1/meta_a.bin) | 出厂元数据 → 0x08008000 |
| [SHA256SUMS](https://github.com/jnxhhtjmg/stm32-iap-ab-ota/releases/download/v1.2.1/SHA256SUMS) | 校验文件 |

## 特性

- **Direct-XIP A/B**：A 208 KB / B 256 KB，各自按自己的基址链接，不搬镜像。
- **追加式元数据**：96 字节记录，CRC32 自校验，最多 170 条，写满自动整理。
- **试运行回滚**：`TRIAL → CONFIRMED`，`attempts` 到 3 次未确认就回滚，回滚耗时 < 50 ms。
- **重复帧幂等**：DATA 的 ACK 丢失后，主机重发同一分片，设备直接回当前偏移，不重复写、不重复计入 CRC/SHA；
- **升级前置检查**：活动槽仍为 TRIAL 时拒绝升级另一个槽，避免擦掉唯一回退点；
- **掉电安全**：三个写入窗口（擦槽 / 写镜像 / 写记录）共 388 个断电点，逐个仿真 0 失败。
- **串口协议**：`AA 55` 同步 + 长度 + CRC32，10 条命令，508 字节分片，逐帧 ACK。
- **上位机三件套**：协议库、命令行、Tkinter GUI，全部纯 Python。
- **跨复位 API**：Bootloader 在 CCM 发布函数表，应用可调 `confirm()` / `request_update()`。
- **Ed25519 签名**：START 携带 64 字节签名，Bootloader 只认私钥签过的镜像。
- **防回滚**：元数据记录单调 `min_version`，低于下限的镜像在 START 阶段就被拒绝。
- **INFO 命令**：一条命令读出 Boot 版本、活动槽、状态、版本、下限、大小。
- **寄存器级实现**：不依赖 HAL，Flash/USART/GPIO 直接操作寄存器，Bootloader 约 20.6 KB。
- **OLED 提示**：SSD1306（I2C1，PB8/PB9）显示当前槽与升级状态。

## 架构

```
┌──────────────────────────────────────────────────────────────┐
│ 上位机（Python）                                              │
│   iap_proto.py   组帧/解帧/CRC/重试                            │
│   iap_update.py  命令行                                       │
│   iap_gui.py     Tkinter 界面                                 │
└───────────────────────────┬──────────────────────────────────┘
                            │ UART 115200 8N1
┌───────────────────────────▼──────────────────────────────────┐
│ STM32F407VE                                                   │
│                                                               │
│  0x08000000  Bootloader 32 KB（含 Ed25519 验签）               │
│              ├─ 启动决策：元数据 + CCM 请求 + 按键             │
│              ├─ 协议：START / DATA / END / STATUS / CONFIRM   │
│              └─ 跳转：VTOR / MSP / PC                         │
│  0x08008000  元数据 16 KB（96 B × 170）                        │
│  0x0800C000  Slot A 208 KB                                    │
│  0x08040000  Slot B 256 KB                                    │
│                                                               │
│  0x1000FE00  升级请求 magic（CCM，复位不清）                   │
│  0x1000FF00  API 表（CCM，每次启动重发）                       │
└──────────────────────────────────────────────────────────────┘
```

## 存储布局

| 区域 | 地址 | 大小 | 用途 |
| --- | --- | --- | --- |
| Bootloader | 0x08000000 | 32 KB | 启动决策、升级协议、Ed25519 验签、跳转 |
| 元数据 | 0x08008000 | 16 KB | 96 B 记录 × 170 |
| Slot A | 0x0800C000 | 208 KB | 应用 A |
| Slot B | 0x08040000 | 256 KB | 应用 B |
| 请求 magic | 0x1000FE00 | 4 B | 应用请求升级 |
| API 表 | 0x1000FF00 | 28 B | `confirm/state/active_slot/request_update/running_version` |

## 快速开始

```bash
git clone https://github.com/jnxhhtjmg/stm32-iap-ab-ota.git
cd stm32-iap-ab-ota
```

### 1. 硬件

- STM32F407VE 核心板（DevEBox / 大越创新 2019M）
- ST-Link V2（接 20-pin JTAG 的 pin 1/7/9/GND）
- CH340（TXD→PA10，RXD→PA9，共地）
- BOOT0 = 0，BOOT1 = 0

### 2. 编译（Keil MDK 5 + ARMCLANG）

打开 `IAP.uvmpw`，四个 target：

| target | 产物 | 链接基址 |
| --- | --- | --- |
| `boot` | `f407_iap_boot.hex` | 0x08000000（32 KB 区） |
| `app_slot_a` | `app_slot_a.hex` | 0x0800C000 |
| `app_slot_b` | `app_slot_b.hex` | 0x08040000 |
| `app_slot_b_bad` | `app_slot_b_bad.hex` | 0x08040000（故意不确认） |

### 3. 首次烧录

```powershell
powershell -ExecutionPolicy Bypass -File tools\flash_all.ps1
```

或手动：

```
f407_iap_boot.hex  → 0x08000000
app_slot_a.hex     → 0x0800C000
meta_a.bin         → 0x08008000
```

### 4. 升级

```bash
# 仓库自带 demo 密钥（keys/demo_signing.key），开箱即用；生产环境：
# python tools/gen_key.py --force      # 生成新密钥 + 更新 ab_pubkey.c，再重编 boot
python tools/iap_update.py --port COM6 --image release/firmware/app_slot_b.hex \
    --version 1.1.0 --key keys/demo_signing.key
python tools/iap_gui.py       # 图形界面
```

### 5. 验证回滚

```bash
python tools/iap_update.py --port COM6 --image release/firmware/app_slot_b_bad.hex
```

设备会打印 `trial boot 1/3 … 3/3`，然后 `rolling back`，最后回到 A。

## 协议

```
AA 55 | ver | type | seq(2) | len(2) | payload | crc32(4)
```

| 命令 | 码 | 作用 |
| --- | --- | --- |
| HELLO | 0x01 | 握手，返回协议版本/活动槽 |
| START | 0x02 | 声明版本/大小/CRC/SHA + 64 B Ed25519 签名，验签后擦槽 |
| DATA | 0x03 | 4 B 偏移 + ≤508 B 数据 |
| END | 0x04 | 整镜像 CRC/SHA 校验，写 TRIAL |
| STATUS | 0x05 | 查询槽状态 |
| CONFIRM | 0x06 | 手动确认 |
| REBOOT | 0x07 | 复位 |
| SET_BAUD | 0x08 | 切换波特率 |
| ABORT | 0x09 | 中止升级 |
| INFO | 0x0A | Boot 版本、活动槽、状态、版本、下限 |

完整字段与状态机见 [docs/PROTOCOL.md](docs/PROTOCOL.md)。

## 工具

| 文件 | 说明 |
| --- | --- |
| `tools/iap_proto.py` | 协议库：组帧、解帧、CRC、重试、镜像分析 |
| `tools/iap_update.py` | 命令行升级 |
| `tools/iap_gui.py` | Tkinter 图形界面 |
| `tools/powerloss_sim.py` | 掉电仿真：388 个断点 |
| `tools/flash_uart.py` | 纯串口烧录（ST ROM bootloader 救砖） |
| `tools/flash_all.ps1` | ST-Link 首次烧录 |

## 测试

```bash
python -m pytest -q
python tools/powerloss_sim.py
```

- 协议、CRC、镜像分析、元数据记录：`tests/`
- 掉电仿真：388 个断点，0 失败
- 上板清单：见 [docs/TESTING.md](docs/TESTING.md)

## 目录

```
bootloader/    Bootloader：启动决策、元数据、协议、Flash、跳转
app/           示例应用（A/B 两个链接目标 + 坏固件目标）
Drivers/       CMSIS
tools/         上位机与仿真
docs/          设计、协议、测试
release/       预编译 hex、GUI exe、首次烧录脚本
tests/         Python 单元测试
```

## 路线图

- **v1.2**（当前）：Ed25519 签名、防回滚、`INFO`、上板验证（升级/拒绝/回滚/拔电）
- **v1.3**：元数据 170 条压缩压力测试、CMake + GCC 构建、协议 fuzz
- **v1.4**：断点续传、传输层抽象（UART/CAN/USB）、差分升级

## 许可

MIT，见 [LICENSE](LICENSE)。
