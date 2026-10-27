# 安全说明

## 当前边界

本项目已实现**完整性 + 签名 + 可回滚**：

- 镜像有 CRC32 和 SHA-256，能发现传输/写入错误；
- START 携带 Ed25519 签名，私钥不在设备里；
- `min_version` 单调递增，低于下限的旧固件在 START 阶段被拒绝。

## demo 密钥

`keys/demo_signing.key` 是公开的演示私钥，任何人都能签镜像；
它只用于跑通流程，生产必须用 `tools/gen_key.py` 生成新密钥并重编 Bootloader。

## 生产环境建议

1. 打开 Option Bytes 的 WRP，保护 Bootloader 扇区；
2. RDP Level 1，关闭调试口读取；
3. 元数据增加单调版本号，拒绝降级；
4. 生产密钥离线保存，`keys/dev_*` 只用于开发；
5. 关闭 `SET_BAUD` 等调试命令，或加鉴权。

## 报告漏洞

请勿公开提 issue，邮件联系维护者，附复现步骤与影响范围。
