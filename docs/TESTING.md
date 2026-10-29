# 测试说明

## 1. 单元测试

```bash
python -m pytest -q
```

覆盖：

| 文件 | 内容 |
| --- | --- |
| `tests/test_protocol.py` | 帧头、长度、CRC、payload 往返 |
| `tests/test_image_meta.py` | 镜像槽识别、元数据记录字段与 CRC |
| `tests/test_powerloss.py` | 记录扫描、坏记录截断、最后一条有效 |

## 2. 掉电仿真

```bash
python tools/powerloss_sim.py
```

做法：

1. 构造 4 条典型记录（A 确认 → B 试用 → B 确认 → 回 A）；
2. 对每条记录的每个字节位置模拟“写到一半断电”；
3. 重新扫描整个元数据扇区，检查是否能回到上一条有效记录；
4. 共 **388 个断点**，要求 0 失败。

```
cuts = 388
failures = 0
```

## 3. 上板测试清单

| 编号 | 用例 | 预期 |
| --- | --- | --- |
| 1 | 首次烧录 boot + A + meta | 串口打印 `boot A @0x0800C000` |
| 2 | HELLO 握手 | 返回协议版本与活动槽 |
| 3 | 升级 B（正常镜像） | 1.8 s 完成，重启进 B |
| 4 | B 自检确认 | 记录变 CONFIRMED |
| 5 | 升级 B（坏镜像） | 3 次后 `rolling back`，回 A |
| 6 | 升级中断电 | 重启仍跑 A |
| 7 | 命令进升级 | `update mode requested` |
| 8 | 手动 CONFIRM | 当前 TRIAL 变 CONFIRMED |
| 9 | 未签名镜像 | START 返回 `ERR_SIGNATURE`，不擦槽 |
| 10 | 签名正确 | 正常写入并跳转 |
| 11 | 低于 min_version 的镜像 | START 返回 `ERR_ROLLBACK` |
| 12 | INFO 命令 | 返回 Boot 版本、槽、版本、下限 |

## 4. 实测数据

| 指标 | 值 |
| --- | --- |
| 8 KB 升级耗时 | 1.8 s |
| 回滚耗时 | < 50 ms |
| Bootloader 大小 | 约 22.1 KB ROM / 10 KB RAM（32 KB 区，含 TweetNaCl） |
| 应用大小 | 8,144 B |
| 元数据记录 | 96 B × 170 |
| 掉电断点 | 388（0 失败） |

## 5. 回归要求

- 单元测试全绿；
- 掉电仿真 0 失败；
- 四个 Keil target 编译 0 error；
- 上板至少跑用例 1、3、5。

## 6. 上板实测（2026-10，ST-Link + COM6）

| 用例 | 结果 |
| --- | --- |
| 首次烧录 boot + A + meta | `boot A @0x0800C000`，app v1.0.1 |
| INFO | boot 1.1.0 / proto 2 / A CONFIRMED / min 1.0.1 |
| 签名升级 A→B v1.0.6 | 2.5 s 完成，B CONFIRMED，min 升到 1.0.6 |
| 签名升级 B→A v1.0.7 | 1.8 s 完成，A CONFIRMED，min 升到 1.0.7 |
| 未签名镜像 | `ERR_SIGNATURE`，槽未被擦 |
| 降级到 1.0.5 | `ERR_ROLLBACK`，槽未被擦 |
| 坏固件 B v1.0.8 | trial 1/3 → 3/3 → `rolling back` → A |
| 回滚后状态 | A CONFIRMED 1.0.7，min 1.0.7 |
| START 后立即 ABORT | 目标槽变 BAD，重启回滚到另一槽 |
| 对活动槽 START | `ERR_STATE`，不擦除 |
| SET_BAUD 230400 往返 | 通过，可恢复 115200 |
| 手动 CONFIRM | TRIAL→CONFIRMED，min 同步抬升 |
| 真实拔电（升级中） | 重启 A CONFIRMED，B 标记 BAD，未跳半截镜像 |

上板过程中发现并修复：

- IWDG 由应用启动后跨复位存活；Bootloader 启动将其重配为 8 s，扇区擦除前后各喂一次，协议空闲循环持续喂；
- TweetNaCl 验签栈需求超过 4 KB，Bootloader 栈提到 8 KB；
- START 签名域从“仅 SHA-256”改为“version + size + crc + sha256”。

## 重复帧与 TRIAL 保护（v1.2.2，已上板）

| 场景 | 操作 | 结果 |
| --- | --- | --- |
| 丢 ACK 重发 | 首片 508 B 正常写入后，立即重发同一帧 | 设备回 ACK(508)，不重复写；继续传完，END 成功 |
| 活动槽 TRIAL | A=CONFIRMED，B 刷入永不确认固件，B 进入 TRIAL 后尝试 START A | 被拒绝，返回 `ERR_STATE`，日志 `active is trial, confirm first` |
| 坏固件回滚 | 让 B 的 TRIAL 计满 3 次 | B 显式标 BAD，启动回 A；重新刷 B 后两槽 CONFIRMED |

| 断点续传 | 传 5080 B 后 REBOOT（不 END），重新 START 同镜像 | 返回 offset=4096，继续传完，END 成功 |
| ABORT | START 后传 508 B，发 ABORT | 目标槽标 BAD，另一槽仍 CONFIRMED |
| 无主机回退 | 制造 DOWNLOADING 后复位，5 s 内不连接 | 等 5 s 后启动另一槽，不卡在 Bootloader |

HIL 脚本：`tools/hil_duplicate_ack.py --port COM6`、`tools/hil_resume.py --port COM6`。
