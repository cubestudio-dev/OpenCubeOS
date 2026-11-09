<!-- SPDX-License-Identifier: Apache-2.0 -->
# Wireless (Wi-Fi)

## The layer

`drivers/wireless/driver_wireless.c` - the family core;
`driver_ath.c` (Atheros), `driver_brcm.c` (Broadcom),
`driver_iwlwifi.c` (Intel), `driver_rtlwifi.c` (Realtek) - the card
families; SDIO-attached cards through the SD layer.

## Command

```
oc> wifi    (status / scan / test / sdio - the 802.11 families +
              the SDIO bus)
```

## Honest state

The layer covers status, scanning primitives and per-family probe
paths; full association on real hardware depends on the card family
(firmware handshake depth differs). In QEMU there is no Wi-Fi device
model to attach - the commands report no-device and exit cleanly.
