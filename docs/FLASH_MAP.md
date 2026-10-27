> 注意：v1.1 起 Bootloader 扩为 32 KB（Ed25519 验签），本文描述的是 v1.0 布局；最新地址见 README。

# 当前固件的 Flash 结构（实板）

> 基准：`flash_all.ps1` 烧录后的状态；地址均为 STM32F407 内部 Flash。
> 所有多字节值都是**小端**（低字节在前）。

## 1. 烧录动作

```powershell
openocd ... -c "program release/firmware/f407_iap_boot.hex verify"          # → 0x08000000
              -c "program release/firmware/app_slot_a.hex verify"           # → 0x08008000
              -c "program release/firmware/meta_a.bin 0x08004000 verify"    # → 0x08004000
```

## 2. 总体地图

| 区域 | 起始 | 结束 | 大小 | 当前内容 |
| --- | --- | --- | --- | --- |
| Bootloader | 0x08000000 | 0x08001ECF | 7,888 B (0x1ED0) | 完整镜像 |
| （空） | 0x08001ED0 | 0x08003FFF | — | FF |
| 元数据 | 0x08004000 | 0x0800405F | 96 B | 记录 #1（A/CONFIRMED） |
| （空） | 0x08004060 | 0x08007FFF | — | FF（下一条记录从这里开始） |
| Slot A | 0x08008000 | 0x08009FCF | 8,144 B (0x1FD0) | 应用镜像 |
| （空） | 0x08009FD0 | 0x0803FFFF | — | FF |
| Slot B | 0x08040000 | 0x08041FCF | 8,144 B | 升级后才有；出厂时全 FF |
| （空） | 0x08041FD0 | 0x0807FFFF | — | FF |

## 3. Bootloader 开头 16 字节

```
0x08000000: B0 0D 00 20   ← 初始 SP = 0x20000DB0
0x08000004: 9D 01 00 08   ← Reset_Handler = 0x0800019D
0x08000008: A5 01 00 08   ← NMI
0x0800000C: A7 01 00 08   ← HardFault
```

## 4. Slot A 应用镜像

```
0x08008000: 80 08 00 20   ← 初始 SP = 0x20000880
0x08008004: 9D 81 00 08   ← Reset_Handler = 0x0800819D
0x08008008: A5 81 00 08   ← NMI
0x0800800C: A7 81 00 08   ← HardFault
```

镜像内部：

| 区间 | 内容 |
| --- | --- |
| 0x08008000–0x08008187 | 向量表（392 B：16 个异常 + 82 个中断） |
| 0x08008188–0x08009FC7 | .text 代码 + .rodata 常量 |
| 0x08009FC8–0x08009FCF | .data 初值（8 B，启动时拷到 SRAM） |

## 5. 元数据记录 #1（0x08004000，96 B）

```
46 49 42 41  60 00  00  02   01 00 00 00   00 01 00 01
"ABIF"       96     A   CONF  seq=1         ver=1.0.1

D0 1F 00 00  84 FA E1 B3   00 00  00 00
size=8144    crc32          att    flags

67 14 70 08 4E 4A 7C A4 82 B7 56 FB 6E B7 96 AC   ← sha256 前 16 字节
...（共 32 字节）

5F 31 A3 4E   ← 记录自身 CRC32（前 60 字节）
00 ...（reserved 32 字节）
```

字段对照：

| 偏移 | 值 | 含义 |
| --- | --- | --- |
| 0 | `46 49 42 41` | magic "ABIF" |
| 4 | `60 00` | 记录长度 96 |
| 6 | `00` | slot A |
| 7 | `02` | CONFIRMED |
| 8 | `01 00 00 00` | seq = 1 |
| 12 | `00 01 00 01` | 版本 1.0.1 |
| 16 | `D0 1F 00 00` | 镜像大小 8144 |
| 20 | `84 FA E1 B3` | 镜像 CRC32 |
| 24 | `00 00` | attempts = 0 |
| 26 | `00 00` | flags = 0 |
| 28 | 32 B | 镜像 SHA-256 |
| 60 | `5F 31 A3 4E` | 记录 CRC32（覆盖 0–59） |
| 64 | 32 B | 保留 |

## 6. 之后会怎么变

| 事件 | Flash 变化 |
| --- | --- |
| 应用确认 | 0x08004060 追加一条 A/CONFIRMED（seq=2） |
| 升级到 B | 0x08040000 起写入新镜像；元数据追加 B/TRIAL |
| B 自检通过 | 再追加一条 B/CONFIRMED |
| 坏固件回滚 | 再追加 A/CONFIRMED，Slot B 内容保持不变但被标记失败 |
| 写满 170 条 | Bootloader 启动时擦除元数据扇区，只保留最新一条 |
