# 贡献指南

## 开发环境

- Keil MDK 5.x + ARMCLANG（固件）
- Python 3.10+（上位机与测试）
- ST-Link V2 + STM32F407VE 核心板（上板验证）

## 提交前

```bash
python -m pytest -q          # 单元测试必须全绿
python tools/powerloss_sim.py  # 掉电仿真必须 0 失败
```

固件改动请在 `IAP.uvmpw` 里四个 target 全部编译通过：

```
boot / app_slot_a / app_slot_b / app_slot_b_bad
```

## 代码风格

- C：4 空格缩进，函数名 `ab_` 前缀，模块内 `static`。
- Python：PEP 8，类型标注，禁止裸 `except`。
- 提交信息：`area: summary`，例如 `boot: fix trial counter wrap`。

## 修改协议或元数据

这两者跨 Bootloader / 应用 / 上位机，改之前请：

1. 先开 issue 说明兼容性影响；
2. 同步更新 `docs/PROTOCOL.md`；
3. 提升 `AB_API_VERSION` 或协议版本；
4. 补 `tests/` 用例。

## 分支

- `main`：可发布
- `feat/*`：新功能
- `fix/*`：修复

PR 请附：改动说明、测试方式、串口日志（涉及固件时）。
