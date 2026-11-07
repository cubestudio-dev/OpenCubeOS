# WP-AUDIT-01-fix5 — Fix Round Report (P0/P1 residues + WRONG repairs + P2 residuals)

**Round:** WP-AUDIT-01-fix5 · 2026-10-11
**Base tree:** WP-10-AUDIT_P2-fix4 (public mirror of private 8e44699)
**Result commit:** see `git log -1` — 37 files, +2820/−412

## Provenance and honesty statement

The independent fix-verification round (docs/audit/fix-verification/) judged
the 349 claimed fixes: 288 FIXED / 45 PARTIAL / 4 WRONG / 11 DOC-FIXED /
1 NEEDS-RUNTIME, and surfaced NEW-01 (sshd first-run keygen blocking the
shell) plus a set of P0-grade residues. A first attempt to fix these
(P0-fix3 / P1-fix4 sessions) completed with local verification but was lost
to a tooling-environment reset **before its commit step** — no commit or
push of that work ever happened. This round recreates that work from the
documented specifications and extends it to the remaining WRONG items and
tactical P2 residuals.

Every hardware-interface claim in this round was re-verified against
primary sources fetched live (QEMU v10.0.2 sources; Linux v6.10 tg3/igb/
ixgbe/xhci/ehci/ch9/cdc/dma sources) rather than copied from the lost
notes. Four errors in the lost session's documented bcm57xx register map
were caught and corrected this way (see the bcm57xx section below). Where
the lost notes and the fetched sources disagreed, the sources won and the
disagreement is recorded in the code comments and in this report.

## What was fixed

### P0 residues (pre-auth memory safety)

| BUG | File(s) | Fix |
|-----|---------|-----|
| 0010/0011 | net/net_ssh.c | Four kex-path variable-length bounds converted to the subtraction form `len > payload_len - off` (BUG-0016 idiom); the wrap window `[0x7FFFFFFC..0x7FFFFFFF]` is closed |
| 0015 | net/net_sshd.c | `4 + el > plen` → `el > plen - 4` |
| 0017 | net/net_sshd.c | K_S budget `4+276` → `4+279` (worst 283 = cstr 11 + mpint e 7 + mpint N 261 incl. sign byte) |
| 0019 | net/net_sshd.c | slen/mlen validation added; ulen/pwlen/alen/blen/reqlen converted to the subtraction form |
| 0024 | net/net_tls.c | chain_buf split loops (1.3 **and** 1.2 paths) gain `used + dlen > sizeof(chain_buf)` → fail-closed return (no truncation-accept) |
| 0035 | kernel/crypto/crypto_x509.c | `d->len < hdr + l` → `l > d->len - hdr`; host_a401_entry_test +3 boundary vectors (0x84 7FFFFFFA, 0x84 7FFFFFFF, nested 0x84 pair) |
| 0040+0054 | drivers/block/driver_block_blk.c, drivers/usb/driver_usb_msc.c | Both registration entry points reject `sector_size != 512` (honest log, NVMe-precedent fail-closed; the old "0 → default 512" is gone); MSC registration failure reported honestly and the device taken down; new tests/host_blk512_gate_test.c 13/13 (old code 1/13) |
| NEW-01 | net/net_sshd.c | Host-key generation in the background `sshd-hkgen` kthread; `sshd` waits pollably; ^C cancels the wait (-3 benign) while generation continues; re-invocation reuses the wait |

### P1 residues

| BUG | File(s) | Fix |
|-----|---------|-----|
| 0069 (WRONG) | net/net_core.c | SYN_RCVD recycle moved **before** the `state != ESTABLISHED` filter (was dead code); tcptest `synrcvd-recycle` regression = ok |
| 0081 | net/net_tls.c | TLS 1.3 KeyUpdate applied per RFC 8446 4.6.3/7.2 (read-secret roll + rseq=0; request_update=1 → reply KeyUpdate(0) then roll the write side); host_tls13_ks_test +3 KATs → 13/13 |
| 0061 | fs/fs_vfs.c, fs/fs_ramfs.c | VFS-level unlink open-fd gate (cached-children scan → -4 EBUSY; fixes the FAT32-only no-op guard) + ramfs unlink guard; fstest p1fix4 block ALL PASS |
| 0063 | net/net_core.c | net_tcp_send memcpy/rtx publish inside a pushfq/cli window; false check_rto comment corrected |
| 0066 | net/net_core.c | UDP h->length clamped to the IP-delivered length |
| 0072 | net/net_core.c | ICMP RX checksum verified (RFC 792 single fold), drop + count |
| 0055 | drivers/usb/driver_usb_serial.c | CDC-ACM protocols {0x00, 0x01, 0xFF, 0x02}; comment labels corrected per Linux cdc.h (0x01 = V.250/AT, 0x02 = PCCA-101) |
| 0083 | shell/shell.c | Bare `> file` pushes an empty capture (file created/truncated, outer capture untouched); capture hook honors `active` (no writes between end and free) |
| 0086 | shell/shell_cmds_file.c | cp of an empty source creates the empty target |
| 0115 | drivers/block/driver_block_ata_dma.c | init re-entry refused (`g_ctrl_count > 0` → -1) |
| 0092 | shell/shell.c, l1/l1_wp8cd.c | L1 builtin dispatch hook on the live shell_exec_stage path; self-test counts CALLS not argc; stale comments cleaned |
| 0126 | drivers/nic/driver_nic_ixgbe.c | TX completion polls the write-back DD at offset-12 (Linux eop_desc->wb.status verified); honest timeout (tx_errors++, -1) |
| 0128 | drivers/nic/driver_nic_bcm57xx.c | Full tg3-verified rewrite — see below |

### WRONG repairs

| BUG | File(s) | Fix |
|-----|---------|-----|
| 0175 | drivers/usb/driver_usb_ehci.c | The nonexistent PORTSC 13:12 "speed" field (Owner/Power bits) is no longer read; pre-reset LINESTAT==K → LS → PORT_OWNER companion handoff; post-reset PED → HS; honest HCSPARAMS N_CC-gated logging |
| 0335 | drivers/nic/driver_nic_igb.c | RX error handling rebuilt on the advanced-RX split (status 15:0 vs error byte 31:24; drop mask `CE\|SE\|SEQ\|CXE\|RXE` = E1000_RXDEXT_ERR_FRAME_ERR_MASK per Linux v6.10); non-EOP fragments counted and dropped honestly |
| 0251 | userprogs/ush.c | awk trailing-slash off-by-one (`argv[2][pl-1]` → `argv[2][pl]`); host simulation: OLD wrong on 4/6 patterns, NEW 6/6 |

### P2 residuals

| BUG | File(s) | Fix |
|-----|---------|-----|
| 0157 | drivers/usb/driver_usb.c | Hub speed masks restored to USB 2.0 Table 11-21 (0x0200 LS / 0x0400 HS; Linux ch9.h verified); false "previous masks match no hub bit" comment retracted |
| 0148⑤ | drivers/usb/driver_usb_xhci.c | ISO TRB sets TRB_SIA (DW3 bit 31; QEMU TRB_TR_SIA + Linux sia_frame_id verified — the earlier "FrameID=0 means ASAP" reading was wrong and is documented as such) |
| 0339 | drivers/snd/driver_snd_hda.c | hda.bdl=spec encoding `(len>>1)\|1` → `(len<<1)\|1` |
| 0340 | drivers/snd/driver_snd_ac97.c | stream_start rewrites BDBAR + LVI (CR.RR clears both — QEMU reset_bm_regs verified) |
| 0127/0128 (snd) | drivers/snd/driver_snd_sb16.c | DMA page register ch5 0x89 → 0x8B (0x89 is ch6's) and mode byte 0x48 → 0x49 (channel-select bits); Linux asm/dma.h + QEMU i8257 verified |
| 0210 | net/net_core.c | Bounded IPv4 fragment reassembly implemented: ≤4 datagrams, ≤8 fragments each, 5 s eviction, drop-oldest overflow, overlap → reject (Teardrop), exact-dup ignored; the reassembled datagram re-enters the full validation path; 3 netstat counters (`IP frag reassembled/dropped/evicted`) |
| 0217 | net/net_core.c | net_bind same-port conflict → -1 (port-keyed demux documented honestly) |
| 0276 | kernel/core/core_syscall.c | sys_write2 mirrors sys_write's cli-atomic pipe wait window |
| 0294 | kernel/crypto/crypto_core.c/.h | DRBG state guarded by the crypto IRQ window; stale xorshift header comment replaced |
| 0228 | net/net_tls.c, kernel/ota/ota_update.c | TLS 1.2 truncation sets the flag; https_get returns -1 on a truncated body; wget/OTA consumers fail-closed (verified both consumption paths) |
| 0229 | net/net_tls.c | Lucky13 residual: MAC input length no longer depends on pad validity; RFC 5246 full padding-byte equality (constant-time accumulation folded into the MAC failure) |
| 0233 | net/net_tls.c | RFC 8446 4.1.3 DOWNGRD sentinel: a 1.3-capable client that lands on TLS 1.2 with the sentinel aborts (illegal_parameter); deliberate tls12_only mode is exempt (it expects the sentinel); the 0x00 (≤1.1) variant documented as inapplicable (this stack never negotiates below 1.2) |
| hygiene | several | TLS 1.3 seal rc checked; BUG-0170 read-contract consumer (`got == 0` → `got <= 0`) + serial header contract text; core_timer register_periodic irqsave+publish-last; EXTENSIONS_WP07 sector_size row updated; KNOWN_ISSUES section 10 |

## The bcm57xx rewrite (BUG-0128) — and the four map corrections

The lost session's notes documented a register map "verified against Linux
tg3.h/tg3.c". Re-verification against the actual Linux v6.10 sources found
**four errors in those notes**, which are corrected in this commit:

1. The RX return-ring **consumer mailbox is 0x280** (RCV_JUMBO_PROD_IDX is
   0x270, RCV_MINI_PROD_IDX 0x278) — the notes' "0x380" is wrong.
2. **SNDBDI is at 0x1800**, not "0x6000".
3. "RCV_CONFIG 0x100" does not exist — **0x100/0x200 are NIC-SRAM RCB
   offsets** reached via the 0x7c/0x84 windows.
4. Mailbox registers are 64-bit with the **low dword at base+4** (e.g. the
   std producer doorbell is 0x26c, the TX doorbell 0x304).

The rewrite: 16-byte tg3 descriptors; std/TX rings at the firmware-fixed
512 entries with a 32-buffer posting budget; 64-entry chip-written return
ring; RX driven by return-ring polling on the DMA'd status block (HOSTCC
with NOW refresh pokes, tg3's own self-test pattern); TX completion via the
status-block tx_consumer with honest timeout; init mirrors tg3_reset_hw
step-for-step, class-branched on the real ASIC rev (5705+ vs 5700); 4GiB
DMA tripwire on every allocation. **Not run on real hardware; QEMU has no
bcm57xx model — source-verified only, disclosed in the file header.**

## Verification

- `make clean && make` → **0 errors / 0 warnings** (-Wall -Wextra -Werror);
  `make iso` → 11.47 MB, BIOS + UEFI El Torito
- Regression harness (tools/run_fix4_regression.py): **28/28** — boot,
  exception self-test 3/3, 176 commands, heaptest/fstest/cryptotest/
  crashlog, l1test G7+G8, all 16 user programs
- tcptest: **ALL PASS (syn-opts=ok parser=ok synrcvd-recycle=ok)**
- fstest: **ALL PASS** incl. the new p1fix4 regressions
- dhtest: **5/5** (group-1 self-tests, group-14 truth vectors, 6-length
  scale sweep) — the BUG-0349 NEEDS-RUNTIME item stays green
- Interactive QEMU: bare `> file` creates the empty file; `cp` of an empty
  source creates the empty target; `sshd` + ^C returns to the shell in
  < 20 s while `ps` shows `sshd-hkgen` READY and the shell stays usable;
  netstat shows the new IP-frag counters
- Host KATs: host_a401_entry_test **12/12** (ASAN, incl. the 3 new
  boundary vectors); host_tls13_ks_test **13/13** (build line documented;
  `-fno-builtin` is required at -O2 — the kernel string prototypes differ
  from glibc builtins and GCC miscompiles the mismatch into a pre-main
  crash; documented in the test header); host_blk512_gate_test **13/13**
  (old code 1/13)
- ixgbe / igb / bcm57xx / EHCI / xHCI-ISO / SB16-DMA changes: source-level
  verification only (fetched Linux/QEMU sources, cited line-level in code
  comments); no runtime model exists in QEMU for bcm57xx; the others are
  runtime-covered only insofar as the default QEMU devices exercise them

## Known residuals (honest)

- NIC drivers with no QEMU model (bcm57xx, ixgbe, igb on real I210) remain
  source-verified only.
- The sshd first key generation under TCG is slow (> 9.5 min measured
  lower bound); it no longer blocks the shell (NEW-01) but the first
  server start on a fresh /etc volume still has to wait for the background
  thread (KNOWN_ISSUES §10.1).
- BUG-0226 (sshd concurrency = 1) remains a documented design decision.
- Release-chain P2 items (SHA256SUMS coverage, website i18n badge
  generation lag, about-page copy) are repo-hygiene items on the private
  repo's release machinery and were not addressable from the public
  mirror; they remain open for the release process.
