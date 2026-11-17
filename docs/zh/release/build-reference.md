<!-- SPDX-License-Identifier: Apache-2.0 -->
# 构建系统全量参考（维护者手册）

Open Cube OS 构建的完整机制级参考：每一个伪目标、每一个内部
文件目标、每一个变量、每一个 `-D` 宏、每一个按文件的优化覆盖，
以及并行度 / 编译缓存机制。配套页面：[guard-scripts](guard-scripts.md)
（检查脚本）、[ci-gates](ci-gates.md)（CI 与发布链）、
[tool-inventory](tool-inventory.md)（`tools/` 全部 117 个文件）。

本文覆盖的构建文件：`Makefile`（顶层）、`ocire/ocire.mk`、
`ocbl/ocbl.mk`、`ocse/ocse.mk`、`ocos/kernel/arch/{arm64,riscv}/build.mk`，
以及入口封装 `tools/build.sh` 和 `build.ps1`。

## 1. 架构分派与入口

`ARCH ?= x86_64`。任何超出 `x86_64 arm64 riscv` 的取值都会立即以
`$(error unsupported ARCH '<ARCH>' - supported: x86_64 arm64 riscv)` 失败——make 在运行任何一条命令之前即中止。

- `ARCH=x86_64`（默认）：`Makefile` 的完整主体——内核、ISO、
  src zip、OTA、QEMU、配置、缓存、测试——外加三个组件包含
  （`ocire.mk`、`ocbl.mk`、`ocse.mk`）。
- `ARCH=arm64` / `ARCH=riscv`：整个 x86_64 主体被跳过，改为包含
  `ocos/kernel/arch/$(ARCH)/build.mk`（仅 bring-up 壳；见 §7）。

三个等价入口构建同一产物：

| 入口 | 相对裸 make 的附加能力 |
|-------|----------------------------|
| `make <target>` | 无——事实来源 |
| `tools/build.sh <target>` | 依赖检测（每条命令对应的精确 Debian 包）、`--install` apt 回退、免 root 的 `install-toolchain.sh` 提示、`OC_TOOLS` 自动发现（`/usr`、`$HOME/tools/grub`）、`JOBS` 并行度（默认 `nproc`） |
| `build.ps1`（Windows） | 经 WSL2（`wsl.exe` + `wslpath`）或 MSYS2/Git-Bash（`cygpath`）翻译成对 `tools/build.sh` 的调用；`-Target`（默认 `all`）、`-Install` |

`tools/build.sh` 还暴露可脚本化 API（WP-13 规范 8）：
`build_all`、`build_iso`、`build_src`、`build_ota`、`config_load`、
`config_save`、`config_get`、`cache_lookup`、`cross_compile`、
`cross_toolchain_setup`——source 该脚本即可调用。

`.DEFAULT_GOAL := kernel`：裸 `make` 只构建
`build/opencube.elf`。完整的 WP-13 流水线是 `make all`。

## 2. 顶层伪目标（26 个）

格式：**触发 → 配方逻辑 → 失败后果。**

| # | 目标 | 触发 / 前置条件 | 逻辑 | 失败时 |
|---|--------|------------------------|-------|------------|
| 1 | `all` | `userprogs $(KERNEL_ISO) src-zip ota` | WP-13 统一流水线；先重建内嵌程序（WP06g1 catch-2：`make all` 曾一次交付过期的 `userprogs_data.h`），然后内核 + ISO、确定性 src zip、OTA 包；打印四个产物路径 | 任一子门禁失败即停止流水线 |
| 2 | `kernel` | `$(KERNEL)` = `build/opencube.elf` | WP-13 之前的语义：仅两阶段链接出 ELF（见 §3.2） | 链接/编译错误即中止 |
| 3 | `iso` | `$(KERNEL_ISO)` | 经 `tools/build_iso.sh` 打包双固件 OCBL ISO；前置条件包含 `$(USERPROGS_HDR)`，因此单独 `make iso` 永远不会打包过期的内嵌程序（WP06g2） | 打包脚本非零退出 → make 失败 |
| 4 | `src-zip` | 无（伪目标） | `git ls-files` 经 `SRC_EXCLUDE` 过滤，tag `v$(OC_RELEASE_VERSION)` 存在时与该 tag 的树求交（`ls-tree -r` + `grep -Fx -f`），再 `git archive --format=zip`；HEAD 回退时打印一条响亮的 "development cut, NOT a release asset" 警告；`.srclist` 为空（非 git 检出）→ `exit 1` 并指引用 `make dist` | 非 git 树硬失败；HEAD 切片仅警告 |
| 5 | `ota` | `$(KERNEL) $(KERNEL_ISO)` | `tools/make_update_pkg.sh` → `dist/OpenCubeOS-<ver>-update.tar.gz`；ISO 是前置条件，因此"旧 ISO + 新 OTA"的组合无法产出（v2.0.0 修复） | 脚本失败即中止 |
| 6 | `reproducible-check` | 无 | clean→kernel→sha256，两轮；`cmp -s /tmp/.oc_repro_1.elf $(KERNEL)`；PASS 消息 + 两个哈希，或 FAIL + `cmp` 前 3 个差异字节 + `exit 1` | 任何字节差异即 exit 1 |
| 7–12 | `run-bios`、`run-uefi`、`run-bios-gui`、`run-uefi-gui`、`run-bios-persist`、`run-uefi-persist` | `$(KERNEL_ISO) $(ETC_IMG)` | QEMU 启动：256M 内存、IDE `/etc` 盘、`-no-reboot`、串口 stdio。`-display none`（无头）、`-display gtk`（GUI——PS/2 键盘需要一个窗口来投递扫描码）、`-snapshot` 关闭（persist：`/etc/opencube.conf` 的编辑写回 `build/etc.img`，WP-09-fix5）。UEFI 变体把 `$(OVMF_FD)` 复制到 `build/OVMF_WORK.fd` 并作为 pflash 挂载 | QEMU 退出码向上传播 |
| 13–14 | `shot-bios`、`shot-uefi` | `$(KERNEL_ISO)` | `tools/qemu_shot_vnc.py` 经 VNC 启动、monitor-screendump、用 PIL 把 PPM 转 PNG → `build/shot-{bios,uefi}.png` | 工具失败即中止 |
| 15 | `clean` | 无 | `rm -rf $(BUILD) $(DIST) $(ISO_DIR)/boot/opencube.elf $(GEN_DIR)/grub_boot_data.c`——删除目标文件、ELF、ISO、dist 产物、瞬态 stage-2 GRUB 数据 | — |
| 16 | `dist` | `userprogs-check $(KERNEL_ISO)` | 先过过期门禁（见下），然后带时间戳的 `zip -qr`（不是 git archive），内联排除（`build/`、`dist/`、`.git/`、`tools/push_to_git.py`、`releases/`、网站构建树、`ocos/kernel/generated/*`）、复制 ISO、`sha256sum` 边车文件 | 门禁失败在任何打包之前中止 |
| 17 | `defconfig` | 无 | heredoc 写入带 `CONFIG_NVME=y CONFIG_AHCI=y CONFIG_VIRTIO_BLK=y` 的 `.config` | — |
| 18 | `menuconfig` | 无 | `tools/oc_menuconfig.sh`——无依赖的交互式编辑器（数字=切换，s=保存，q=退出，r=重置） | — |
| 19 | `cache-stats` | 无 | 对 `$(OC_CACHE_DIR)/STATS` 跑 awk：HIT/MISS 计数、命中率 %、条目数；冷缓存打印 "no STATS yet" | 永不失败 |
| 20 | `cache-clear` | 无 | `rm -rf $(OC_CACHE_DIR)` | — |
| 21 | `test` | 无 | `python3 tools/run_host_tests.py` 编译并运行每个 `tests/host_*.c` 测试桩 | **退出码 = 失败测试数** |
| 22 | `l0-i18n` | `$(L0_I18N_GEN)` | `tools/gen_l0_i18n.py` 从 `etc/i18n/l0/{en,zh}.json`（各 49 键）再生 `ocos/kernel/core_lib_i18n_data.c`；幂等 | 生成器失败即中止 |
| 23 | `userprogs` | `$(USERPROGS_HDR)` | `tools/gen_userprogs.py` 全量再生内嵌程序头文件（发现 + 注册表 + 孤儿清除，见 [guard-scripts](guard-scripts.md)） | 任一程序的编译/链接失败即中止 |
| 24 | `userprogs-check` | 无 | 过期门禁：`ls -t $(USERPROG_SRCS)` 最新源对比头文件的 `stat -c %Y`；最新源新出 2 秒以上 → `ERROR: <src> is newer than userprogs_data.h` + `exit 1`（2 秒余量吸收全新克隆的 mtime 抖动） | exit 1；接入 `make dist` |
| 25 | `api-docs` | 无 | `tools/gen_api_docs.py --help-capture evidence/help_a_runtime.txt --l1-registry evidence/help_a_runtime.txt` → `docs/data/*.json` | 生成器失败即中止 |
| 26 | `FORCE` | `.PHONY` | 永远过期的辅助目标，驱动 cmp 守护的生成器 | — |

## 3. 内部文件目标

### 3.1 order-only 前置与生成头文件

| 目标 | 规则要点 | 存在原因 |
|--------|--------------|---------------|
| `$(BUILD)` | `mkdir -p build` | 每个目标文件的 order-only 前置条件 |
| `$(GEN_DIR)/version_gen.h` | `FORCE` + 写入 `.tmp`、`cmp -s` 守护、仅变更时 mv | 产出 `#define OC_VERSION_STAMP "<ver> build <build>"`。`-D` 参数对 `-MMD` 不可见，因此增量构建曾保留过期横幅（WP-10-tui-qa：一个 ISO 在 `-ga29ac53` 的代码上启动了 `-ga4d5342` 横幅）。该头文件让版本变更对依赖图可见 |
| `$(GEN_DIR)/config_gen.h` | 同样的 cmp 守护模式 | 产出 `OC_CONFIG_NVME/AHCI/VIRTIO_BLK` 0/1，以及关闭时 `driver_block_<x>_init()` 的空操作桩；是 `$(KERNEL_OBJ) $(STUB_OBJ) $(GRUB_DATA_OBJ)` 的 order-only 前置条件——必须在任何目标文件编译前存在，但只有 `.d` 文件触发重建 |

### 3.2 两阶段内核链接

```
KERNEL_OBJ + grub_boot_stub.o --ld--> build/opencube_base.elf (+strip)
embed_grub.py "$(OC_TOOLS)" base.elf --> ocos/kernel/generated/grub_boot_data.c
grub_boot_data.c --compile--> grub_boot_data.o
KERNEL_OBJ + grub_boot_data.o --ld--> build/opencube.elf (+strip)
```

- `$(STUB_OBJ)`：`core_boot_grubstub.c` 单独编译（仅 stage-1
  链接；从 `KERNEL_C` 中过滤掉）。
- `$(GRUB_DATA_C)`：`tools/embed_grub.py` 把 GRUB i386-pc 的
  `boot.img`（512 B）+ `core.img`（grub-mkimage，带盘上 cfg
  所需模块）作为 C 数组内嵌——内核内安装器的载荷。内核 ELF
  本身不被内嵌（GRUB 经 grub.cfg 把它作为 multiboot2 模块
  挂载）。
- `$(KERNEL)`：stage-2 链接还携带 `$(L0_I18N_GEN)` 和
  `$(IME_PHRASE_GEN)` 作为前置条件，使 i18n/IME 表在链接时
  是新鲜的。
- `-include $(KERNEL_OBJ:.o=.d)`：BUG-025 FIX——`-MMD -MP`
  头文件跟踪。OCIRE 和 OCSE 出于同一原因各带自己的
  `-include` 行。

### 3.3 其他文件目标

| 目标 | 逻辑 |
|--------|-------|
| `$(ETC_IMG)` = `build/etc.img` | `truncate -s 32M`、带 `drive e:` 的 mtoolsrc、`mformat -F -v OCS_ETC e:`（FAT32）、`mcopy etc/opencube.conf`——持久的 `/etc` 卷（WP-09-fix5） |
| `$(USERPROGS_HDR)` = `ocos/kernel/userprogs_data.h` | 真实文件目标：任一 `USERPROG_SRCS` 条目（或生成器脚本）更新 → `gen_userprogs.py`；幂等的字节比较式产出，因此已是最新状态的树不再重建任何东西 |
| `$(L0_I18N_GEN)` | `etc/i18n/l0/{en,zh}.json` 或生成器更新 → 再生 |
| `$(IME_PHRASE_GEN)` + 其 `.h` | `tools/ime_phrases.txt` 或 `gen_pinyin_phrase.py` 更新 → 再生 |
| `build/ocos/**/*..o` | 通用 `$(OC_CC) $(CFLAGS) -c`（即 `-O2`） |
| `build/boot_%.o` | 对 `ocos/kernel/core_boot_*.S` 的 NASM `-f elf64 -F dwarf -g` |
| `build/core/arch/%.o` | 对 `ocos/kernel/arch/x86_64/*.S` 的 NASM |
| `$(KERNEL_ISO)` | `bash tools/build_iso.sh`；前置条件：内核、`userprogs_data.h`、`ocbl_bios.bin`、`ocbl_x64.efi`、`ocire.elf`、脚本本身 |
| `$(SRC_ZIP)` / `.srclist` / `.taglist` | 见上文 `src-zip` |

## 4. 变量（完整清单）

### 4.1 路径、工具、架构

| 变量 | 默认值 / 推导 | 含义 |
|----------|----------------------|---------|
| `OC_ROOT` | Makefile 目录的 `abspath` | 仓库根 |
| `BUILD` / `ISO_DIR` / `DIST` | `$(OC_ROOT)/build`、`/iso`、`/dist` | 输出目录 |
| `ARCH` | `x86_64` | 目标机分派（`x86_64 arm64 riscv`，否则 `$(error)`） |
| `CC` / `LD` / `NASM` / `QEMU` | `gcc` / `ld` / `nasm` / `qemu-system-x86_64` | PATH 解析的工具 |
| `OC_TOOLS` | `/usr` | 迁移式工具链根（dpkg -x 树）；`build.sh` 自动检测 |
| `OVMF_FD` | `$(OC_TOOLS)/share/ovmf/OVMF.fd` | UEFI 固件 |
| `QEMU_DATADIR` | `$(OC_TOOLS)/share/qemu` | QEMU `-L` 路径 |
| `SEABIOS_DIR` | `$(OC_TOOLS)/share/seabios` | BIOS 固件 |
| `CROSS`（架构构建） | `aarch64-linux-gnu-` / `riscv64-linux-gnu-` | 交叉前缀 |

### 4.2 版本与标识

| 变量 | 默认值 | 含义 |
|----------|---------|---------|
| `OC_RELEASE_VERSION` | `3.0.0-NAK`（固定字面量；`?=` 覆盖优先） | 内核横幅 / `uname` / 更新检查字符串；按 E2E 更新测试覆盖，例如 `make OC_RELEASE_VERSION=WP-10c-test1` |
| `OC_KERNEL_BUILD_OFFSET` | `21` | 在 `git rev-list --count HEAD` 之上的对齐偏移；保持数值型 OTA 比较在已发布 id 278/321/414/416 之上单调（BUG-0347） |
| `OC_KERNEL_BUILD` | `expr git rev-list --count HEAD + 21` | 单调构建 id；覆盖方式 `make OC_KERNEL_BUILD=42` |

### 4.3 配置

| 变量 | 默认值 | 效果 |
|----------|---------|--------|
| `CONFIG_NVME` / `CONFIG_AHCI` / `CONFIG_VIRTIO_BLK` | `y`（存在 `.config` 时取自其中） | `=n` 把该驱动 `.c` 从 `KERNEL_C` 过滤掉，并经 `config_gen.h` 把它的 init 调用替换为桩 |
| `.config` | （可选） | 由 `defconfig`/`menuconfig` 写入的 make 片段；命令行覆盖优先 |

### 4.4 源与目标文件

| 变量 | 内容 |
|----------|---------|
| `KERNEL_DIRS` | `ocos/kernel`、`ocos/kernel/arch/x86_64`、`ocos/services`、`ocos/drivers` + `ocos/services/*/src`、`ocos/drivers/*/` 的通配 |
| `KERNEL_INC` | 每个 `KERNEL_DIRS` 的 `-I` + `tools` + `ocos/services/*/interface` + `ocos/kernel/generated` + `include`（多 `-I` 纯基名 include 策略） |
| `KERNEL_C` | 对 `KERNEL_DIRS` 的 `*.c` 通配，减去 `core_boot_grubstub.c`，减去被配置过滤的驱动 |
| `KERNEL_ASM_B` / `KERNEL_ASM_K` | `ocos/kernel/core_boot_*.S` / `ocos/kernel/arch/x86_64/*.S` |
| `KERNEL_OBJ` | 模式替换 `.c→.o`（build 镜像）、boot `.S→boot_%.o`、arch `.S→core/arch/%.o` |
| `KERNEL` / `KERNEL_ISO` | `build/opencube.elf` / `build/opencube.iso` |
| `GEN_DIR` / `GRUB_DATA_C` / `GRUB_DATA_OBJ` / `STUB_OBJ` / `BASE_ELF` | 生成目录 + 两阶段链接集合 |
| `VERSION_GEN` / `CONFIG_GEN` | cmp 守护的生成头文件 |
| `EMBED_PY` / `BUILD_C_PY` / `BUILD_SOLIB_PY` | `tools/embed_userprog.py` / `build_c_userprog.py` / `build_solib.py` |

### 4.5 内嵌程序、打包、缓存

| 变量 | 内容 |
|----------|---------|
| `USERPROG_SRCS` | 对 `ocos/l1apps/*.{asm,c}`、`ld_so.c`、`*/*.c`、`*/*.asm`、`ocos/interface/*.h`、`*/i18n/*.json`、`_i18n/*/*.json` 的通配（json 在 WP06g2 的 548 文件提示清扫现场抓出后加入） |
| `USERPROGS_HDR` | `ocos/kernel/userprogs_data.h` |
| `ETC_IMG` | `build/etc.img` |
| `SRC_ZIP` / `SRC_EXCLUDE` / `SRC_REV` | `dist/OpenCubeOS-src-<ver>.zip`；排除 `website/public/downloads website/out website/.next website/node_modules build dist releases .zscripts public/deliverables evidence archive trash`；rev = tag `v<ver>`，否则 `HEAD` |
| `OTA_PKG` | `dist/OpenCubeOS-<ver>-update.tar.gz` |
| `OC_CACHE_DIR` | `$(HOME)/.cache/opencube-cc` |
| `L0_I18N_GEN` / `IME_PHRASE_GEN` | 生成的 i18n/IME 源 |

### 4.6 参数

| 变量 | 值 |
|----------|-------|
| `CFLAGS` | `-ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 -g -std=gnu11 $(KERNEL_INC) -DOC_RELEASE_VERSION=\"…\" -DOC_KERNEL_BUILD=N -MMD -MP` |
| `ASFLAGS` | `-f elf64 -F dwarf -g` |
| `OC_CC` | `bash tools/oc_cc.sh $(CC)`——编译缓存封装 |
| `LDFLAGS` | `-n -nostdlib -T ocos/kernel/arch/x86_64/linker.ld -z max-page-size=0x1000 -z noexecstack` |

`-Wall -Wextra -Werror` 三件套出现在每个组件的参数里（内核、
OCIRE、OCBL、OCSE、agent）——这是本项目的 "lint"；CI 在每次
推送时运行它（见 [ci-gates](ci-gates.md)）。

## 5. .mk 包含文件

### 5.1 ocire.mk（OCIRE 恢复内核——11 个伪目标）

独立内核：自己的参数（`OCIRE_CFLAGS`，同一 freestanding 集，
仅 `-I ocire -I ocire/generated -I include`）、自己的链接脚本
`ocire/linker_ocire.ld`、优先于通用 `$(BUILD)/%.o` 规则的静态
模式规则、自己的 `-include $(OCIRE_OBJ_C:.o=.d)`（在 WP-02
现场抓出过期的 OCIRE 目标文件向两个 MBR 写入垃圾起始 LBA 之后
加入）。`OCIRE_VERSION ?= $(OC_RELEASE_VERSION)`（BUG #92 修复
——横幅曾落后四个里程碑）。内部生成器：
`ocire/generated/ocire_cn_font_data.c`（OCSE 字体数据的 sed
改写，cmp 守护）和 `ocire_version_gen.h`（`OCIRE_STAMP`，
cmp 守护）。

| 目标 | 产物 / 逻辑 |
|--------|-----------------|
| `ocire-agent` | ring-3 日志代理，按 l1apps C 程序的方式编译（user.ld @ 0x400000、int 0x80）但独立交付 → `build/ocire/agent.elf` |
| `ocire-kernel` | `build/ocire/ocire.elf` |
| `ocire-iso` | `tools/build_ocire_iso.sh` → `build/ocire/ocire.iso` |
| `ocire-disk` | `tools/make_ocire_disk.sh` 双启动 + 恢复测试盘 |
| `ocire-run-bios` / `-uefi` / `-bios-gui` / `-uefi-gui` | QEMU 运行（128M、自己的 `OVMF_OCIRE.fd` pflash 副本） |
| `ocire-test` | `tools/run_ocire_tests.py`（boot/help/unknown/backspace/shutdown × BIOS+UEFI） |
| `ocire-dual-test` | 双启动 + 分区隔离全套 |
| `ocire-shell-test` | WP-02 第 4 组恢复 shell 全套（help/logread/syscheck/repair quick/full/return，含摘要损坏 + BPB 被清零） |

### 5.2 ocbl.mk（OCBL 引导加载器——双风味）

一棵源码树，两种风味：带 `-mcmodel=small` 的
`-DOCBL_FLAVOR_BIOS`，和带 `-mcmodel=large` 的
`-DOCBL_FLAVOR_UEFI`（每个地址都是 PE 转换器可重定位的 64 位
`movabs`）。内建的断言（违规即构建失败）：两种 stage1 风味都
恰好 512 B；`tramp16.bin` ≤ 0x300 B；`handoff_blob.bin` ≤
0x1000 B；EFI PE 头检查（MZ / PE\0\0 / machine 0x8664 /
subsystem 10）。`include/ocbl_state.h` 是每个 OCBL 目标文件的
前置条件（WP07a 过期地址现场抓出）。EFI 风味用 `--emit-relocs`
链接一个中间 ELF 并经 `ocbl/elf_to_efi.py` 转换（完整的
`.reloc` 目录；朴素的 `ld -m i386pep` 路线在 OVMF 下重定位
错误）。

伪目标：`ocbl-bios`（stage1 El Torito + MBR 风味 + core →
`ocbl_bios.bin`、`stage1_mbr.bin`）、`ocbl-efi`
（`ocbl_x64.efi`）、`ocbl-iso`（经 `ocbl/build_test_iso.py` 的
独立双固件测试 ISO——该脚本是硬前置条件，B1 现场抓出）、
`ocbl-run-bios`、`ocbl-run-uefi`、`ocbl-scan-disks`（经
`ocbl/make_scan_disks.py` 的 11 镜像机群；时间戳覆盖所有同级
镜像——B5-7 现场抓出）、`ocbl-run-scan`（启动机群：2 块 IDE
盘、3 块 virtio 盘、FAT + 裸镜像、1 张 CD + 测试 ISO）。

### 5.3 ocse.mk（OCSE 安装环境）

- `ocse-vendor-check`——`ocse/kernel/types.h` 缺失即失败：
  "run `tools/vendor_ocse.sh` then `tools/ocse_edits.py`"。作为
  每个 `make ocse-elf` 的前置条件运行。
- `ocse-elf` → `build/ocse/ocse.elf`（单阶段链接、输出 `-Map=`、
  `filter-out driver_block_disk_setup.c`——GRUB 时代的工具；该
  过滤行逐字重复了 10 次，一个已知的外观瑕疵）。
- `ocse-iso` → `bash tools/build_ocse_iso.sh`，它先对 ELF 运行
  `tools/check_bss.py`（见 [guard-scripts](guard-scripts.md)）。
- `ocse-clean`；自己的 `-include $(OCSE_OBJ:.o=.d)`。
- `OCSE_CFLAGS` 镜像内核参数集 + `-DOCSE_RELEASE=\"<ver>-ocse\"`
  + `-DOCSE_BUILD`。

### 5.4 arm64 / riscv build.mk（各 4 个目标）

`make ARCH=arm64|riscv` 替换整个 x86_64 主体。工具链
`CROSS ?= aarch64-linux-gnu-` / `riscv64-linux-gnu-`（Debian
`gcc-aarch64-linux-gnu` / `gcc-riscv64-linux-gnu`，GCC ≥ 12）。
源码：`src/L0/core/arch/<arch>/{boot.S,kernel_main.c}`（裸机
入口 + UART + 心跳）。`XCFLAGS`：freestanding +
`-mgeneral-regs-only`（arm64）/ `-march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany`（riscv——无 FP：裸 `-kernel` 启动的 M-mode 没有
编程 `mstatus.FS`），两者都带 `-Wall -Wextra -Werror -O2 -g -std=gnu11 -DOC_RELEASE_VERSION -DOC_KERNEL_BUILD`。目标：
`all`/`kernel` → `build/opencube-<arch>.elf`（+ readelf
Class/Machine/Entry 回显）、`run-bios` → `qemu-system-<arch> -machine virt [-cpu cortex-a57] -nographic -kernel`、`clean-arch`。
链接：`$(XCC) -T linker.ld -Wl,--build-id=none -Wl,--hash-style=sysv`。

## 6. -D 宏表（全部构建期定义）

| 宏 | 定义位置 | 启用什么 |
|-------|---------------|---------|
| `OC_RELEASE_VERSION` | kernel/OCSE/arch CFLAGS（字符串） | 内核横幅、`uname`、更新检查版本串 |
| `OC_KERNEL_BUILD` | kernel/OCSE/arch CFLAGS（数字） | 被 OTA 检查按数值比较的单调构建 id（BUG-0347） |
| `OCBL_RELEASE`、`OCBL_BUILD` | ocbl.mk，两种风味 | OCBL 横幅/版本 |
| `OCBL_FLAVOR_BIOS` | ocbl.mk BIOS 目标文件 | 选择 int-13h/VGA/E820/VBE 后端实现（`ocbl/common/ocbl_main.c` 中的 `#ifdef` 位点） |
| `OCBL_FLAVOR_UEFI` | ocbl.mk EFI 目标文件 | 同一契约的 EFI 后端 |
| `OCSE_RELEASE`、`OCSE_BUILD` | ocse.mk | OCSE 横幅（发布字面量 + `-ocse` 后缀） |
| `OCBL_ELTORITO` / `OCBL_MBR` | `stage1.asm` 上的 NASM `-D` | 汇编哪种 512 字节 stage1 风味 |
| `OC_CONFIG_NVME` / `OC_CONFIG_AHCI` / `OC_CONFIG_VIRTIO_BLK` | 生成的 `config_gen.h`（0/1） | 硬件画像；`0` 还把 `driver_block_<x>_init(...)` `#define` 成空操作桩 |
| `OC_VERSION_STAMP` | 生成的 `version_gen.h` | 触发重编译的字符串 |
| `OCIRE_STAMP` | 生成的 `ocire_version_gen.h` | OCIRE 重编译触发器 |
| `RSA_DEBUG` | **不在任何构建文件中**——源码局部 `#ifdef`（`ocos/services/crypto/src/svc_crypto_rsa.c` 中 6 处） | RSA 路径的调试打印；编辑该文件顶部的 `#define` 启用 |

构建文件中任何地方都**没有** `NAP_SANITIZE`、`DEBUG` 或
`OC_DEBUG` 构建宏：所有构建类型的 freestanding 参数完全相同；
宿主侧测试用各自的 gcc 行编译（若干在本地使用 ASAN，记录在
每个 `tests/host_*.c` 头部）。

## 7. 按文件优化覆盖（9 × -Os，1 × -O0）

所有覆盖使用同一模式：
`$(OC_CC) $(filter-out -O2,$(CFLAGS)) -Os|-O0 -c $< -o $@`。

| 文件 | 级别 | 原因（来自 Makefile 注释） |
|------|-------|----------------------------------|
| `ocos/drivers/net/drv_net_wireless.c` | `-Os` | 无线/sd 驱动集——内核镜像必须留在 0x400000 恒等窗口内；链接器 ASSERT 按字节精确，`-O2` 实测超限 8 KiB |
| `ocos/drivers/net/drv_net_iwlwifi.c` | `-Os` | 同一窗口纪律 |
| `ocos/drivers/net/drv_net_rtlwifi.c` | `-Os` | 同 |
| `ocos/drivers/net/drv_net_ath.c` | `-Os` | 同 |
| `ocos/drivers/net/drv_net_brcm.c` | `-Os` | 同 |
| `ocos/drivers/storage/drv_storage_sdhci.c` | `-Os` | 同 |
| `ocos/drivers/misc/drv_misc_mjpeg.c` | `-Os` | 代码生成敏感：`-O2` 错编一条可用路径（段错误被 4:4:4 宿主夹具抓到）；`-Os` 干净，且 JPEG 解码器不是性能关键 |
| `ocos/drivers/misc/drv_misc_camera.c` | `-Os` | 同一相机集原因 |
| `ocos/drivers/misc/drv_misc_camera_uvc.c` | `-Os` | 同 |
| `ocos/drivers/bus/drv_bus_acpi_aml.c` | `-O0` | AML 解释器只在启动时运行一次；`-O2` 代码生成与重指针的值模型交互不良（kfree 中野指针导致启动崩溃），而 `-O0` 能干净解析现场 QEMU DSDT |

历史备注（NAK 清理）：针对根级 `tui/` 目录的两条 `-Os` 规则从未
匹配到真实目标文件，已被删除而非更新路径——现在激活它们会改变
一直以 `-O2` 交付的文件的代码生成。

## 8. 并行度与编译缓存

- **并行度**：`make -j$(nproc)`（build.sh 的 `JOBS`，默认
  `nproc`）。安全性来自每个目标文件的 `@mkdir -p $(dir $@)`
  配方、order-only 前置条件（`| $(BUILD)`、`| $(VERSION_GEN) $(CONFIG_GEN)`）和永不搅动 mtime 的 cmp 守护生成器。
- **编译缓存**（`OC_CC := bash tools/oc_cc.sh $(CC)`）：缓存键 =
  建立在（1）精确编译器身份（路径 + 版本串）、（2）完整参数表
  （flags、`-D` 值、include 路径）、（3）源文件哈希、（4）源所
  依赖的每个头文件的哈希（来自编译器自己的 `-MM` 输出——基于
  内容，因此动了但内容没变的头文件仍然命中、mtime 没动但内容
  变了的头文件仍然未命中）之上的 SHA-256。命中按字节一致还原
  `.o` 和 `.d`；未命中则编译并把两者都存入。命中字节一致，
  因此 `reproducible-check` 在缓存开启时依然有效。控制项：
  `OC_CACHE_DIR`（迁移）、`OC_CACHE_OFF`（绕过）；计数器落到
  `$(OC_CACHE_DIR)/STATS`（由 `cache-stats` 读取）。
- **增量完整性**：所有 `.d` 文件的 `-include`（内核、OCIRE、
  OCSE 各自包含自己的）、`version_gen.h` 重编译触发器，以及
  `userprogs_data.h` 过期门禁，共同封住三类历史过期产物
  （BUG-025、WP-10-tui-qa、WP06g1 catch-2）。

## 9. 生成但已提交的输入

这些文件由 make 规则再生且已提交，因此全新的通配构建总能看到
它们：`ocos/kernel/userprogs_data.h`、
`ocos/kernel/core_lib_i18n_data.c`、
`ocos/kernel/generated/version_gen.h` / `config_gen.h`、
`ocos/kernel/generated/grub_boot_data.c`（仅 stage-2，不在
`KERNEL_DIRS` 中，瞬态）、`ocire/generated/ocire_cn_font_data.c` /
`ocire_version_gen.h`、
`ocos/services/display/src/svc_display_pinyin_phrase_data.c`（+
`.h`）、`ocse/chinese/chinese_font_data.c`。绝不手工编辑它们；
每个文件对应的生成器见 [tool-inventory](tool-inventory.md)。

## 10. 快速参考

```
make all                     # userprogs + kernel + ISO + src zip + OTA
make kernel | iso | src-zip | ota
make test                    # host unit harnesses
make run-bios | run-uefi | run-bios-gui | run-uefi-gui | run-*-persist
make shot-bios | shot-uefi
make reproducible-check
make defconfig | menuconfig
make cache-stats | cache-clear
make userprogs | userprogs-check
make l0-i18n | api-docs
make ocire-kernel | ocire-iso | ocire-test | ocire-disk | ocire-shell-test
make ocbl-bios | ocbl-efi | ocbl-iso | ocbl-run-scan
make ocse-elf | ocse-iso
make ARCH=arm64 | ARCH=riscv
```
