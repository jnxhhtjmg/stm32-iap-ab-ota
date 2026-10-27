# 全功能演示脚本（照着点）

> 目标：15 分钟内把「连接、状态、升级、确认、回滚、重启、断电安全」全部演示一遍。
> 每一步都给出**界面日志**、**OLED 画面**、**串口输出**三个对照。

## 准备

| 项 | 状态 |
| --- | --- |
| 板子 | BOOT0 = 0，CH340 已接，USB 已插 |
| 串口 | COM6，115200 |
| 程序 | `release\AB-IAP升级工具.exe` |
| 固件 | `release\firmware\` 下的 hex |

先关掉任务栏里所有旧的"AB-IAP 升级工具"窗口，再双击 exe。

---

## 第 1 步：连接

**操作**：选 COM6 → 点「连接」

**界面日志**：

```
[..] 打开 COM6 @ 115200
[..] 已发送 boot，等待应用复位 ...
[..] 已连接
```

**OLED**：`SLOT A` + 大字 A + 底部柱状条在动。

---

## 第 2 步：读状态

**操作**：点「刷新状态」

**界面**：

```
bootloader: protocol 1  build 1.0.0.0   active=A state=CONFIRMED
slot A: v1.0.1.0  7968 bytes  CONFIRMED  attempts=0
slot B: 空
```

---

## 第 3 步：升级 A → B（核心功能）

**操作**：浏览 `release\firmware\app_slot_b.hex`，版本填 `1.0.4` → 点「开始升级」

**界面日志**：

```
image      : 7968 bytes, linked for slot B
target     : slot B (active is A)
erasing and uploading ...
done       : slot B v1.0.4.0 in 1.5s
重启进入试用固件 ...
设备已重启，点『连接』继续
```

**OLED**：约 1 秒后从「柱状条」变成 **`SLOT B` + 大字 B + 小球弹跳**。

**串口**：`hb,1`、`hb,2` … 每秒一次。

---

## 第 4 步：确认升级结果

**操作**：点「连接」→ 点「刷新状态」

**界面**：

```
active=B state=CONFIRMED
slot A: v1.0.1.0  7968 bytes  CONFIRMED
slot B: v1.0.4.0  7968 bytes  CONFIRMED
```

> 这一步证明：新固件跑完 3 秒自检后**自己确认**了，不是靠人点。

---

## 第 5 步：升回 A

**操作**：浏览 `release\firmware\app_slot_a.hex`，版本填 `1.0.5` → 开始升级

**OLED**：变回 `SLOT A` + 柱状条。
**界面**：重连 + 刷新状态 → `active=A`，slot A `v1.0.5`。

> 为什么要先回 A？因为下一步要往 B 刷坏固件，不能覆盖当前运行槽。

---

## 第 6 步：回滚演示（最出彩）

**操作**：浏览 `release\firmware\app_slot_b_bad.hex`，版本填 `9.9.9` → 开始升级

**OLED**：显示

```
   BAD
   FW
no confirm
```

然后设备每 4 秒复位一次，屏幕反复闪这三行。

**串口**（用串口助手或 `iap_update.py` 观察）：

```
trial boot 1/3
trial boot 2/3
trial boot 3/3
trial attempts exhausted, rolling back
meta: slot=A state=2 attempts=0
```

约 15 秒后，**OLED 自动变回 `SLOT A` 柱状条** —— 这就是试运行失败自动回滚。

**界面**：重连 + 刷新状态 → `active=A`，slot B 显示 `TRIAL attempts=3`。

---

## 第 7 步：应用主动进入升级模式

**操作**：在当前应用运行时，点「应用→升级模式」

**界面日志**：

```
已发送 boot，等待应用复位 ...
```

**串口**：

```
rebooting to bootloader
AB-IAP boot v1.0
meta: slot=A state=2 attempts=0
update mode requested
```

再点「连接」就能连上。

---

## 第 8 步：远程重启

**操作**：在 bootloader 状态下点「重启设备」

**结果**：设备重启进入活动槽，OLED 恢复动画。

---

## 第 9 步（可选）：升级中断电

**操作**：升级到一半（进度条约 50%）时拔掉板子电源/CH340，再上电。

**结果**：

- 旧槽固件照常启动，OLED 显示旧槽动画；
- 元数据没变，重新连接后可再升级一次。

> 这一步演示「掉电不变砖」。注意：不要反复在擦除瞬间断电，虽然设计上安全，但没必要折腾板子。

---

## 收尾

保持 `active=A` 即可。检查一遍：

| 检查项 | 期望 |
| --- | --- |
| OLED | `SLOT A` + 柱状条 |
| 串口 | 每秒 `hb,N` |
| 界面状态 | `active=A`，两槽版本正确 |
