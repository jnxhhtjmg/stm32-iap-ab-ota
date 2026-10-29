# 变更记录

## v1.0.0

首个可发布版本。

- direct-xip A/B 双区：Boot 16 KB、Meta 16 KB、A 224 KB、B 256 KB
- 96 字节追加式元数据，CRC32 自校验，170 条容量
- TRIAL / CONFIRMED / BAD 状态机，3 次未确认自动回滚
- 串口协议：HELLO / START / DATA / END / STATUS / CONFIRM / REBOOT / SET_BAUD / ABORT
- Python 协议库、CLI、Tkinter GUI
- 掉电仿真：388 个断点，0 失败
- CCM API 表：confirm / state / active_slot / request_update / running_version
- OLED 状态提示

## v1.2.2

- DATA 支持重复帧幂等：ACK 丢失后主机重发同一分片，设备直接回当前偏移，不重复写、不重复计入 CRC/SHA。
- START 拒绝在活动槽为 TRIAL 时升级另一个槽，避免擦掉唯一回退点。
- 修正 PROTOCOL.md 错误码表（1 CRC、2 LEN、3 STATE、4 SIZE、5 FLASH、6 VERIFY、7 VERSION、8 SIGNATURE、9 ROLLBACK）。

## v1.2.1

- 修正 IWDG 处理：Bootloader 启动将看门狗重配为 8 s（大于最坏扇区擦除时间），每个扇区擦除前喂一次、擦除后立即补喂；明确擦除期间 CPU 停住、不能在等待循环里喂狗。
- 上板验证：128 K 扇区擦除升级 2.6 s 完成，A/B 均 CONFIRMED。

## v1.1

### 安全

- Bootloader 扩到 32 KB，集成 TweetNaCl，START 携带 Ed25519 签名并验签
- 布局改为 Boot 32K / Meta 0x08008000 / Slot A 0x0800C000 208K / Slot B 256K
- 新增 `tools/gen_key.py`、`--key` 参数；未签名镜像返回 `ERR_SIGNATURE`
- 元数据 v2：加入 `min_version`（防回滚）与 `sec_flags`，CRC 移到 0x5C、覆盖 92 B
- 新增 `INFO` 命令（0x0A）；低于下限的 START 返回 `ERR_ROLLBACK`
- 签名域改为 `version + size + crc + sha256`（44 B），防止改版本号绕过防回滚
- Bootloader 栈 8 KB；IWDG 重配为 8 s，擦除前后喂狗，协议空闲循环持续喂
- 上板实测：签名升级 / 未签名拒绝 / 降级拒绝 / 坏固件回滚全部通过
- 修复：START 先追加 BAD 记录再擦目标槽，ABORT/掉电后不会把空槽当可用；禁止对活动槽 START
- 上板验证：START+ABORT 后 A 显示 BAD，重启自动回滚到 B

### 修复

- `ab_image_verify()` 的 Reset 向量范围改为按镜像实际大小检查，
  避免向量指向镜像末尾之后、仍在槽容量内的擦除区（`ab_image_vectors_ok_size()`）。

### 工程化

- 开源脚手架：LICENSE（MIT）、README、CONTRIBUTING、SECURITY、CHANGELOG
- 协议文档 `docs/PROTOCOL.md`、测试文档 `docs/TESTING.md`
- Python 单元测试 14 项（协议 / 镜像 / 元数据 / 掉电扫描）
- `tools/verify_release.py` + `release/SHA256SUMS`
- GitHub Actions：单元测试 + 掉电仿真 + 发布物校验
- `pyproject.toml`：上位机可作为包安装

### 计划

- 镜像签名与防回滚（v1.1）
- 断点续传（v1.1）
- CMake + GCC 构建（v1.2）
