<!-- SPDX-License-Identifier: Apache-2.0 -->
# 无线（Wi-Fi）

## 层

`drivers/wireless/driver_wireless.c` - 系列核心；
`driver_ath.c`（Atheros）、`driver_brcm.c`（Broadcom）、
`driver_iwlwifi.c`（Intel）、`driver_rtlwifi.c`（Realtek）- 卡片系列；
通过 SD 层连接的 SDIO 卡片。

## 命令

```
oc> wifi    (status / scan / test / sdio - the 802.11 families +
              the SDIO bus)
```

## 实际状态

该层涵盖状态、扫描原语和各系列的探测路径；
真实硬件上的完整关联取决于卡片系列（固件握手深度不同）。
在 QEMU 中没有可连接的 Wi-Fi 设备模型 -
命令报告无设备并正常退出。
