# 掉电续传实板验证

目标：升级传到一半时**真实断电**，重新上电后从断点继续，旧槽不受影响。

> 这不是脚本里的 `REBOOT`，是拔板子供电。Flash 状态等价，但演示和面试口径更硬。

## 0. 前提

- 板子已烧入带断点续传的 Bootloader（v1.3.0 及以上）；
- 两个槽都有可用镜像（`status` 里 A/B 至少一个 CONFIRMED）；
- ST-Link 只用于看状态，**不要**在掉电测试中给板子供电；板子用独立 USB 供电；
- 串口工具占用 COM 时先关掉。

先记下当前状态：

```powershell
cd C:\Users\17638\Desktop\stm32_iap_ab_ota
python tools\iap_update.py status -p COM6
```

## 1. 生成 64 KB 测试镜像

8 KB 镜像升级只有 2 秒，来不及拔。把 B 槽镜像补到 64 KB：

```powershell
@'
import sys
from pathlib import Path
sys.path.insert(0, "tools")
import iap_proto as P

img = P.load_image("release/firmware/app_slot_b.hex")
img = img + b"\xFF" * (64 * 1024 - len(img))
Path("big_b.bin").write_bytes(img)
print("big_b.bin", len(img), "bytes")
'@ | python -
```

`big_b.bin` 仍然是合法的 B 槽镜像：向量表在开头，尾部是 0xFF 填充，应用能正常跑。

## 2. 第一次升级，准备拔电

```powershell
python tools\iap_update.py flash big_b.bin --version 1.3.6 -p COM6
```

终端会打印：

```
target     : slot B (active is A)
erasing and uploading ...
  sent 4064/65536 bytes
  sent 8128/65536 bytes
  ...
```

**等到看到：**

```
  sent 32768/65536 bytes
```

立刻**拔掉板子的 USB 供电**（如果 ST-Link 也给板子供电，连 ST-Link 一起拔）。

终端会报 `no response` / 连接错误，这是预期的。

## 3. 重新上电，5 秒内续传

插回 USB。Boot 看到 `DOWNLOADING` 记录，会给 **5 秒**续传窗口。

立刻执行**同样的命令、同样的版本号**：

```powershell
python tools\iap_update.py flash big_b.bin --version 1.3.6 -p COM6
```

看到这一行就说明续传生效：

```
  resuming at 32768/65536 bytes
```

然后继续传完：

```
done       : slot B v1.3.6 in 1.x s
```

第二次耗时应该明显短于第一次（只传后一半）。

## 4. 验证

```powershell
python tools\iap_update.py status -p COM6
```

期望：

```
active     : slot B  state CONFIRMED
slot A     : v1.3.3  ...  CONFIRMED
slot B     : v1.3.6  65536 bytes  CONFIRMED
```

如果新固件还在试运行，等 3 秒自检确认后再看。

## 5. 失败处理

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 没有 `resuming at`，从 0 开始 | 版本号或镜像不一致 | 用完全相同的 `big_b.bin` 和 `1.3.6` |
| 连不上，Boot 已回退 A | 超过 5 秒窗口 | 按一下复位，上电后 5 秒内再跑命令 |
| `resuming at 0` | 拔电时还没写满 4 KB | 晚一点拔，至少看到 `sent 4064` 之后 |
| START 返回签名错 | 私钥不对 | 用 `keys/demo_signing.key` |
| 续传后 END 校验错 | 前缀 Flash 被改动 | 重新完整升级一次 |

## 6. 记录表（面试可带着）

| 项 | 值 |
| --- | --- |
| 日期 |  |
| 镜像 | `big_b.bin`，65,536 B |
| 版本 | 1.3.6 |
| 拔电时偏移 | 32,768 |
| 续传返回偏移 |  |
| 第一次耗时 |  |
| 第二次耗时 |  |
| 最终状态 | B CONFIRMED |

## 7. 和模拟复位的区别

`tools/hil_resume.py` 用协议里的 `REBOOT` 命令复位，验证的是：

- DOWNLOADING 记录持久化；
- START 能返回 written；
- 前缀 CRC/SHA 能重算。

真实拔电额外验证：

- 掉电瞬间 Flash 写入不会把记录写坏；
- 上电后 Boot 不跳半截镜像；
- 5 秒窗口和回退逻辑。

两者都做，面试时可以说“复位和真实掉电都验证过”。
