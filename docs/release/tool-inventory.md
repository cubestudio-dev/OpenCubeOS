<!-- SPDX-License-Identifier: Apache-2.0 -->
# tools/ inventory — all 117 files (maintainer guide)

The complete classification of `tools/` (117 files: 95 `.py` + 16 `.sh`
+ 6 data/C files). One line of responsibility per file, taken from the
file headers. Sections sum to 117 exactly. Mechanics of the guarded
ones: [guard-scripts](guard-scripts.md); build targets that invoke
them: [build-reference](build-reference.md).

| Section | Count |
|---------|-------|
| 1. Build pipeline (shell + tar packer) | 17 |
| 2. Source & data generators | 12 |
| 3. Font & i18n generators | 7 |
| 4. Docs & evidence generators | 3 |
| 5. Architecture & determinism guards | 6 |
| 6. E2E / regression suites | 46 |
| 7. Hostile security peers (p0/p0fix3) | 9 |
| 8. Test peer servers | 4 |
| 9. Historical one-shot restructure | 7 |
| 10. Data & C files | 6 |

## 1. Build pipeline (17)

| File | Responsibility |
|------|----------------|
| `build.sh` | POSIX one-command entry: dep detection (gcc/ld/nasm/grub-mkimage/xorriso/mformat/mcopy/python3/zip, each with its Debian package), `--install` apt fallback, `OC_TOOLS` autodiscovery, target dispatch, and the WP-13 spec-8 scriptable API (`build_all/build_iso/build_src/build_ota/config_load/config_save/config_get/cache_lookup/cross_compile/cross_toolchain_setup`) |
| `build_iso.sh` | OCOS release ISO: stages the dual-firmware OCBL pair (BIOS stage1+core + UEFI PE) + kernel + ocire.elf through the OCBL ISO packer; G13 determinism seals delegated to the packer → `build/opencube.iso` |
| `build_ocire_iso.sh` | standalone OCIRE recovery ISO (OCBL boots it; scanner class = volume carrying `/boot/ocire.elf`) → `build/ocire/ocire.iso` |
| `build_ocse_iso.sh` | OCSE install-medium ISO: runs `check_bss.py` on the ELF FIRST, stages `/ocse.mrk` marker + `/boot/ocse.elf` → `build/ocse.iso` |
| `build_release.sh` | the one-command release SOP steps 1–5, gated: clean `make all` (0-warning) → `reproducible-check` → BIOS+UEFI boot banner gates (fresh mktemp log per run, BUG #63 fix) → optional `--deep` gates → docs/website data; prints the three SHA256s; refuses an existing tag or wrong git identity; pushes nothing |
| `make_update_pkg.sh` | OTA package: `manifest.json` (version + files + payload sha256 over concatenated payload bodies in archive order) + kernel + etc + docs; no boot-cfg member since WP05 (OCBL scans dynamically) |
| `make_ocos_tar.py` | deterministic OCOS.TAR install payload (boot/opencube.elf + boot/ocire.elf + etc/opencube.conf; ustar, zero mtimes, sorted) |
| `make_ab_disk.sh` | A/B update disk `build/abdisk.img`: p1 boot/flags 64M FAT32 + slot A 128M + slot B 128M + data 128M; mtools-only (no loop, no root); seeds slot A |
| `make_ocire_disk.sh` | WP-02 dual-boot test disks: hd0 GRUB dual-menu (OCOS volume + recovery entry), hd1 OCIRE disk (0xEE shadow, no GPT header, FAT12: golden files + MANIFEST.TXT SHA-256 + agent + 64 KiB log ring) |
| `install-toolchain.sh` | no-root toolchain: downloads Debian .debs in 3 batches (build tools / qemu / grub+firmware; ~117 packages incl. deps) and `dpkg -x`-extracts to `$OC_OPT` (default `$HOME/opt/extract`): nasm 2.16.03, qemu-system-x86 10.0.13 (+data+libs), xorriso 1.5.6, grub-pc-bin/efi-amd64-bin/common, mtools, ovmf, seabios |
| `env-sandbox.sh` | 3-line environment for the extract layout: exports `OC_TOOLS=$HOME/opt/extract`, PATH, LD_LIBRARY_PATH |
| `oc_cc.sh` | content-addressed compile cache wrapper: key = SHA-256(compiler identity + args + source hash + header-content hashes from `-MM`); hits restore `.o`+`.d` byte-identically (reproducible-check stays valid); `OC_CACHE_OFF` bypasses, `OC_CACHE_DIR` relocates, counters append to `STATS` |
| `oc_menuconfig.sh` | dependency-free interactive `.config` editor (number=toggle, s=save, q=quit, r=reset) |
| `vendor_ocse.sh` | re-vendor OCSE from tag `v1.0.0` (143 files: kernel core/lib/mem/arch, drivers acpi/block/display/i2c/input/pci/time/usb, fs, boot, l1, chinese) |
| `github_release_wp08.sh` | WP-08-era GitHub Release helper: pushes commit+tag, creates the Release with the ISO asset via the API (`GITHUB_TOKEN`) |
| `update_website.sh` | reads dist assets → writes `website/assets/site-data.js` + `website/update.json` + `update-v2.json` (OTA manifest); idempotent per release, nothing hand-edited |
| `sync_wiki.sh` | docs/ (EN) + docs/zh/ → GitHub Wiki: zh canonical page name, EN as `<Page>-EN.md`, language banners, `[[Wiki-Link]]` rewriting per language, assets stripped; the ~83-entry `map_page()` table is the contract |

## 2. Source & data generators (12)

| File | Responsibility |
|------|----------------|
| `gen_userprogs.py` | full regeneration of `userprogs_data.h`: folder apps + legacy flat programs + `_shared/` skipped + `libfoo.c`→solib; emits byte arrays + the `OC_USERPROGS[]` registry (the execve surface); purges orphaned blobs; duplicate names fatal (see [guard-scripts](guard-scripts.md) §4) |
| `build_c_userprog.py` | compile one C userprog + refresh its blob; modes: `exec` (user.ld @0x400000), `pie` (ET_DYN + PT_INTERP=/lib/ld.so), `pie-foo` (+DT_NEEDED libfoo.so), `ld_so` (ld_so.ld @0x10000000); verifies e_type/PT_INTERP per mode |
| `build_solib.py` | the solib leg: ET_DYN `-Wl,-soname,<name>.so`, refreshes `l1/solib_data.h` (`solib_libfoo`) |
| `embed_userprog.py` | compile a `.asm` userprog (NASM) and append its bytes to the generated header |
| `embed_grub.py` | stage-2 generator: GRUB boot.img (512 B copy) + core.img (grub-mkimage with the modules the on-disk cfg needs + embedded early config) → `grub_boot_data.c`; the kernel ELF is NOT embedded (multiboot2 module via grub.cfg) — the in-kernel installer payload |
| `embed_roots.py` | embed the public CA roots (ISRG X1/X2, GlobalSign, GTS R1/R4, DigiCert, Baltimore, Amazon, Microsoft, Sectigo E46/R46) as DER arrays into `crypto_roots.h` |
| `gen_l1svc_ops.py` | mirror the kernel `L1SVC_*` numbering from `svc_sysapi_l1svc.h` verbatim into `ocos/interface/l1svc_ops.h` (append-only renumbering contract) |
| `gen_svc_api_headers.py` | emit one `svc_<name>_api.h` umbrella per service; idempotent, hand-migrated umbrellas preserved verbatim |
| `gen_gzip_vectors.py` | gzip/deflate test vectors for the kernel updater self-tests (`update_test_vectors.h`) |
| `make_wdat_table.py` | synthetic-but-valid ACPI WDAT table for QEMU `-acpitable` injection, driving the REAL q35 TCO registers (the WP-12 E2E gate for WDAT discovery) |
| `host_keygen_etc.py` | generate RSA-2048 host/client keys on the HOST into `etc.img` (n‖d, 256 B each, e=65537) so the guest skips minutes-long TCG keygen |
| `ocse_edits.py` | the single source of truth for every OCSE vendored-source edit (E1 OTA API stripped, E2 GRUB wrapper out, E3 net stack not vendored, E4 crash plumbing stubbed, E5 ring-3 kill removed); never hand-edit vendored files |

## 3. Font & i18n generators (7)

| File | Responsibility |
|------|----------------|
| `gen_font.py` | 8×16 ASCII 0x00–0xFF bitmap font (16 rows of 8 bits, bit 7 = leftmost) → `screen_font_data.c` (`screen_font8x16[256][16]`) |
| `gen_chinese_font.py` | 16×16 GB2312 font from GNU Unifont outlines (rectangle-union rasterization, upem=64, no anti-aliasing): 6763 hanzi (zones 16..87) + 682 symbols + zone indexes + unicode→GB map → `ocse/chinese/chinese_font_data.c` |
| `gen_font_gz.py` | wrap the six Chinese font tables into one gzip blob — identity-window relief: the kernel must end below 0x400000 (user address spaces leave 0x400000–0x600000 empty); ~298 KiB of raw `.rodata` becomes a lazily-inflated stream |
| `gen_l0_i18n.py` | `etc/i18n/l0/{en,zh}.json` (49 keys each) → key-sorted lookup tables in `core_lib_i18n_data.c` for the `l0_tr_()` binary search |
| `gen_pinyin_phrase.py` | `tools/ime_phrases.txt` → pinyin-sorted phrase table (longest-match-first); tone-free pinyin composes from the kernel single-char table — no pypinyin (CI clean-env fix) |
| `apply_i18n_hints.py` | write the `_hint` (en, zh) pair into every program's `i18n/{en,zh}.json` — hand-authored dictionary for all 274 programs |
| `apply_tool_i18n.py` | route the file-tool apps' user-facing sentences through `tr_()` (gettext model: key = English sentence; ZH keys carry REAL newlines — the v1 literal-`\n` keys never matched) |

## 4. Docs & evidence generators (3)

| File | Responsibility |
|------|----------------|
| `gen_api_docs.py` | the docs data generator: parses the runtime `help -a` capture (registry header + `name [tag] hint` rows; serial-echo overlap tolerated) or live `--qemu` capture → `docs/data/{commands,interfaces,stats}.json`; `--pages`/`--pages-zh`/`--md` regenerate the 292 command reference pages (EN+ZH); v1 baseline fallback `evidence/v1_command_list.txt` |
| `qemu_shot.py` | run QEMU → wait → screendump → kill (PPM output) |
| `qemu_shot_vnc.py` | VNC variant writing PNG via PIL — the engine behind `make shot-bios` / `make shot-uefi` |

## 5. Architecture & determinism guards (6)

Deep-dive on each in [guard-scripts](guard-scripts.md).

| File | Responsibility |
|------|----------------|
| `check_deps.py` | dependency-direction gate over `ocos/{kernel,drivers,services,l1apps}`: kernel must not include drivers/services (composition root excepted), drivers must not include services, l1apps only `ocos/interface/` headers; plain-basename resolution against the real file inventory; exit 1 with file:line on any violation |
| `check_bss.py` | no regular `.bss` variable may land in `[__boot_pml4, __ocse_work_start)` (page tables + boot stack); a 64 KiB buffer on the live PML4 once triple-faulted the boot; wired into `build_ocse_iso.sh` |
| `v2_registry_gate.py` | v2 group-B freeze: boot the declaration-collected kernel, verify the command surface (231 `help -a` lines / 233 total / collection log / service-group view) |
| `fat_zero_dates.py` | zero every FAT12/16 date/time field in an mtools image (mformat/mcopy stamp wall clock) so two builds minutes apart stay byte-identical |
| `iso_zero_ctime.py` | rewrite Rock Ridge TF ctime/atime stamps to 1970 epoch in every ISO9660 directory record (xorriso hardcodes ctime to assembly time; atime leak fixed in WP04-fix1) |
| `fix_file_headers.py` | rewrite stale `* File:` header comments pointing at pre-restructure paths (comment-only) |

## 6. E2E / regression suites (46)

| File | Responsibility |
|------|----------------|
| `qemu_runner.py` | shared QEMU+pexpect driver: boots the ISO, runs shell commands (`@` prefix = HMP monitor over `mon:stdio`), `strip_ansi()`, `boot_to_recovery()`, `screen_assert_text()` (BUG #66: PPM screendump must show ≥2 colors and a measurable foreground ratio) |
| `run_host_tests.py` | `make test` backend: extracts the gcc line from each `tests/host_*.c` header, remaps pre-restructure paths, compiles + runs; SKIP-FIXTURE when openssl fixtures are absent |
| `run_fix4_regression.py` | the 31-check full regression gate (see [ci-gates](ci-gates.md) §6 for the floor mechanics) |
| `run_fix4_https_e2e.py` | the 8-check HTTPS E2E gate (TLS 1.2 + 0x0067 cipher, close_notify recorder, truncation guard) |
| `ssh_g11a_e2e.py` | the 5-check SSH E2E gate (guest→host paramiko + guest sshd ← host client) |
| `run_l1_per_app.py` | P-08 per-app record: boots the ISO once per program (fresh process table per row), runs it via `run <name>` |
| `run_ocire_tests.py` | OCIRE standalone battery: boot/help/unknown/backspace/shutdown × BIOS+UEFI |
| `run_ocire_dual_tests.py` | WP-01 dual-boot + partition isolation (GRUB menu boots OCOS or OCIRE; 0xEE shadow disk untouched) |
| `run_ocire_shell_tests.py` | WP-02 group-4 recovery shell battery on throwaway disk copies (digest damage, nuked-BPB full repair) |
| `run_ocire_supervised_tests.py` | WP-03 G4 OCIRE-supervised boot (60 s watchdog + mailbox + agent) |
| `wp07a_state_e2e.py` | boot-state monitor gate: S1 normal stages 1..6 + 45 s no-reset + isolation walk, S2 user_request → OCIRE, S3 hang_boot diagnosed by the 30 s budget, …, S8 UEFI smoke under OVMF |
| `wp11_crash_e2e.py` | crash recovery: #DE/#UD/#PF self-test, sigsegv/stackovf/badapp, crashlog flush to /var/log, dump + dumpinfo |
| `wp12_watchdog_logging_e2e.py` | 14 scenarios S1–S14: klog levels/switches, logcat filters, dmesg replay, logctl, persistence+rotation `.1` chains, global filter, Intel TCO real reset + second boot, ACPI WDAT, policy kill, health JSON |
| `wp10_native_e2e.py` | native desktop flows |
| `wp10_native_manual.py` | re-runs the FLAPPING checks as isolated fresh-QEMU scenarios (QEMU drops serial RX bytes after HMP screendumps — verified with an in-kernel RX trace) |
| `wp10_fix2_render_e2e.py` | pixel-verify TUI hanzi render — full 16×16 glyphs against the embedded font bitmaps |
| `wp10_chinese_shot.py` | drive font-test/tui-cn, screendump, compare CJK glyph blocks against embedded bitmaps |
| `wp10_chipset_e2e.py` | chipset/input command surface: input→expected→actual→verdict records (BIOS or UEFI) |
| `wp10_timer_rtc_e2e.py` | time-source surface: CMOS RTC / HPET / ACPI PM timer / TSC |
| `wp10_wireless_bluetooth_e2e.py` | wireless + BT surface; BT is real E2E over COM2 H4 bytes into `bt_test_peer.py` |
| `wp10_camera_misc_e2e.py` | UVC 30/30, parallel 10/10 (SPP bytes verified in the chardev file), gameport 6/6, IR 14/14 (NEC/RC5/RC6/SIRC), SCSI vs LSI 53C895A |
| `wp10_wp08fix1_test.py` | line editing (oc> and ush) + ush tools + nano editors; serial for text, HMP sendkey for arrows |
| `wp_p2_batch_a_test.py` | batch A live probes (read0 #14, pipe slot-leak #9, …) via the `p2test` app |
| `test_p1fix3_runtime.py` | BUG-0133 `umount /` refused, BUG-0134 run/kill tid + latency cap, BUG-0135 vi modal editor |
| `tui_g11c_e2e.py` | the eight full-screen TUI pages: launch, HMP screendump PNG proof, Esc leave, prompt back |
| `ime_g11b_e2e.py` | `ime -i` compose over the PS/2 queue via HMP sendkey ("ni" + digit 1 → 你) |
| `fix1_before_ush.py` | WP04-fix1 BEFORE evidence capture (raw-byte unix-socket serial channel so control bytes arrive verbatim) |
| `fix1_before_tui.py` | same, TUI side |
| `fix1_after_ush.py` | AFTER evidence capture (ush) |
| `fix1_after_ush2.py` | AFTER evidence capture, round 2 |
| `fix1_checkupdate_e2e.py` | shipped ISO's checkupdate sees the real live feed, honest up-to-date (version-inversion regression) |
| `fix1_ocire_kbd_scroll.py` | OCIRE PS/2 keyboard scroll proof |
| `fix1_ocire_vga_scroll.py` | OCIRE VGA-text scroll proof (pmemsave of the 0xB8000 cell array, frozen-band analysis, no OCR) |
| `fix1_ocire_vga_scroll2.py` | VGA scroll proof, round 2 (screendump-based) |
| `wp01_gui_click_test.py` | BUG #113: REAL pointer clicks via QMP `input-send-event` (press-edge latch) |
| `wp01_killrace_test.py` | BUG #5: kill/fork race stress, prompt responsive after every round |
| `wp01_p0leak_test.py` | P0 user-pointer fence probe (kernel-address sys_write/read must be fenced) |
| `wp01_section_dump.py` | differential triage driver: run a list of ush commands, dump each section verbatim |
| `wp06g3_baseline.py` | encoding-fix battery: baseline captures |
| `wp06g3_repro_nano_scroll.py` | nano-scroll/serial-enc repros |
| `wp06g3_repro_serial_enc.py` | serial-encoding repro |
| `wp06g3_test_editor_rework.py` | editor rework (zh header legend, IME preedit discipline, no-scroll render) |
| `wp06g3_test_install_ocire.py` | install-with-OCIRE-partition |
| `wp06g3_test_ocire_menu.py` | OCBL menu OCIRE entry |
| `wp06g3_test_ocse_enc.py` | OCSE encodings (utf8/gbk) |
| `wp06g3_test_serial_enc.py` | `serial_encoding` command surface |

## 7. Hostile security peers (9)

The p0/p0fix3 hostile-server drivers (the P0 security batches; case
matrix client-side a04/a05, sshd-side a09/a13/a38, TLS a18_chain_*):

| File | Responsibility |
|------|----------------|
| `p0_paramiko_evil.py` | BUG-0013 SSH overflow driver (hostile paramiko server) |
| `p0_evil_tls_server.py` | BUG-0022/0023/0024 TLS record overread servers |
| `p0_redir_server.py` | BUG-0021 oversized redirect path server |
| `p0_run_p0_case.py` | p0 case runner |
| `p0_run_tls_case.py` | p0 TLS case runner |
| `p0fix3_evil_tls.py` | chain_buf wrap (p0fix3 batch) |
| `p0fix3_run_case.py` | p0fix3 case runner (matrix incl. `OC_ETC_IMG` pre-seeded host-key fast path) |
| `p0fix3_new01_test.py` | first-run host-key gen: non-blocking notice, ^C cancel, background completion, persistence |
| `p0fix3_new01b_test.py` | second-boot fast path |

## 8. Test peer servers (4)

| File | Responsibility |
|------|----------------|
| `paramiko_sshd.py` | host SSH server: logs the negotiated DH shared secret K (cross-check vs kernel dhtest), password oc/oc, exec channel → /bin/sh |
| `https_test_server.py` | one-request OpenSSL HTTPS server matched to the kernel client (TLS 1.2 only, 0x0067 DHE-RSA-AES128-SHA256, 1024-bit RFC 3526 DH, self-signed; `observe` mode = raw-byte close_notify recorder, `eof` mode = hard-close truncation server) |
| `update_server.py` | update-manifest server (HTTP or TLS) for checkupdate tests; loops for a whole QEMU session |
| `bt_test_peer.py` | H4 BT controller + virtual remote (L2CAP/SDP/RFCOMM-SPP/HID) on COM2 |

## 9. Historical one-shot restructure (7)

Retained for provenance; they hardcode historical absolute roots
(a historical checkout outside this tree) and are NOT part of any current pipeline.

| File | Responsibility |
|------|----------------|
| `restructure_move.py` | WP-10-project_restructure phase 1: git-mv per the new layout |
| `restructure_includes.py` | phase 1b: include rewrites |
| `restructure_build_map.py` | phase 2a: symbol rename map build |
| `restructure_apply_map.py` | phase 2a-final: map apply |
| `restructure_rename_iface.py` | phase 2: interface renames |
| `nak_migrate.py` | the NAK restructure engine: old→new map, uniqueness invariants, `git mv` + include rewrites (map ambiguity / duplicate targets = failure) |
| `nak_fix_refs.py` | post-move reference fixer for the NAK line |

## 10. Data & C files (6)

| File | What it is |
|------|-----------|
| `host_gzip_test.c` | host-side gzip inflate test program |
| `ocire_fat_host_test.c` | host-side OCIRE FAT driver test |
| `ocse_kmain_c` | the OCSE kmain.c vendoring payload (copied by `vendor_ocse.sh`) |
| `ime_phrases.txt` | the IME phrase list (word + explicit pinyin for polyphonic entries) |
| `nak_map.tsv` | the NAK restructure old→new map (77 KB) |
| `update_test_vectors.h` | committed updater test vectors (generated by `gen_gzip_vectors.py`) |

## 11. Reading order for a new maintainer

1. `build.sh` + the `Makefile` header — how anything builds.
2. `build_release.sh` — how a release is gated (then
   [process](process.md), [ci-gates](ci-gates.md)).
3. `qemu_runner.py` — the shared E2E substrate every suite assumes.
4. `gen_userprogs.py` + `gen_api_docs.py` — the two registries that
   make the system self-describing (embedded programs; command docs).
5. The guards in [guard-scripts](guard-scripts.md) before touching
   layering, `.bss` layout, or determinism.

Known inventory gaps (documented, not hidden): there is no committed
mega-app smoke script — the 21/21 gate quoted in the WP-l1apps-merge
release notes is a referenced rig; the Phase A/B checklists to
re-create it are in `docs/dev/e2e-suites.md`. `oc_menuconfig.sh`'s
menu text still advertises the removed `CONFIG_TUI`/`CONFIG_CHINESE`
knobs (the Makefile consumes only NVME/AHCI/VIRTIO_BLK).
