<!-- SPDX-License-Identifier: Apache-2.0 -->
# 构建与打包（开发者参考）

构建系统的开发者视角。用户级指南见
[BUILDING.md](../root/BUILDING.md)；工具链设置见
[environment.md](environment.md)。以下事实来自顶层
`Makefile`（740 行）、`tools/build.sh` 与各组件的 `.mk`
文件。

## 1. 你实际会用到的目标

```
make -j$(nproc) kernel     # fast inner loop: ELF only (default goal)
make iso                   # + bootable dual-firmware ISO
make all                   # userprogs + kernel + ISO + src zip + OTA
make userprogs             # regenerate the embedded program registry
make userprogs-check       # staleness gate (wired into make dist)
make test                  # host unit tests (no QEMU needed)
make run-bios | run-uefi | run-bios-gui | run-uefi-gui
make run-bios-persist      # no -snapshot: /etc edits persist in etc.img
make shot-bios | shot-uefi # VNC screendump -> PNG
make reproducible-check    # two clean builds -> byte-identical ELF
make defconfig | menuconfig
make cache-stats | cache-clear
make l0-i18n | api-docs
make ocire-kernel | ocbl-bios | ocbl-efi | ocse-elf   # sibling kernels
make ARCH=arm64 | ARCH=riscv                            # bring-up shells
make clean
```

## 2. Makefile 变量（含隐藏变量）

| 变量 | 默认值 / 推导 | 含义 |
|---|---|---|
| `OC_TOOLS` | `/usr` | 迁移（dpkg -x）树的工具链根；由 `tools/build.sh` 探测 `/usr`、`/home/z/tools/grub`、`$HOME/tools/grub` 自动检测 |
| `OVMF_FD` | `$(OC_TOOLS)/share/ovmf/OVMF.fd` | `run-uefi*` 目标的 UEFI 固件 |
| `QEMU_DATADIR` | `$(OC_TOOLS)/share/qemu` | QEMU `-L` 路径 |
| `SEABIOS_DIR` | `$(OC_TOOLS)/share/seabios` | BIOS 固件 |
| `OC_RELEASE_VERSION` | 固定字面量（如 `3.0.0-NAK`） | 内核横幅 / uname / 更新检查字符串；用 `?=`，命令行覆盖优先 —— E2E 更新测试以如 `make OC_RELEASE_VERSION=WP-10c-test1` 构建 |
| `OC_KERNEL_BUILD_OFFSET` | `21` | 在 `git rev-list --count HEAD` 之上的对齐偏移（保持数字 OTA 比较在已发布 id 278/321/414/416 之上单调） |
| `OC_KERNEL_BUILD` | `expr git rev-list --count HEAD + 21` | 单调构建 id（BUG-0347）；可用 `make OC_KERNEL_BUILD=42` 覆盖 |
| `OC_CACHE_DIR` | `$(HOME)/.cache/opencube-cc` | 编译缓存根（`oc_cc.sh`）；`OC_CACHE_OFF` 绕过 |
| `CONFIG_NVME` / `CONFIG_AHCI` / `CONFIG_VIRTIO_BLK` | `y`（存在 `.config` 时取自其中） | 硬件画像开关；`=n` 会把该驱动的 `.c` 从 `KERNEL_C` 过滤掉并将其 init 打桩 |
| `ARCH` | `x86_64`（亦支持 `arm64`、`riscv`） | 分发到对应架构的 `build.mk`，而不是整段 x86_64 主体 |
| `CC` / `LD` / `NASM` / `QEMU` | `gcc` / `ld` / `nasm` / `qemu-system-x86_64` | 工具选择（按 PATH 解析） |
| `KERNEL_DIRS` | kernel + arch + services + drivers 通配 | 源码通配根 |
| `KERNEL_INC` | 每个 `KERNEL_DIRS` 的 `-I` + `tools` + `services/*/interface` + `kernel/generated` + `include` | 多 `-I` 纯基名 include 策略 |
| `LDFLAGS` | `-n -nostdlib -T ocos/kernel/arch/x86_64/linker.ld -z max-page-size=0x1000 -z noexecstack` | 内核链接 |
| `ETC_IMG` | `build/etc.img` | 32 MiB FAT32 `/etc` 卷（mformat + mcopy `etc/opencube.conf`） |
| `SRC_ZIP` / `OTA_PKG` | `dist/OpenCubeOS-src-<ver>.zip` / `dist/OpenCubeOS-<ver>-update.tar.gz` | 发布产物 |

工具所认的环境变量：`OC_TOOLS`、
`OC_RELEASE_VERSION`/`OC_KERNEL_BUILD`（make 覆盖）、
`OC_CACHE_DIR`/`OC_CACHE_OFF`、`ARCH`/`CROSS`、`JOBS`
（build.sh，默认 nproc）、`OVMF_FD`/`QEMU_DATADIR`/
`SEABIOS_DIR`、`OC_PROMPT`（qemu_runner 提示符正则，
默认 `(?:ush>|oc>)\s`）、`OC_CMD_TIMEOUT`（默认 300）、
`OC_USB_SERIAL_ECHO`、`OC_ETC_IMG`（供宿主密钥快速路径
使用的预播种 /etc）、`LD_LIBRARY_PATH`（迁移的 QEMU 库）、
`SOURCE_DATE_EPOCH`。

## 3. CFLAGS 与 -D 宏表

内核 CFLAGS（x86_64）：

```
-ffreestanding -fno-stack-protector -fno-pie -fno-pic
-mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel
-fno-asynchronous-unwind-tables -Wall -Wextra -Werror
-O2 -g -std=gnu11 $(KERNEL_INC)
-DOC_RELEASE_VERSION=\"$(OC_RELEASE_VERSION)\"
-DOC_KERNEL_BUILD=$(OC_KERNEL_BUILD)
-MMD -MP
```

零警告门禁：每个组件（kernel、OCIRE、OCBL、OCSE、
agent）都开 `-Wall -Wextra -Werror` —— 这是本项目的
"lint"。

构建期 `-D` 宏：

| 宏 | 定义处 | 启用什么 |
|---|---|---|
| `OC_RELEASE_VERSION` | kernel/OCSE/arch CFLAGS（字符串） | 横幅、uname、更新检查版本 |
| `OC_KERNEL_BUILD` | kernel/OCSE/arch CFLAGS（数字） | 参与数字比较的构建 id |
| `OCBL_RELEASE`、`OCBL_BUILD` | ocbl.mk 两种风味 | OCBL 横幅/版本 |
| `OCBL_FLAVOR_BIOS` | ocbl.mk BIOS 对象 | 选择 int-13h/VGA/E820/VBE 后端 |
| `OCBL_FLAVOR_UEFI` | ocbl.mk EFI 对象 | 同一契约的 EFI 后端 |
| `OCSE_RELEASE`、`OCSE_BUILD` | ocse.mk | OCSE 横幅（`<ver>-ocse`） |
| `OCBL_ELTORITO` / `OCBL_MBR` | `stage1.asm` 上的 NASM `-D` | 汇编哪种 512 字节 stage1 风味 |
| `OC_CONFIG_NVME` / `OC_CONFIG_AHCI` / `OC_CONFIG_VIRTIO_BLK` | 生成的 `config_gen.h`（0/1） | 硬件画像；`0` 还会把 `driver_block_<x>_init()` 打桩 |
| `OC_VERSION_STAMP` | 生成的 `version_gen.h` | 触发重编译的字符串 |
| `OCIRE_STAMP` | 生成的 `ocire_version_gen.h` | OCIRE 重编译触发器 |
| `RSA_DEBUG` | `svc_crypto_rsa.c` 内源码局部 `#ifdef` | RSA 路径的调试打印 —— 编辑该文件顶部的 `#define` 启用 |

构建文件中**没有** `NAP_SANITIZE`、`DEBUG` 或 `OC_DEBUG`
构建宏 —— 调试行为按工具区分（宿主测试用各自的 gcc
命令行编译；若干在本地使用 ASAN）。

## 4. 0x400000 窗口与逐文件优化覆盖

内核镜像必须结束在 **0x400000** 以下 —— 由 `linker.ld`
中的链接期 ASSERT 强制：

```
ASSERT(__end_of_kernel <= 0x400000, "kernel image grew past the 0x400000 identity-window boundary - move large buffers to kmalloc")
```

原因：用户地址空间把*虚拟*窗口 0x400000-0x600000 重映射
到空 PT（P3-1 安全修复），因此内核在用户 CR3 上运行时
永远不能解引用在该*物理*区间内发出的 PMM 帧；PMM 永久
保留这 512 帧，而镜像结束于 0x400000 以下让恒等窗口
保持无歧义。所有大对象（1024 项 klog 环、32 连接 TCP 表、
管道、崩溃转储缓冲区）都因此放在内核堆上。

**9 个逐文件 `-Os` 覆盖**（通则是 `-O2`；这些文件把
`-O2` 过滤掉改用 `-Os`）：

- `drv_net_wireless.c`、`drv_net_iwlwifi.c`、
  `drv_net_rtlwifi.c`、`drv_net_ath.c`、`drv_net_brcm.c`
  （无线家族）
- `drv_storage_sdhci.c`
- `drv_misc_mjpeg.c`、`drv_misc_camera.c`、
  `drv_misc_camera_uvc.c`

原因：实测 `-O2` 下超出约 8 KiB —— 覆盖让镜像留在
窗口内（链接器 ASSERT 是字节精确的）；相机一组对
代码生成还额外敏感。

**1 个逐文件 `-O0` 覆盖**：

- `drv_bus_acpi_aml.c` —— AML 解释器：`-O2` 代码生成
  曾让 `kfree` 因野指针崩溃；`-O0` 能干净解析 QEMU 的
  现场 DSDT。（AML 是对生成代码的解释 —— 优化会改变
  解释器所依赖的生命期。）

历史注记：NAK 清理期间刻意删除了两条针对根级 `tui/`
目录的死 `-Os` 规则 —— 路径迁移逐文件覆盖规则时照此
办理。

## 5. 两阶段链接与 cmp 守护的生成器

```
KERNEL_OBJ + grub_boot_stub.o --ld--> build/opencube_base.elf (+strip)
embed_grub.py --> generated/grub_boot_data.c --> grub_boot_data.o
KERNEL_OBJ + grub_boot_data.o --ld--> build/opencube.elf (+strip)
```

- **阶段 1** 以 `core_boot_grubstub.c` 中的强零长度
  `grub_boot_img` / `grub_core_img` 符号链接，因此内核
  在真实启动数据存在之前就能链接。
- **阶段 2**：`tools/embed_grub.py` 以磁盘上配置所需的
  模块（biosdisk、part_msdos/gpt、fat、multiboot2、
  search_fs_file、normal、configfile、echo、serial、
  test、ls）运行 `grub-mkimage`，把 512 字节 boot.img +
  164,355 字节 core.img 输出为 C 数组；阶段 2 链接用
  真实数据替换桩符号。`install`/`abdisk`/`grub-install`
  就是这样写出零外部文件的可启动磁盘的。
- **cmp 守护**：`version_gen.h` 与 `config_gen.h` 是
  FORCE 运行的生成器，其输出只在内容有差异时才重写
  （`cmp` 守护）。`version_gen.h` 携带
  `#define OC_VERSION_STAMP "<ver> build <build>"`，
  使 `-D OC_RELEASE_VERSION` 对 `-MMD -MP` 依赖图可见
  （WP-10-tui-qa 修复：增量构建曾在新代码上保留旧横幅）。
  `config_gen.h` 是每个对象的 order-only 前置，因此它
  在任何编译之前就存在。同一幂等发射（字节比较）模式
  守护着 `userprogs_data.h`、`core_lib_i18n_data.c` 和
  IME 词组表。

## 6. 应用内嵌链

`make userprogs`（`make all` 的一部分，`make dist` 中由
`userprogs-check` 守护）运行 `tools/gen_userprogs.py`：
发现每个 `ocos/l1apps/<name>/` 目录 + 扁平遗留程序，
逐个 freestanding 编译（仅 `-I ocos/interface`），按镜像
模式链接（ET_EXEC @0x400000 / PIE + PT_INTERP /
ld_so @0x10000000），把每个 ELF gzip 成
`userprog_gz_<sym>[]`（每应用一条 gzip 流；ush 保持
原始），并输出 `ocos/kernel/userprogs_data.h`（blob +
`OC_USERPROGS[]` + `OC_USERPROGS_COUNT`）。没有手工
注册这回事；一个新目录就是一条新命令。过期集合包括
`*.c`、`*.asm`、`ocos/interface/*.h` 与每个
`i18n/*.json`（WP06g2 提示清扫在一次现场捕获后补上了
json）。

## 7. 编译缓存

`tools/oc_cc.sh` 用内容寻址缓存包裹每条编译规则：
键 = SHA-256(编译器身份 + 完整标志集 + 源内容哈希 +
来自 `-MM` 的每个头依赖的内容哈希)。热对象与冷对象
字节一致，因此 `make reproducible-check` 保持有效；
过期的缓存条目不可能存在（任何头编辑都会改变键）。
用 `make cache-stats` / `make cache-clear` 管理。

## 8. 打包确定性（G13）

- **ELF**：树中没有 `__DATE__`/`__TIME__`/
  `__TIMESTAMP__`；固定链接顺序（通配展开）；
  `strip --strip-debug`；导出时尊重 `SOURCE_DATE_EPOCH`。
- **SRC zip**（`make src-zip`）：从 tag `v<ver>` 执行
  `git archive`（回退 HEAD 并大声警告）—— 树序、提交
  mtime、索引权限。
- **OTA**（`make ota`）：内核 + 清单（清单携带按归档
  顺序拼接的成员体之上的载荷 sha256）；先强制刷新 ISO
  （针对旧 ISO + 新 OTA 的 v2.0.0 修复）。
- **FAT 镜像**：`tools/fat_zero_dates.py` 把日期/时间
  字段清零（mformat/mcopy 会打上墙上时钟）。
- **ISO**：xorriso 钉住卷日期/节点时间/GPT GUID/ESP
  序列号；`tools/iso_zero_ctime.py` 把 Rock Ridge TF
  ctime/atime 重写为 epoch。
- **OCOS.TAR**（安装载荷）：ustar、零 mtime、uid/gid 0、
  排序、NUL 填充头。
- `make reproducible-check`：两次干净构建，字节比较 ELF，
  有差异即退出码 1。

## 9. 多配置与交叉构建

```
make defconfig          # .config with everything on
make menuconfig         # interactive (tools/oc_menuconfig.sh)
make CONFIG_NVME=n iso  # hardware profile: NVMe off
make CONFIG_AHCI=n CONFIG_VIRTIO_BLK=n iso
make ARCH=arm64         # PL011 bring-up shell ELF (aarch64-linux-gnu-)
make ARCH=riscv         # NS16550A bring-up shell ELF (riscv64-linux-gnu-)
```

被禁用的配置把驱动 init 调用编译为空操作桩，绝不删除
代码。注意 `tools/oc_menuconfig.sh` 的菜单文本仍在宣传
已移除的 `CONFIG_TUI`/`CONFIG_CHINESE` 开关（过时 ——
Makefile 只消费 NVME/AHCI/VIRTIO_BLK；CJK 流水线是
无条件的）。

## 10. 可脚本化 API 与发布自动化

`tools/build.sh` 兼作函数库：

```
source tools/build.sh
build_all; build_iso; build_src; build_ota
config_load; config_save; config_get; cache_lookup
cross_compile; cross_toolchain_setup
tools/build.sh api-selftest     # verifies the spec'd interfaces
```

发布 SOP 组合这些函数而不是解析 make 输出 —— 见
[../release/process.md](../release/process.md) 与
`tools/build_release.sh <ver> [--deep]`（干净
`make all` -> `reproducible-check` -> BIOS+UEFI 横幅
门禁 -> 可选深度门禁 -> 文档/网站再生成；打印三个
SHA256；拒绝已存在的 tag 或错误的 git 身份）。
