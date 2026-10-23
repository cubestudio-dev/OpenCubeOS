# EXTENSIONS — WP-10-wireless_bluetooth

Work package: **WP-10-wireless_bluetooth** (18th work package).
Scope: 802.11 core + seven wireless family drivers + a full Bluetooth
stack + an SDIO bus, with two new shell commands (`wifi`, `bt`) and
host-side byte-level test suites.

Interface rule unchanged: **the numbered L1 interface set stays closed
at 1-138** — this batch adds kernel-internal driver APIs and shell
commands only (chipset / timer_rtc precedent).  Everything below is a
kernel-internal contract with a header, comments, and real
implementations.

## 1. New shell commands (2; total 192 -> 198, additions only)

| Command | Forms | What it does |
|---|---|---|
| `wifi` | `wifi` | wireless registry status: family list + per-device bus/vid/did/state (honest `down` = device not present) |
| | `wifi scan` | starts a passive scan; prints the beacon-derived network list (empty without a radio - honest) |
| | `wifi test` | 802.11 core selftest + every registered family's selftest; prints one PASS/FAIL line per check |
| | `wifi sdio` | SDHCI/SDIO bus status (state/hosts) + bus selftest (CMD52/CMD53 encodings, card states) |
| `bt` | `bt` | transport + controller status (real HCI Read_BD_ADDR probe when not yet queried) + L2CAP channel table |
| | `bt usb` / `bt uart` | per-transport status lines |
| | `bt test` | the full flow against the attached transport: Reset -> Read_Local_Version -> Read_BD_ADDR -> Set_Event_Mask -> Inquiry -> Create_Connection -> L2CAP echo -> SDP SPP channel -> RFCOMM DLC -> SPP echo -> HID report -> disconnect; every verdict comes from real controller/peer responses |

## 2. New kernel driver APIs (header + comments + default behavior)

### 802.11 core — drivers/wireless/driver_wireless.h

- `driver_wireless_init()`, `wl_register_family()`, `wl_register_device()`,
  `wl_device_probe_all()` — family/device registry; bring-up is honest:
  no hardware or no firmware leaves the device DOWN.
- `wl_scan_start()/wl_scan_result_get()` — beacon-derived scan list
  (dedup by BSSID).
- `wl_connect()/wl_disconnect()` — open-system authentication +
  association state machine (clause 9.3.3.6/9.3.3.10); the Association
  Request is sent by the RX path after a real Authentication Response.
- `wl_rx_frame()/wl_tx_frame()/wl_netif_recv()` — RX dispatch and the
  Ethernet bridge (LLC/SNAP per clause 9.3.3.3) with a heap-allocated
  decap ring.
- Format helpers: `wl_parse_frame_ctl()`, `wl_parse_beacon()`,
  `wl_walk_ie()/wl_find_ie()`, `wl_build_probe_req/auth/assoc()`,
  `wl_parse_auth_resp/assoc_resp()`, `wl_eth_to_80211_data()`,
  `wl_80211_data_to_eth()`, `wl_channel_to_mhz()`.
- `driver_wireless_selftest()` — 28 vector checks on the production
  code (also run host-side: tests/host_wifi_test.c, 38/38).

### Family drivers (each registers a `wl_family_ops_t`)

- Intel: `driver_iwlwifi_register()` — driver_iwlwifi.c/h.  PCI ids
  0x08b1-0x08b4/0x0950-0x0951/0x3165/0x24f3/0x24fd/0x2526/0x2723/
  0x2725/0x02f0/0xa0f0; CSR register block (HW_IF_CONFIG/RESET/
  GP_CNTRL/HW_REV/EEPROM_GP/GIO_CHICKEN_BITS per the 7000-series
  datasheet + iwl-csr.h names), MAC access handshake, ucode container
  parsers (legacy "UCOD" five-section + TLV 0x0a4c4657 walk),
  firmware load through the FH SRVC channel (TFDIB/TCSR).
- Realtek: `driver_rtlwifi_register()` — driver_rtlwifi.c/h.  USB
  vendor-request register protocol (bRequest 0xc5 read / 0x05 write,
  wValue=address) per rtl8xxxu; PCI ids 0x8179/0x8178/0x818b/0x8193/
  0x8812/0x8821/0xb723, USB ids 0x8179/0x818b/0x8812/0x0821/0x172a;
  8188E register map, power-on flow, efuse read, 8051 firmware chunk
  download, 40-byte TXDESC encoding, USB bulk TX.
- Atheros: `driver_ath_register()` (three families) — driver_ath.c/h.
  ath9k: AR9002/9003 register map, RTC wake, AR5416 TX/RX descriptors
  with DMA pages; ath10k: CE descriptors, HTC framing, BMI commands,
  WMI-TLV encoder; ath11k: MHI ring elements, QMI TLV encoder.
- Broadcom: `driver_brcm_register()` (two families) — driver_brcm.c/h.
  b43: SSB-over-PCI BAR0 core windowing (config offset 0x80), ucode+
  pcm firmware requirement; brcmfmac: PCIe ids 0x43ba/0x43ec + SDIO
  client on the SDHCI bus (Broadcom SB address windows, vendor 0x02d0).

### SDIO bus — drivers/sd/driver_sdhci.h

- `driver_sdhci_init()` — real SDHCI per the SD Host Controller
  Standard Specification (reset/caps/power/clock with stability polls,
  command engine, CMD0/CMD5/CMD8/ACMD41 probe sequence).
- `driver_sdio_rw_direct()/rw_extended()` — CMD52/CMD53 client API.
- `driver_sdio_register_driver()` — SDIO client registry (brcmfmac
  binds here).  QEMU's `sdhci-pci` makes this path real emulated
  hardware; with no card attached the state is honestly `empty`.

### Bluetooth — drivers/bluetooth/driver_bt.h

- `bt_stack_init()` — whole-stack init; transport pick: live HCI-USB
  dongle first, else the H4 UART on COM2; honest no-transport state.
- HCI core: `bt_hci_cmd()` (synchronous command engine over the
  transport; async commands complete on their own events),
  `bt_hci_reset/read_bd_addr/read_local_version/set_event_mask/
  write_scan_enable/inquiry_start/inquiry_results/create_connection/
  disconnect/remote_name/read_rssi/send_acl/poll`.
- H4 UART transport: `bt_h4_init/probe/ops()` — 16550 loopback probe,
  115200 8N1, IRQ3-driven RX ring, per-type H4 frame reassembly.
  The IRQ ring is the ONLY FIFO consumer (a poll-time direct drain
  would race it); the RX trigger is level 1 because QEMU's 16550 has
  no receive-timeout interrupt.
- HCI-USB transport: `bt_usb_init/present/ops()` — btusb class driver
  (interface class 0xE0/1/1): commands on the control pipe, events on
  interrupt IN, ACL on bulk (Core 5.x vol 4.B).
- L2CAP: `bt_l2cap_connect/disconnect/echo/send/register_profile()` —
  signaling with a response-box dispatcher; connect is idempotent per
  handle+PSM.
- RFCOMM: `bt_rfcomm_set_peer/open/close/send/recv_register/fcs()` —
  TS 07.10 mux, DLCI = channel<<1|1, PN+MSC negotiation, real FCS
  (reflected CRC-8 poly 0x07 init 0xff; addr+ctrl for UIH,
  addr+ctrl+len otherwise).
- SDP: `bt_sdp_service_search/get_spp_channel()` — ServiceSearch +
  ServiceAttribute with a real (attr-id, value) pair walker.
- SPP: `bt_spp_connect/send/recv_register/echo()`.
- HID: `bt_hid_connect/get_report/last_report/disconnect()` — control
  PSM 0x11 + interrupt PSM 0x13, boot-protocol reports.

## 3. Config keys

None added (the batch needs no user-tunable configuration; firmware
files are looked up at fixed paths /etc/firmware/* and are NOT shipped
- third-party property).

## 4. Honest notes / depth statement

1. **Wireless families are datasheet-verified, not hardware-verified.**
   QEMU has no 802.11 device model at all.  Per the work-package test
   matrix ("QEMU 实测（如果有模型）/ datasheet 对照") the drivers are
   verified at code-vs-datasheet level: byte-exact fixtures run through
   the production parsers/builders in the in-kernel selftests (`wifi
   test`), the host suites (tests/host_wifi_test.c 38/38,
   tests/host_bt_test.c 22/22), and the PCI/USB/SDIO enumeration paths
   execute live in QEMU (honest not-present).  Vendor firmware blobs
   are third-party property and are NOT shipped; without one,
   fw_load fails honestly and the device stays DOWN.
2. **Bluetooth IS hardware-exercised end-to-end in QEMU**: the second
   serial port carries real H4 bytes; tools/bt_test_peer.py implements
   the controller half + the remote device (L2CAP/SDP/RFCOMM/SPP/HID);
   `bt test` passes 0-failures on BIOS and UEFI
   (tools/wp10_wireless_bluetooth_e2e.py, 32/32 each).  The HCI-USB
   transport has no QEMU model (usb-bt-dongle was removed from QEMU
   7.1), so it is covered by the btusb class driver claiming on the
   live USB stack + the transport framing sharing the HCI core paths.
3. **Register maps are transcribed from public sources** (Intel CSR
   map, Realtek 8188E spec tables, ath9k ar5416reg.h, the SD Host
   Controller spec).  The HCI/L2CAP/RFCOMM/SDP wire formats are
   spec-exact and proven against the peer on the wire.
4. **ath10k/ath11k/brcmfmac data paths stop at their bus protocols**
   (CE/HTC/BMI/WMI-TLV, MHI/QMI, SDIO windows): without the vendor
   firmware the higher layers cannot run - the drivers report honest
   refusals instead of pretending to transmit.
5. `bt` status prints the controller address after a REAL Read_BD_ADDR
   probe; before the first probe it honestly reports not-present.
6. The shell `wp` tag field was widened 24 -> 32 bytes for the new
   batch name (24 could not hold "WP-10-wireless_bluetooth" + NUL; the
   help -w group would have silently fallen into the fallback bucket).
