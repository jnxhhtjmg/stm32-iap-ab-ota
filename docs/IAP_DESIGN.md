> 注意：v1.1 起 Bootloader 扩为 32 KB（Ed25519 验签），本文描述的是 v1.0 布局；最新地址见 README。

# 串口 IAP 固件升级系统设计（独立项目 v1.1）

> 状态：定稿
> 定位：**独立项目**。这块 F407 只跑本系统，不依赖也不修改 `f407_gesture_ai`
> 组成：Bootloader + 示例应用（双槽）+ 上位机 + 文档
> 硬件：STM32F407VET6（512 KB Flash / 128 KB SRAM + 64 KB CCM），Keil MDK + ARMCLANG V6.7，USART1 + CH340

---

## 1. 项目定位

一套可演示、可复现、可讲解的串口 IAP 参考实现：

1. **Bootloader**：上电决策、串口升级、校验、跳转、试运行计数、回滚。
2. **示例应用**：裸机小程序，带版本号、心跳、自检后确认、看门狗、`fail` 指令（用于演示回滚）。
3. **上位机**：Python 命令行，打包 → 传输 → 校验 → 重启。
4. **文档**：设计、协议、测试、排错、简历材料。

示例应用刻意做小（目标 < 16 KB），目的是让"升级/回滚"的每一行都可控；后续替换成真实应用时，只要 ≤ 224 KB 并按槽链接即可。

### 非目标

签名/加密、差分升级、外部 Flash 暂存、跨复位断点续传、多镜像。

---

## 2. 硬件约束与分区

### 2.1 扇区

| 扇区 | 起始 | 大小 |
| --- | --- | --- |
| S0–S3 | 0x08000000 | 16 KB × 4 |
| S4 | 0x08010000 | 64 KB |
| S5 | 0x08020000 | 128 KB |
| S6 | 0x08040000 | 128 KB |
| S7 | 0x08060000 | 128 KB |

### 2.2 为什么仍选 direct-xip

应用只有十几 KB 时，swap-using-scratch 理论可行，但要吃掉 128 KB scratch，且回滚需要搬运整个镜像、要处理"换到一半掉电"的恢复状态机。direct-xip 的优势：

- 不占 scratch，两个槽都能到 224 KB，给真实应用留足空间；
- 回滚只是改一条元数据，不擦不搬，速度快、擦写次数少；
- 没有 swap 状态区，掉电分析简单（见 §5）。

代价只有一个：应用要按两个链接地址各编译一次（两个 Keil target）。

### 2.3 分区表（定稿）

| 区域 | 扇区 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- | --- |
| Bootloader | S0 | 0x08000000 – 0x08003FFF | 16 KB | 当前 7.7 KB |
| Metadata | S1 | 0x08004000 – 0x08007FFF | 16 KB | 96 B × 170 条追加日志 |
| Slot A | S2–S5 | 0x08008000 – 0x0803FFFF | 224 KB | 应用，链接基址 0x08008000 |
| Slot B | S6–S7 | 0x08040000 – 0x0807FFFF | 256 KB | 应用，链接基址 0x08040000 |

镜像上限取两槽较小者：**224 KB**。

### 2.4 CCM 保留区

| 地址 | 用途 |
| --- | --- |
| 0x1000FE00 | 升级请求 magic `0xB00710AD` |
| 0x1000FF00 | 运行时 API 表（magic `ABIP` + 5 个函数指针） |

---

## 3. 镜像、元数据与 API

### 3.1 槽内镜像

裸 `.bin`，向量表在槽基址第一个字节，无镜像头。版本/大小/CRC/SHA 存在元数据里。

启动前健全性检查：MSP ∈ [0x20000000, 0x20020000) 且 4 字节对齐；复位向量为奇数且在槽范围内。

### 3.2 元数据记录（96 B，小端，追加写）

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 4 | magic | `0x41424946` |
| 4 | 2 | rec_size | 96 |
| 6 | 1 | slot | 0=A，1=B |
| 7 | 1 | state | 0 NONE / 1 TRIAL / 2 CONFIRMED / 3 BAD |
| 8 | 4 | seq | 递增 |
| 12 | 4 | version | `0xMMmmppbb` |
| 16 | 4 | img_size | 字节 |
| 20 | 4 | img_crc32 | CRC32 |
| 24 | 2 | attempts | 已试启动次数 |
| 26 | 2 | flags | bit0 = 请求升级 |
| 28 | 32 | sha256 | 镜像 SHA-256 |
| 60 | 4 | crc32 | 前 60 字节 CRC32 |
| 64 | 32 | reserved | 0 |

只追加不擦除；写满或遇坏记录时由 bootloader 在启动阶段整理（擦 S1、写回最新一条）。应用确认只追加（约 1 ms），不擦除。

### 3.3 运行时 API（CCM）

| 函数 | 作用 |
| --- | --- |
| `confirm()` | TRIAL → CONFIRMED，attempts 清零 |
| `state()` | 当前槽状态 |
| `active_slot()` | 当前槽 |
| `request_update()` | 写 magic + 软复位进 bootloader |

应用只检查 magic 后调用，不拷贝代码。ABI 版本 = 1。

---

## 4. 状态机与启动流程

| 当前 | 事件 | 动作 | 新状态 |
| --- | --- | --- | --- |
| 空 | 首次上电，A 向量合法 | 视为出厂镜像 | CONFIRMED(A) |
| CONFIRMED(x) | 下载完成并通过校验 | 追加 TRIAL(y) | TRIAL(y) |
| TRIAL(y) | 启动且 attempts < 3 | attempts++，启动 y | TRIAL(y) |
| TRIAL(y) | 应用自检通过 | 追加 CONFIRMED(y) | CONFIRMED(y) |
| TRIAL(y) | attempts ≥ 3 或校验失败 | 标 BAD，追加 CONFIRMED(x)，启动 x | CONFIRMED(x) |
| TRIAL(y) | 另一槽也无效 | 停 bootloader | recovery |

启动流程：

```
上电
 ├─ 16 MHz / USART1 115200 / GPIO
 ├─ 发布 API 表
 ├─ 读升级请求 magic（有则清零）
 ├─ 读元数据最新记录 → 判定 active/state
 │   ├─ 空记录：A 向量合法则 A/CONFIRMED，否则 recovery
 │   ├─ TRIAL：SHA 校验 + attempts 判断 → 递增或回滚
 │   └─ CONFIRMED / BAD：使用或回滚
 ├─ 请求 / flags / PE3 → 升级循环（不返回）
 ├─ 400 ms 窗口收到合法帧 → 升级循环
 └─ 跳转：关 SysTick、关中断、清 NVIC、设 VTOR/MSP、跳复位向量
```

时间预算（示例应用约 12 KB，@921600）：擦除 < 0.3 s，传输 < 0.2 s，校验 < 0.05 s；启动窗口 0.4 s。

---

## 5. 掉电与异常

| 时机 | Flash 状态 | 重启结果 |
| --- | --- | --- |
| 写元数据记录中途 | 该条 CRC 坏 | 忽略，回退上一条 |
| 下载中途 | 目标槽不完整 | 旧槽照常启动 |
| END 校验后未写记录 | 新槽完整但无记录 | 启动旧槽 |
| 写 TRIAL 后跳转前 | TRIAL 已落盘 | 下次启动计次 |
| 整理元数据中途 | S1 全 FF 或部分坏 | 视为空日志，按 A 槽向量判定 |
| 跳转瞬间 | 无影响 | 正常启动 |

已知薄弱点：整理期间掉电可能回退一个版本（窗口 ≤ 250 ms，170 次升级才触发一次）。后续可加第二个 meta 扇区做乒乓，v1 不做。

---

## 6. 串口协议

```
AA 55 | ver(1) | type(1) | seq(2) | len(2) | payload | crc32(4)
```

CRC32 覆盖 `ver..payload`；`len ≤ 512`；响应 `type |= 0x80`，`seq` 回显；响应首字节为状态码（0 OK，1 CRC，2 长度，3 状态，4 大小，5 Flash，6 校验，7 版本）。

| 命令 | type | 请求 | 响应 |
| --- | --- | --- | --- |
| HELLO | 01 | 空 | status, proto, boot_ver(2), active, state, max_attempts, pad, max_image(4), flags(4) = 16 B |
| START | 02 | slot(1)+pad(3)+version(4)+size(4)+crc32(4)+sha256(32) = 48 B | status, slot, offset(4) = 6 B |
| DATA | 03 | offset(4)+data | status, next_offset(4) |
| END | 04 | crc32(4)+sha256(32) = 36 B | status, slot, version(4) |
| STATUS | 05 | 空 | status, active, state, count, 2×12 B |
| CONFIRM | 06 | 空 | status |
| REBOOT | 07 | 空 | status |
| SET_BAUD | 08 | baud(4)，115200/230400/460800/921600 | status |
| ABORT | 09 | 空 | status |

容错：主机逐帧 ACK，超时 1 s、重试 3 次，START 超时 10 s；设备帧 CRC 错直接丢弃；30 s 无帧自动复位。

---

## 7. 示例应用

裸机，无 FreeRTOS、无 HAL 依赖（只用 CMSIS），UM 约 8–12 KB。

| 功能 | 说明 |
| --- | --- |
| 版本号 | 编译宏 `APP_VERSION`（默认 0x00010001），START 时由上位机传入同值 |
| 启动信息 | `app v1.0.1 slot=A/B reset=...` |
| 心跳 | 每 1 s 输出 `hb,<n>` |
| 自检 | 3 s 内完成（示例：检查 UART/时钟/一个内存区 CRC），通过后调用 `confirm()` |
| 看门狗 | IWDG 约 4 s，主循环喂狗；跑飞则复位 → 计入 trial 次数 |
| `boot` 指令 | 调用 `request_update()`，软复位进 bootloader |
| `fail` 指令 | 停止喂狗，4 s 后看门狗复位（演示 trial 计次与回滚） |
| `info` 指令 | 打印当前槽、状态、确认次数 |

两个 Keil target：`app_slot_a`（IROM 0x08008000，`VECT_TAB_OFFSET=0x8000`）、`app_slot_b`（IROM 0x08040000，`VECT_TAB_OFFSET=0x40000`）。

---

## 8. 上位机

`tools/iap_update.py`：

| 命令 | 行为 |
| --- | --- |
| `status` | 打印 active/state/两槽版本 |
| `flash <hex|bin> [--slot auto|a|b] [--baud N]` | 转 bin → 选空闲槽 → START/DATA/END → 校验 → 可选 REBOOT |
| `confirm` | 手动确认 |
| `--wait` | 持续 HELLO，配合手动复位 |

---

## 9. 测试与验收

| # | 用例 | 通过标准 |
| --- | --- | --- |
| T1 | 烧 bootloader + A 槽应用 | banner 正确，应用正常跑 |
| T2 | 正常升级 A→B | 传输成功，重启 trial，自检后 CONFIRMED |
| T3 | 连续升级 20 次 | 全部成功，A/B 正确轮换 |
| T4 | 升级坏镜像（改 1 字节） | END 报错，旧固件不受影响 |
| T5 | trial 期间 `fail` 3 次 | 自动回滚旧版本 |
| T6 | 下载中途断电 | 重启跑旧固件，可重传 |
| T7 | 921600 升级 | 成功，全程 < 2 s |
| T8 | 两槽都擦除 | 停留 recovery，可重传恢复 |
| T9 | 元数据写满整理 | 状态保持，升级继续 |
| T10 | 连续 50 次升级（压力） | 无失败，日志/擦除行为符合预期 |

验收：T1–T10 全过；bootloader ≤ 16 KB；应用 ≤ 224 KB；启动额外延迟 ≤ 0.5 s。

---

## 10. 里程碑

| # | 交付物 | 责任 |
| --- | --- | --- |
| M1 | 本设计文档 v1.1 | 我 |
| M2 | bootloader 源码 + Keil 工程（已通过编译，7.7 KB）+ 自测 | 我 |
| M3 | 示例应用（双 target）+ Keil 工程 | 我 |
| M4 | `tools/iap_update.py` + 离线自测 | 我 |
| M5 | 测试清单/排错手册/简历材料 | 我 |
| M6 | 实板 T1–T10 | 你 |

---

## 11. 风险

| # | 风险 | 缓解 |
| --- | --- | --- |
| R1 | 400 ms 窗口错过 | `--wait` 持续 HELLO + 手动复位；PE3 强制进入 |
| R2 | 元数据整理期间掉电 | 概率低；后续加第二 meta 扇区 |
| R3 | CH340 无 DTR 复位 | 手动复位；应用 `boot` 指令兜底 |
| R4 | 921600 误码 | 默认 115200，稳定后再升；SET_BAUD 可回退 |
| R5 | 应用后续超 224 KB | 换 32 KB boot 分区（A 208 KB）或裁剪 |

---

## 12. 待确认

| # | 事项 | 默认 |
| --- | --- | --- |
| Q1 | PE3 空闲，可作强制进 bootloader | 可用 |
| Q2 | 每次上电 400 ms 串口窗口 | 接受 |
| Q3 | 默认波特率 | 115200（可切 921600） |

---

## 13. 当前状态

| 模块 | 状态 |
| --- | --- |
| `bootloader/Src`（flash/meta/crc32/sha256/proto/api/main） | 完成，0 error / 0 warning |
| `bootloader/MDK/f407_iap_boot.uvprojx` | 由 `tools/gen_boot_project.py` 生成 |
| `app/` 示例应用 | 完成：`app_slot_a` / `app_slot_b` 各 4280 B；`app_slot_b_bad` 1856 B |
| `tools/iap_update.py` | 完成：status / flash / confirm / reboot / provision / selftest |
| `tools/iap_gui.py` | 完成：Tkinter 界面 |
| `tools/flash_all.ps1` | 完成：ST-Link 首次烧录 |
| `tools/powerloss_sim.py` | 完成：388 个断点，0 失败 |
| **实板验证** | **完成**（2026-10-25） |

### 实板实测

```
首次烧录            bootloader + A 槽 + 元数据      通过
状态读取            active=A CONFIRMED             通过
A -> B 升级         4280 B / 1.5 s                通过
试运行确认          hb 每秒 1 次，3 s 后 CONFIRMED  通过
坏固件回滚          trial 2/3 -> 3/3 -> boot A     通过
看门狗              4 s 复位，reset=iwdg            通过
应用命令进入升级    发送 "boot" 后复位              通过
```

### 调试中修掉的三个真实 bug

| # | 现象 | 根因 | 修复 |
| --- | --- | --- | --- |
| 1 | 跳转后应用卡死、tick 不涨 | `jump_to()` 关了中断，应用没重新开 | 跳转前 `__enable_irq()`，应用侧再加一次防御 |
| 2 | 串口全 0x00 / 无响应 | 工程未定义 `HSE_VALUE`，`SystemCoreClockUpdate()` 按 25 MHz 算出 525 MHz，BRR 错 | 时钟变量显式设为 168 MHz，BRR 按本板实测公式 `84e6/baud` |
| 3 | bootloader 一进协议就 HardFault | 1 KB 栈被 520 B 帧缓冲 + 调用链撑爆 | 栈改 2 KB，帧缓冲改 static |

### 已知限制

| 限制 | 说明 |
| --- | --- |
| 只验证 115200 | 其他波特率代码支持但未实测，上位机已限制为 115200 |
| BRR 为板级标定 | 公式 `84e6/baud` 在本板实测正确，换晶振/时钟需重算 |
| 无签名/防回滚 | v1 只做 CRC32 + SHA-256 |
| 单 meta 扇区 | 整理期间掉电会退一版 |

---

## 14. 变更记录

| 版本 | 日期 | 变更 |
| --- | --- | --- |
| v1.0 | 2026-10-25 | 初版（基于手势识别工程 182 KB 应用） |
| v1.1 | 2026-10-25 | 改为独立项目；应用自研；删除对旧工程的依赖；分区与协议不变 |
| v1.2 | 2026-10-25 | 示例应用、双 target + 坏固件 target、GUI/CLI 上位机、首次烧录脚本、掉电模拟；补充快速上手指南 |
| v1.3 | 2026-10-25 | 实板调通：修复中断使能、时钟/BRR、bootloader 栈溢出；完成升级与回滚实测 |

