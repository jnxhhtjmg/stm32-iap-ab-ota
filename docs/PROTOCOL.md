# 协议说明（v2）

## 帧格式

```
偏移  长度  字段
0     2     sync = AA 55
2     1     ver
3     1     type
4     2     seq（小端）
6     2     len（小端）
8     len   payload
8+len 4     crc32（小端，覆盖 ver..payload）
```

- 多字节字段一律小端；
- CRC32 为 IEEE 802.3（zlib 同款）：poly 0xEDB88320 反射，init/xorout 0xFFFFFFFF；
- 响应帧：`type | 0x80`，`seq` 原样返回。

## 命令

| 命令 | 码 | 请求 payload | 响应 payload |
| --- | --- | --- | --- |
| HELLO | 0x01 | 空 | `proto, version, active_slot, state` |
| START | 0x02 | `slot(1) + pad(3) + version(4) + size(4) + crc32(4) + sha256(32) + sig(64)` | `status, slot, offset(4)`；验签后先把目标槽标记为 BAD，再擦除；同 sha/version/size 的 DOWNLOADING 记录存在时直接返回已写偏移；目标为活动槽、或活动槽仍为 TRIAL 时返回 `ERR_STATE` |
| DATA | 0x03 | `offset(4) + data(≤508)` | `status, written(4)`；重复帧直接回 ACK；每 4 KB 追加 DOWNLOADING 记录 |
| END | 0x04 | `crc32(4) + sha256(32)` | `status, slot, version(4)` |
| STATUS | 0x05 | 空 | `count + count × (slot, state, version, size)` |
| CONFIRM | 0x06 | 空 | `status` |
| REBOOT | 0x07 | 空 | `status`，随后复位 |
| SET_BAUD | 0x08 | `baud(4)` | `status` |
| ABORT | 0x09 | 空 | `status` |
| INFO | 0x0A | 空 | `status, proto, boot_version(4), active, state, version(4), min_version(4), size(4), attempts(2)` |

`START.slot = 0xFF` 表示“自动选非活动槽”。

## 状态码

| 值 | 名称 | 含义 |
| --- | --- | --- |
| 0 | OK | 成功 |
| 1 | ERR_CRC | 帧 CRC 不符 |
| 2 | ERR_LEN | 长度非法 |
| 3 | ERR_STATE | 当前状态不允许 |
| 4 | ERR_SIZE | 镜像大小非法 |
| 5 | ERR_FLASH | Flash 操作失败 |
| 6 | ERR_VERIFY | 整镜像 SHA/向量表校验失败 |
| 7 | ERR_VERSION | 版本号非法 |
| 8 | ERR_SIGNATURE | Ed25519 签名无效 |
| 9 | ERR_ROLLBACK | 版本低于 min_version |

## 升级时序

```
主机                        设备
HELLO  ──────────────────>  回 HELLO
START  ──────────────────>  擦目标槽，回 OK(slot, 0)
DATA#0 ──────────────────>  写 508 B，回 OK(508)
DATA#1 ──────────────────>  ...
END    ──────────────────>  校验 CRC/SHA/向量，追加 TRIAL，回 OK
REBOOT ──────────────────>  回复后复位
                            下次启动：TRIAL → attempts=1 → 跳新槽
```

每帧等 ACK，超时重发 3 次。

## 重复帧与丢 ACK

主机重发同一 DATA 帧时，offset 会小于设备已写偏移。设备按以下规则处理：

| 条件 | 行为 |
| --- | --- |
| `offset == written` | 正常写入，返回新的 written |
| `offset < written` 且 `offset + n <= written` | 重复帧：不重写、不重复计入 CRC/SHA，直接回 `OK(written)` |
| `offset < written` 但尾部超出 written | 部分重叠，返回 `ERR_STATE` |
| `offset > written` | 有空洞，返回 `ERR_STATE` |

这样 ACK 丢失时，主机重发不会把数据写两遍，也不会让整镜像 CRC 算错。

另外，活动槽为 TRIAL 时 START 会被拒绝：此时另一个槽是唯一回退点，不能擦。

## 断点续传

- START 时，如果目标槽最新记录是 `DOWNLOADING`，且 `sha256 / version / img_size` 与本次请求一致，
  设备**不擦除、不写 BAD**，直接返回 `written` 偏移；
- 设备从 Flash 重新计算已写前缀的 CRC32/SHA-256，然后从该偏移继续接收 DATA；
- DATA 每跨过 4 KB 边界追加一条 `DOWNLOADING` 记录（`written` 记为边界值），
  最多重发不到 4 KB 的已写数据，由重复帧规则吞掉；
- 掉电/复位后，Boot 看到最新的 `DOWNLOADING` 记录：不启动目标槽，停在升级模式等续传；
- END 校验通过后写 `TRIAL`，`DOWNLOADING` 被新记录覆盖；
- `sha256 / version / img_size` 任一不同，视为新升级：标 BAD、擦槽、从 0 开始。

## 元数据记录（96 B）

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0x00 | 4 | magic = "ABIF" |
| 0x04 | 2 | rec_size = 96 |
| 0x06 | 1 | slot（0=A, 1=B） |
| 0x07 | 1 | state（1=TRIAL, 2=CONFIRMED, 3=BAD, 4=DOWNLOADING） |
| 0x08 | 4 | seq（单调递增） |
| 0x0C | 4 | version |
| 0x10 | 4 | img_size |
| 0x14 | 4 | img_crc32 |
| 0x18 | 2 | attempts |
| 0x1A | 2 | flags（bit0=请求升级） |
| 0x1C | 32 | sha256 |
| 0x3C | 4 | min_version（防回滚下限） |
| 0x40 | 4 | sec_flags（bit0=已验签） |
| 0x44 | 4 | written（DOWNLOADING 已写字节） |
| 0x48 | 20 | 保留 |
| 0x5C | 4 | 记录 CRC32（覆盖前 92 B） |


扫描规则：

```
从 0x08008000 起每 96 B
magic == 0xFFFFFFFF → 空片，停
magic/长度/CRC 不符   → 写到一半，停
seq 取最大者          → 当前状态
```

## 跳转约定

- `VTOR = 槽基址`；
- `MSP = *(槽基址)`；
- `PC = *(槽基址 + 4)`，最低位必须为 1；
- 跳转前关闭 SysTick、清 NVIC 使能与挂起。

## 兼容性

- `ver` 不同：直接拒绝；
- START 的 64 字节签名为 Ed25519(`version|size|crc|sha256` 共 44 B)，版本号也被签名覆盖；
- API 表新增字段只能追加，`AB_API_VERSION` 递增；
- 元数据 `rec_size` 变化视为不兼容。
