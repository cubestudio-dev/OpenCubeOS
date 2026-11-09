<!-- SPDX-License-Identifier: Apache-2.0 -->
# ACPI

## 驱动程序

`drivers/acpi/driver_acpi.c` - RSDP 发现（EFI 上为 multiboot2 标签 14/15，BIOS 上为内存扫描），RSDT/XSDT，FADT，DSDT，MCFG。
`driver_acpi_aml.c` - 一个真实的 AML 命名空间遍历器（`acpidev` 命令列出它找到的命名空间）。
`driver_acpi_power.c` + `arch_acpi_s3*.S` - S3 挂起/恢复。

## ACPI 提供的内容

- 电源控制（FADT PM1a_CNT）：`acpipow`，关机/重启路径。
- WDAT 看门狗表（E2E 验证的激活 + 真实复位）。
- GPE/SCI 事件。
- S3 挂起路径（suspend/suspend_test 命令；恢复路径通过汇编跳板返回）。

## 命令

```
oc> acpi        (table dump: RSDP/FADT/DSDT/MCFG)
oc> acpidev     (AML namespace devices)
oc> acpitest    (parser self-test)
oc> acpipow     (power control status)
```

## 点亮注意事项

- q35 与 i440fx 在表布局上存在差异 - 两者均可解析。
- AML 遍历器是防御性的：格式错误的表会停止遍历并显示消息，而不是引发故障。
