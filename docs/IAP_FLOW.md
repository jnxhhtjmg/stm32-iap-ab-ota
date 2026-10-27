> 注意：v1.1 起 Bootloader 扩为 32 KB（Ed25519 验签），本文描述的是 v1.0 布局；最新地址见 README。

# IAP 流程框图（一页看完）

> 配套：[IAP_DESIGN.md](IAP_DESIGN.md)。本文只放图，文字压到最少。

---

## 图 1：谁住在哪（静态）

```
 0x08000000  +----------------------------+
             | (2) our bootloader  16 KB  |  <-- reset vector, never erased
 0x08004000  +----------------------------+
             |     metadata log    16 KB  |      96 B x 170 records
 0x08008000  +----------------------------+
             | (3) app slot A     224 KB  |      link base 0x08008000
 0x08040000  +----------------------------+
             | (3) app slot B     256 KB  |      link base 0x08040000
 0x08080000  +----------------------------+

 0x1FFF0000  +----------------------------+
             | (1) ST ROM bootloader      |      BOOT0=1 only, rescue
             +----------------------------+
```

| # | 谁 | 谁把 CPU 交给它 | 作用 |
| --- | --- | --- | --- |
| 1 | ST ROM bootloader | BOOT0=1 的硬件 | 工厂烧录 / 救砖 |
| 2 | 我们的 bootloader | BOOT0=0 的硬件 | 决策 + 升级 + 跳转 |
| 3 | 应用 A 或 B | 我们的 bootloader | 干活 |

---

## 图 2：上电到 main 的启动链

```
 power on
    |
    v
 [hardware]  MSP = [0x08000000] , PC = [0x08000004]
    |
    v
 [startup]   Reset_Handler -> SystemInit (FPU, VTOR) -> __main
    |
    v
 [C library] copy .data flash->sram , zero .bss
    |
    v
 [main]      your code
```

---

## 图 3：bootloader 的决策树（核心）

```
 read latest metadata record
 |
 +-- [no record] -----------------> A slot vectors ok ?
 |                                    +-- yes --> run A   (factory state)
 |                                    +-- no  --> RECOVERY (wait UART)
 |
 +-- [TRIAL] ----------------------> image CRC+SHA ok ?
 |                                    +-- no  -------------> ROLLBACK
 |                                    +-- yes --> attempts >= 3 ?
 |                                                 +-- yes --> ROLLBACK
 |                                                 +-- no  --> attempts++ , run slot
 |
 +-- [CONFIRMED] -----------------> run slot
 |
 +-- [BAD] -----------------------> ROLLBACK

 then:
 |
 +-- request magic / flag / PE3 pressed ----> UPDATE MODE (UART loop)
 +-- valid frame within 400 ms ------------> UPDATE MODE
 +-- otherwise ----------------------------> jump to active slot
```

ROLLBACK 展开：

```
 rollback(failed_slot)
   |
   +-- other slot has a record ? --no--> RECOVERY
   +-- other image CRC ok ? ------no--> RECOVERY
   +-- append CONFIRMED(other)
   +-- jump other
```

---

## 图 4：一次升级的时序

```
 PC tool                        bootloader                     flash
   |                                |                            |
   |--- HELLO --------------------->|                            |
   |<-- HELLO_ACK (slot,state) -----|                            |
   |                                |                            |
   |--- START(ver,size,crc,sha) --->|                            |
   |                                |--- erase target slot ----->|
   |<-- START_ACK(slot) ------------|                            |
   |                                |                            |
   |--- DATA(offset, 512 B) ------->|--- program 256 B buffer -->|
   |<-- DATA_ACK(next offset) ------|                            |
   |            ... repeat ...      |                            |
   |                                |                            |
   |--- END(crc, sha) ------------->|--- verify CRC32 + SHA256   |
   |                                |--- append TRIAL record --->|  (meta sector)
   |<-- END_ACK(ok) ----------------|                            |
   |                                |                            |
   |--- REBOOT -------------------->| reset                      |
```

关键点：

- START 就擦目标槽（早失败）；
- 每帧带 offset，可重传；
- END 才写 TRIAL，校验不过不记账。

---

## 图 5：TRIAL / CONFIRMED / ROLLBACK 状态机

```
                     download + verify ok
   CONFIRMED(A) -----------------------------> TRIAL(B)
        ^                                        |
        |                                        | boot: attempts++
        |                                        v
        |                                 +--------------+
        |                                 | app selftest |
        |                                 +------+-------+
        |                              pass      | crash (IWDG)
        |                                        | or 3rd boot
        |                                        v
        |                                  +-----------+
        |                                  | ROLLBACK  |
        |                                  +-----+-----+
        |                                        |
        +----------------------------------------+
                   append CONFIRMED(A)
```

时间关系：

```
 boot            0 s          3 s                 4 s
  |---------------|------------|-------------------|
  bootloader      app selftest  confirm()          IWDG timeout
                                 (if pass)          (if hang)
```

---

## 图 6：跳转到底改了什么

```
        bootloader state                  application state
   +------------------------+        +------------------------+
   | MSP   = boot stack     |        | MSP   = [slot base]    |
   | VTOR  = 0x08000000     |        | VTOR  = slot base      |
   | PC    = jump_to()      |        | PC    = [slot base+4]  |
   | SysTick ON, IRQ ON     |        | SysTick OFF, IRQ OFF   |
   +-----------+------------+        +-----------^------------+
               |                                 |
               |  SCB->VTOR = base               |
               |  __set_MSP([base])              |
               |  branch [base+4]                |
               +---------------------------------+
```

跳过去后，应用的 `Reset_Handler` 会再走一遍图 2 的启动链。

---

## 图 7：断电发生在哪一步，结果是什么

```
 phase            flash after power loss        next boot
 ---------------  ----------------------------  ---------------------
 before START     unchanged                     old slot
 during erase     target slot blank             old slot
 during DATA      target partial                old slot
 after END check  target complete, no record    old slot
 after TRIAL      record -> new slot            new slot, attempts=1
 after CONFIRM    record -> new slot, confirmed new slot, no count
 during rollback  record -> old slot            old slot
```

不变量：

```
 every moment:  (old slot intact)  OR  (last valid record intact)
```

---

## 图 8：四个升级入口

```
                     +---------------------+
   app request ----->|                     |
   meta flag ------->|    UPDATE MODE      |---> HELLO/START/DATA/END
   PE3 button ------>|  (never returns    |
   400 ms window --->|   until reboot)     |
                     +---------------------+
```

---

## 图 9：两个工程如何产出三个固件

```
 bootloader/Src ----[f407_iap_boot.uvprojx]----> f407_iap_boot.hex  -> 0x08000000
 app/Src ----------+--[app_slot_a]--------------> app_slot_a.hex    -> 0x08008000
                   +--[app_slot_b]--------------> app_slot_b.hex    -> 0x08040000
```
