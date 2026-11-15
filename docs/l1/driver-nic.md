<!-- SPDX-License-Identifier: Apache-2.0 -->
# NIC and wireless drivers

How a network family joins the net service.

## The model (3.0.0)

- Drivers live in `ocos/drivers/net/` - one file per family
  (`drv_net_e1000e.c`, `drv_net_igb.c`, `drv_net_ixgbe.c`,
  `drv_net_rtl8139.c`, `drv_net_rtl8169.c`, `drv_net_bcm57xx.c`,
  `drv_net_other.c`), plus the wireless stack
  (`drv_net_wireless.c`, `drv_net_ath.c`, `drv_net_brcm.c`,
  `drv_net_iwlwifi.c`, `drv_net_rtlwifi.c`).
- Identity: `OC_DRIVER_DECL("nic", "e1000e", &ops, "Intel e1000e")`
  - or the `"wireless"` class for 802.11 families. The boot
  collector probes; families self-report honest states (registered
  / scanning / connected).
- The driver-internal table API is `drv_net_api.h` - kernel
  internal, mutated only from the family's own probe paths.
- The net service (`ocos/services/net/`) owns the stack: IPv4 +
  TCP/UDP, DHCP, DNS, the firewall/netfilter, TLS, SSH/sshd, and
  the L1SVC category 3/8 surfaces.

## The wireless layer

`wifi` (status / scan / test / sdio) reports per-family state and
beacon-derived scan lists; `bt` (status / usb / uart / test) fronts
the Bluetooth transports (HCI-USB in `drv_misc_bt_usb.c`, H4 UART,
the full L2CAP/SDP/RFCOMM/SPP chain). No hardware -> the honest
"no wireless devices registered" line, never a fake list.

## What a new family looks like

1. `ocos/drivers/net/drv_net_<fam>.{c,h}` - PCI probe, MAC setup,
   RX/TX into the service's packet path.
2. `OC_DRIVER_DECL("nic", "<fam>", &ops, "description")`.
3. Optional status/test op: APPEND the number in
   `svc_sysapi_l1svc.h`, regenerate the mirror, add the classic
   table row (`net-tool`'s `g_ops[]`) or a thin wrapper folder.

## The consumer contract

Apps call the service, never drivers: `net-tool ip` ->
`sys_l1svc(NET, IP, ...)`; `ping`/`wget` ride the `SYS_NETCMD`
bridge (op 2 / 4). The output lines (`"IP: ..."`,
`"DHCP success: IP=..."`, `"DNS resolution failed (timeout)"`)
are the anchors the update wizard and the docs rely on - see
[surface-map.md](surface-map.md).
