# 5 分钟上手（已在实板验证）

> 验证环境：STM32F407VE + ST-Link V2 + CH340，Windows，2026-10-25
> 已验证：首次烧录、串口升级、试运行确认、看门狗回滚

## 0. 准备

| 项 | 要求 |
| --- | --- |
| 板子 | STM32F407VE，BOOT0 = 0 |
| 调试器 | ST-Link V2，接 20-pin JTAG（P1） |
| 串口 | CH340，PA9/PA10，**115200 8N1** |
| 软件 | Python 3 + pyserial、OpenOCD |

```powershell
python -m pip install pyserial
```

## 1. 第一次烧录（一次性）

```powershell
powershell -ExecutionPolicy Bypass -File tools\flash_all.ps1
```

烧三样东西：bootloader（0x08000000）、应用 A（0x0800C000）、出厂元数据（0x08008000）。

> 会覆盖板子原固件。之后升级不再需要 ST-Link。

## 2. 打开界面

```powershell
python tools\iap_gui.py
```

1. 选 COMx，点 **连接**；
2. 连不上 → 点 **应用→升级模式**（给应用发 `boot`），或按板子复位键，再连接；
3. 点 **刷新状态**，应看到：

```
bootloader: protocol 1  build 1.0.0.0   active=A state=CONFIRMED
slot A: v1.0.1.0  7968 bytes  CONFIRMED  attempts=0
slot B: 空
```

## 3. 升级到 B 槽

1. **浏览** → `app\MDK\app_slot_b\app_slot_b.hex`；
2. 版本填 `1.0.4`；
3. **开始升级**。

实测输出：

```
image      : 7968 bytes, linked for slot B
target     : slot B (active is A)
done       : slot B v1.0.4.0 in 1.5s
```

设备重启进入 B，应用跑 3 秒自检后确认。串口（115200）能看到：

```
hb,1
hb,2
```

再 **刷新状态**：

```
active=B state=CONFIRMED
slot A: v1.0.1.0  CONFIRMED
slot B: v1.0.4.0  CONFIRMED
```

## 4. 回滚演示

前提：当前在 **A 槽**运行（如果刚升到 B，先升回 A）。

1. 浏览 `app\MDK\app_slot_b_bad\app_slot_b_bad.hex`（故意不确认的坏固件）；
2. 版本 `9.9.9`，开始升级；
3. 设备试用 B，坏固件打印一行后停止喂狗，看门狗 4 秒复位；
4. 连续 3 次后自动回滚。

实测串口输出：

```
app v1.0.1 slot=B reset=iwdg
this build never confirms (rollback demo)

AB-IAP boot v1.0
trial boot 2/3
boot B @0x08040000

...

trial attempts exhausted, rolling back
meta: slot=A state=2 attempts=0
boot A @0x0800C000.
app v1.0.1 slot=A reset=iwdg
selftest...... ok
running, commands: info | boot | fail
```

## 5. 命令行

```powershell
python tools\iap_update.py status -p COM6
python tools\iap_update.py flash app\MDK\app_slot_b\app_slot_b.hex -p COM6 --version 1.0.4
python tools\iap_update.py confirm -p COM6
python tools\iap_update.py reboot -p COM6
python tools\iap_update.py selftest
python tools\powerloss_sim.py
```

注意：`-p/--wait` 放在子命令前后都可以（已做兼容）。

## 6. 已知行为（不是故障）

| 现象 | 说明 |
| --- | --- |
| OLED 显示 | A 槽：`SLOT A` + 柱状动画；B 槽：`SLOT B` + 小球动画 |
| 上电多等 400 ms | bootloader 的串口升级窗口 |
| 只支持 115200 | 已验证的速率；其他速率代码支持但未实测 |
| 每次升级后设备重启 | trial 镜像需要重启才能被 bootloader 选中 |

## 7. 排错

| 现象 | 处理 |
| --- | --- |
| 没有 COM 口 | 装 CH340 驱动；换数据线；确认 `USB-SERIAL CH340` |
| 连接超时 | 应用在跑；点"应用→升级模式"或按复位 |
| "正是当前运行槽" | 换另一个槽的 hex（工具会检测链接地址） |
| 串口乱码 | 波特率必须是 115200；确认 CH340 是 3.3 V 档 |
| 升级后没反应 | 按复位；看串口是否有 `trial boot` |
| 完全变砖 | BOOT0=1 进 ST ROM bootloader，用 `tools\flash_uart.py` 重烧 |
