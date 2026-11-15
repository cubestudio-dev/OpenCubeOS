<!-- SPDX-License-Identifier: Apache-2.0 -->
# 常见错误 — 高频清单

你实际会撞上的错误，按频率从高到低，每条按
**症状 → 原因 → 修复**组织。这是短清单；完整的
逐子系统清单（含排查链）是
[error-catalog.md](error-catalog.md)；什么都不打印的失败是
[silent-failures.md](silent-failures.md)。

## 日志与 watchdog 错误

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `logcat: bad level (debug/info/warn/error/fatal or 0-4)` | 级别拼写错误 | `logcat -l warn` 或 `-l 2` |
| `klog: flush FAILED (no filesystem mounted?)`（rotate/export 孪生） | /var/log 没有卷支撑 | 挂载一个；`logctl flush` 随后写入 `/var/log/kernel.log` |
| `logctl: bad level ('-' = any)` | 过滤语法 | `logctl filter <clear\|[level] [module] [keyword]>` |
| dmesg 页脚显示 `dropped <N>` | 1024 条环形已回绕 | `dmesg 1024`；考虑 `logctl threshold` + `flush` |
| 你期待的消息从不出现 | 级别被禁或过滤器生效 | `logctl status`；`logctl test` 校准管线（5 行，模块 `klogtest`） |
| `watchdog: register FAILED (table full?)` | 最多 8 槽 | `watchdog slots`；先停一个 |
| `watchdog hw: start FAILED (bad timeout?)` | TCO 范围是 2..63 s | `watchdog hw start 30` |
| `watchdog hw: feed FAILED (not armed?)` | start 之前就 feed | 先 `watchdog hw start` |
| `health: export FAILED (no filesystem mounted?)` | 导出目标缺失 | 挂载一个卷；`health export /mnt/health.txt` |

## TUI 与显示错误

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `tui` 页面经脚本不打印任何内容 | TUI op 走 DIRECT 路由，绕过捕获 | 交互式运行 `tui`；只有 `tui-test` 经捕获提供文本 |
| `page '%s' failed (rc=%d)` / `tui: desktop init failed` | 桌面内部问题 | `dmesg 100`；附串口捕获报告 |
| `value out of range (1980-2099, ...)`（时间页） | RTC 设置防护 | 设置合法日期 |
| `volume: device rejected the level`（硬件页） | 混音器钳制 | 用 0-100 |
| 编辑器 `[save failed]` | 只读文件系统或部分缓冲 | 检查 `mounts`；`[PARTIAL: save refused]` = 超限文件 |
| 中文显示为按键或豆腐块 | locale/编码不匹配 | `lang zh`、`serial_encoding utf8`（与你的终端匹配） |

## OCSE 安装器错误（来自安装介质）

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `ocblmbr.bin MISSING` / `ocos.tar MISSING` | 介质不完整 | 重新下载 ISO；核对大小/SHA |
| `install: FATAL - extraction failed` | ocos.tar 损坏 | 同上 |
| `mkpart failed` / `created partition not found` | 目标磁盘问题 | 先跑磁盘检查；换一块盘 |
| `install: FATAL - could not set the boot flag` | 分区表写入失败 | 重跑；检查介质 |
| 安装后安装器设置被忽略 | BUG-129 配置方言分裂 | 从运行中的系统再次设键：`config set shell.lang zh` |
| OCSE GUI 点击无效 | BUG-113 | CLI 热键 Ctrl+Alt+F2，或 `click <x> <y>` 命令 |

## 电源错误

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `Suspend not supported (ACPI required).` | 机器无 ACPI 表 | 某些 VM/老主板属预期 |
| `S3 resume failed.` | 恢复路径损坏 | 报告；有数据丢失风险 — 先保存 |
| `System halted.` 然后没有别的 | 这就是 halt 成功消息 | 在宿主机侧断电 |

## 恢复环境（OCIRE）错误

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `Supervisor: no OCIRE recovery volume found` | hd1 0xEE+FAT12 卷缺失 | 带恢复分区重新安装（OCSE 会创建） |
| `logread: BAD ring header (magic/cursor)` | 引导日志环形损坏 | `repair quick` 重置它 |
| `WARNING: log agent digest mismatch` | 代理镜像与邮箱摘要不符（防篡改） | `syscheck`，然后 `repair` |
| 修复期间 `REBUILD FAILED` / `RESET FAILED` | 黄金集或介质损坏 | 重跑；换一个黄金集 |
| `oc>` 中 `Unknown command: <cmd>` | 恢复 shell 很小 | `help` 列出它；隐藏附加：`lang [en\|zh]` |

## 万用第一步

```
ush> health          # alarms: memory >90%, tasks >64, CPU >=90%x10s
ush> dmesg 100       # the kernel log tail (ring 1024)
ush> crashlog        # panics you may not have seen (32-slot ring)
```

---

## Shell 与文件工具

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `ush: command not found: <cmd>` | 拼写错误，或程序不在注册表中 | `help -a`（或分组的 `help -w`）；查 [../manual/function-dictionary.md](../manual/function-dictionary.md) |
| `ush: syntax error near '>'`（或 `'<'`） | 空重定向目标 | 给出文件名：`cmd > file` |
| `ush: 'exit' is not available; ...` | 按设计 — ush 是启动项 | `shutdown` / `reboot` |
| `ush: job control requires external programs` | `fg`/`bg` 被识别但未实现 | 用 `jobs` 列出；没有恢复功能 |
| `cat: cannot open <path>`（同 head/tail/sort/uniq/wc/awk） | sys_open 失败：文件缺失、cwd 错误 | `pwd`、`ls`；路径相对 shell cwd |
| `mkdir: cannot create '<path>'` | 已存在，或父目录缺失 | 先创建父目录 |
| `rm: cannot remove '<path>'` / `rmdir: ...` | 文件忙（打开的 fd），或目录非空 | 关闭它；`ls` 该目录 |
| `ln: cannot create symlink (target must be an absolute path)` | 符号链接目标仅限绝对路径 | `ln -s /abs/target link` |
| `sed: unknown script` | 只有 `s/old/new/[g]`、`-n Np`、`Nd` | 见 [../manual/ush-reference.md](../manual/ush-reference.md) |
| `awk: missing {print ...}` | 微型 awk 仅支持 `{print ...}` | 同上 |
| `help: unknown option '<o>'` | 只有 `-a`/`--all`、`-w`/`--wp` | `help -a` |
| `run: unknown program: <name>` | 注册表查找失败 | `help -a`；注册表在构建时冻结为 284 |
| `ls *` 列出名为 `*` 的字面文件 | **任何地方都没有 globbing**（ush 和 ls） | 显式给出文件名 |
| `disk-tool: unknown op: <op>`（任意 mega 应用） | 逐字表未命中 | 该应用的 `usage:` 列出其表；`help -w` 分组全部命令 |
| `<app>: service call failed (rc=-1)`（250 个应用共享） | L1SVC 桥返回 -1 | 重跑；`dmesg 100`；该家族的 `_test` 桩；然后 [error-catalog.md](error-catalog.md) §7.1 |
| nano/vi 退出后屏幕残留 | BUG-111（编辑器残影） | `clear` |
| 屏幕上中文为豆腐块（串口正常） | BUG-110/130 控制台回退 UTF-8 | 当前构建已修复；更新镜像 |
| 编辑器：`[PARTIAL: save refused]` | 文件超过 256 行 × 128 字节 | 按设计（BUG-34 修复）；拆分文件 |

## 网络

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `DHCP failed (timeout)` | 无 OFFER/ACK — user-net 缺失、线缆或服务器 | `ip`（0.0.0.0?），再 `dhcp`；QEMU：`-netdev user,id=n1 -device e1000,netdev=n1` |
| `DNS resolution failed` / `(timeout)` | 无 DNS 服务器，或 UDP 被阻断 | 重跑 `dhcp`（它会设置 DNS）；`dns <host>` 测试；见 [network-issues.md](network-issues.md) |
| `ping` 先不打印然后失败 | 名称解析先失败（"Resolving..."） | `dns <host>`；或直接 ping IP |
| `connect failed` | 路由/防火墙 | `route`（网关在吗？）、`firewall` 列表，试验时 `firewall reset` |
| `wget: cannot create file, printing to console:` | 目标路径不可写 | 检查 `pwd`/挂载；正文仍打印到控制台 |
| `TLS failed (code <n>)` / `[tls] x509 verify failed: ... REFUSED` | 信任决策 — 自签名/无锚链**默认**被拒 | 修正服务器证书；仅测试的选择加入：`config set tls.insecure 1` |
| `[ssh] HOST SIGNATURE INVALID - disconnecting` | TOFU 不匹配 | `[ssh] WARNING: HOST KEY CHANGED!` 表示必须刻意重新信任 `/etc/ssh_known_hosts` 中的条目 |
| `[ssh] USERAUTH_FAILURE — wrong password` | 凭据 | 默认用户 `oc` / 密码 `oc`；公钥 = 密码 `-` 加 `/etc/ssh_client_key` |
| `sshd` 拒绝启动：`cannot obtain a per-installation host key` | 无密钥且无法生成 | 预置 `etc.img`（`tools/host_keygen_etc.py`），或释放内存重试（首次启动会后台生成） |
| 首次连接 sshd 停滞 | DHCP 后 ARP 缓存冷 | 先热身：`ping <peer>` |
| `e1000e: init failed`（任意 `<nic>: init failed`） | 驱动探针失败 — 卡缺失、BAR 损坏、DMA 不可达 | `lspci` → `pcidump <b:d.f>` → 家族状态 → 家族 `_test`；模板见 [network-issues.md](network-issues.md) |
| `virtio TX timeout: avail_idx=...` | virtio NIC 停止消费描述符 | 重启 VM/设备；若持续则报告 |
| `wifi scan` 不打印网络 | QEMU 无射频 — 如实输出 | 真实硬件，或跳过 |

## 存储与文件系统

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `mount: failed` / `vfs_mount: unknown fs type` | 类型/设备/参数错误，或该 fs 不受支持 | `lsblk`、`mounts`；类型：ramfs/fat32/exfat/ext4 |
| `fat32: not a FAT32 volume (FAT12/16 detected)` / `invalid BPB` | 设备节点错误，或不是 FAT32 | `fatstat <dev>` 显示解析的 BPB；节点错？`lsblk` |
| `ext4: unsupported INCOMP feature bits ... mount refused` | ext4 驱动只读且有特性限制 | 挂载更简单的 ext4，或在别处读取 |
| `umount: target is busy - close open files first` | 打开的 fd | 关闭它们；`umount: unmount nested mounts first` → 先子挂载（`mounts` 显示嵌套） |
| `umount: cannot unmount the root filesystem` | 按设计 | — |
| `fsck: not a FAT32 volume (fsck supports FAT32 only; ...)` | fsck 仅支持 FAT32 | 在别处修复；`fatstat` 确认 |
| `parted: device not found` / `mkfs.*: device not found` | 设备字符串错误 | 先 `lsblk`；名字必须精确 |
| `mkfs.exfat: device too small (need at least 65 sectors, got <N>)` | 目标过小 | 更大的设备 |
| `grub-install: MBR gap too small for <N>` | MBR 后间隙放不下 core.img | 以 ≥ 1 MiB 间隙重新分区（或 GPT） |
| `install: disk too large for MBR (>2TiB) or too small` | MBR 限制 | 用 ≤ 2 TiB 的目标 |
| 重启后写入丢失 | 它们活在 ramfs 上 — 只有 /etc 和已挂载卷持久 | `mounts` 显示什么持久；电源操作前 `sync` |
| U 盘出现后又消失 | MSC 拔出路径 | `usb_hotplug_test` 复现；重新插拔 |
| `blk: write to protected device ... REFUSED (ocire recovery disk)` | 整盘 OCIRE 保护（仅 klog 行） | 按设计 — 仅从 OCIRE `repair` 修改该卷 |
| `abdisk` 之后 A/B 磁盘"absent" | 服务缓存尚未刷新 | 重跑 `update status`；向导会循环复查 |
| `blkcache: alloc FAILED - cache disabled (raw I/O)` | 缓存表分配失败；系统继续无缓存 | `mem`；若持续则报告 |

## 更新 / OTA

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `update` 不打印 / "up to date" 但你预期有更新 | 六因静默链 | 走 [silent-failures.md](silent-failures.md) §7；或直接跑向导：`update`（无参数） |
| `update: cannot determine target slot (not booted from A/B?)` | 从 ISO 引导，无 A/B 布局 | `update status`；`abdisk <dev>` 创建（destructive） |
| `update: download failed: <why>` | 网络 / 服务器 | `<why>` 是 OC_UPDATE_E_* 文本 — [error-codes.md](error-codes.md) §1 |
| `SHA256 mismatch` / `package (tar.gz) invalid` | 下载损坏 | 重新下载；核对大小 |
| `config: write rejected (empty, non-ASCII, too long, or invalid value)` | 从严的值防护 | `config list` 看格式；仅 ASCII 值 |
| `config: cannot read /etc/opencube.conf` | /etc 未挂载 | [storage-issues.md](storage-issues.md) |

## 内核 / 引导 / 崩溃

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `*** KERNEL PANIC: <reason> ***` | 11 个可能位点 — 原因串指名它 | [error-codes.md](error-codes.md) §panic-sites；串口是权威；重启后 `crashlog` |
| 机器"自己"重启 | 策略为 REBOOT 的 panic、watchdog、或 OCBL 判定停滞 | `crashlog`、`dmesg`、`/var/log/crash.log`；OCBL 侧：[boot-failures.md](boot-failures.md) |
| 整机冻结，无 panic（快速连打 'a'/'u'，或 `loop`） | BUG-109 冻结竞态 / BUG-112 loop 遏制缺口 | 捕获 `-serial file:` 并报告；避免与控制台输入赛跑 |
| `[user] process table full (max <N> processes)` | 进程过多 | `ps`；`kill <pid>` |
| `[user] not an ELF file` | exec 目标不是 ELF64 | 对 badapp 类固定件属预期；检查二进制 |
| `KERNEL PANIC (no crash handler installed): <reason>` | 崩溃服务注册前的 panic | 早引导 bug 类；捕获串口，报告 |
| 引导停在 `FAIL` 阶段行 | 该子系统初始化失败 | [boot-failures.md](boot-failures.md)；FAIL 之上的消息指名设备 |
| `Power off not supported. System halted.` | 电源端口穷尽（VM） | 使用宿主机电源控制 |
| `sleep: invalid time` | 非数值/负参数 | `sleep 2`、`sleep 0.5` |

## 音频 / USB / 杂项硬件

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `play: device error` / `: unknown device '<name>'` | 无此音频设备 | `sound` 列出已注册设备 |
| `[audio_rw_test] no sound cards registered => SKIPPED` | 未接音频设备 | QEMU：`-device intel-hda`；否则如实跳过 |
| QEMU hda 在 44.1 kHz 冻结 | QEMU-10 模型死循环（源内有记载） | 真实硬件正常；其他采样率可用 |
| `usb: SET_ADDRESS failed` / `GET_DESCRIPTOR(...) failed` | 设备未枚举 | 重新插拔；`usb`，然后 `usb_storage_test` |
| `virtio-blk: write rejected (device is read-only)` | QEMU 驱动器是 readonly=on | 按设计；去掉该 flag |
| `no hardware watchdog present` 行 | 无 WDAT/TCO — 探针如实 | q35 有 TCO；默认 pc 没有 |
| self-IPI 通过但 `msitest: FAIL` | MSI 路由损坏 | `pcibars`、`pcie`，报告 |
| `date: ms since boot (no RTC driver)` | 无 CMOS RTC（VM 默认） | QEMU 中 `-rtc base=...`；信息性 |

## 构建期（宿主机）

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `grub-mkimage: cannot open ... unicode.pf2` | 字体在 grub-common 中 | 安装它 / 让 OC_TOOLS 指向含 `usr/share/grub/unicode.pf2` 的树 |
| `src-zip: not a git checkout` | 无 git 元数据 | 从克隆构建或用 `make dist` |
| `error: operand size mismatch for 'setc'` | 新 GAS 上的修复前代码树 | 更新越过 u8-flag 修复 |
| 新 GCC 上的 packed-member 警告 | 修复前代码树 | 更新（packed 标签类型） |
| `embed_grub: missing /usr/usr/lib/grub...` | 修复前脚本 | 更新（双布局探测） |
| `chinese_font gz layout mismatch - regenerate ...` | 字体 blob 与头文件漂移 | 重新生成 chinese_font_data.c |
| 某个 `_Static_assert` 触发 | 跨模块布局契约漂移 | 断言消息指名两侧 — [error-codes.md](error-codes.md) §static-asserts |

## 测试门

- SSH E2E 需要预置的主机密钥（[../dev/e2e-suites.md](../dev/e2e-suites.md)
  中的 mtools 配方）。
- HTTPS E2E 第 0 项要求自签名链被拒绝 — 如果
  连上了，检查 `tls.insecure` 为 0。
- 回归套件需要 pexpect；SSH 套件需要 paramiko。
- 串口抖动？见 [silent-failures.md](silent-failures.md) §8 和
  [qemu-debugging.md](qemu-debugging.md) §errata。

## 下一步去哪

| 如果错误是... | 前往 |
|--------------------|------|
| 数值代码或 panic 横幅 | [error-codes.md](error-codes.md) |
| 来自特定子系统的消息串 | [error-catalog.md](error-catalog.md) |
| 完全不打印 | [silent-failures.md](silent-failures.md) |
| 引导期间 / OCBL / OCIRE 中 | [boot-failures.md](boot-failures.md) |
| 网络形状 | [network-issues.md](network-issues.md) |
| 磁盘/文件系统形状 | [storage-issues.md](storage-issues.md) |
| 仅在 QEMU 中 | [qemu-debugging.md](qemu-debugging.md) |
| 你找不到的命令 | [../manual/function-dictionary.md](../manual/function-dictionary.md) |
