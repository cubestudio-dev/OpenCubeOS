<!-- SPDX-License-Identifier: Apache-2.0 -->
# 开发环境

如何从一台全新机器到动手改内核 —— 工具链契约、两道
依赖门禁、免 root 的独立目录工具链安装，以及你将要游走的目录布局。

## 1. 需求

- 一台 Linux 宿主机（Debian/Ubuntu 已测试；任何装有下列
  软件包的系统均可）。Windows：WSL2 给你的正是这套环境
  （`build.ps1` 分发到它；MSYS2 缺 xorriso/grub）。
- **发布工具链**：gcc-14（Debian gcc 14.2.0 是发布版本
  所用的编译器；CI 优先 `gcc-14`、回退 `gcc`）、binutils
  `ld`、**nasm 2.16.03**、xorriso **1.5.6**、mtools、
  grub-common + grub-pc-bin + grub-efi-amd64-bin、
  python3（3.9+）、zip。
- 测试用：**qemu-system-x86 10.0.13**、**OVMF**、seabios、
  ipxe-qemu。
- E2E 套件用：Python `pexpect`（回归、mega 应用冒烟）和
  `paramiko`（SSH E2E）。
- 仅交叉引导外壳需要：`gcc-aarch64-linux-gnu`、
  `gcc-riscv64-linux-gnu`（GCC >= 12）。

存在两个不同的 "check_deps" —— 不要混淆：

1. **宿主软件包检查** —— `tools/build.sh` 校验构建集
   （gcc、ld/binutils、nasm、grub-mkimage/grub-common、
   xorriso、mformat+mcopy/mtools、python3、zip），逐命令
   打印对应的 Debian 软件包；`--install` 以 root 执行
   apt 安装，否则指向下面的免 root 替代方案。
2. **架构门禁** —— `tools/check_deps.py` 对
   `ocos/{kernel,drivers,services,l1apps}` 强制执行
   *依赖方向*：
   - kernel 不得 include 任何解析到 `drivers/` 或
     `services/` 之下的内容（例外：`core_boot_*.c` 组合
     根），
   - drivers 不得 include services，
   - l1apps 只能 include `ocos/interface/` 所有的头文件，
   - 按真实文件清单解析纯基名 `#include "x.h"`；任何违规
     都以 file:line、该 include 及解析到的所有者退出码 1。
   在发布卫生检查之前运行它（它未接入 CI）。

## 2. 免 root 的独立目录工具链安装

`tools/install-toolchain.sh` 下载 Debian `.deb` 并用
`dpkg -x` 解压到 `$OC_OPT`（默认
`~/opt/extract`）—— 约 117 个软件包：nasm 2.16.03、
qemu-system-x86 10.0.13（+ data + 库）、xorriso 1.5.6、
grub-pc-bin + grub-efi-amd64-bin + grub-common、mtools、
ovmf、seabios。gcc 本身来自系统。然后：

```
source tools/env-sandbox.sh   # exports OC_TOOLS, PATH, LD_LIBRARY_PATH
```

或者直接把 Makefile 指向该树：
`make OC_TOOLS=<tree> ...` —— 它会探测 `<tree>/usr/...`
与 `<tree>/...` 两种布局；`OVMF_FD`/`QEMU_DATADIR`/
`SEABIOS_DIR` 由 `OC_TOOLS` 推导，且可单独覆盖。

## 3. 获取代码树与健全性构建

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
make all             # userprogs + kernel + ISO + src zip + OTA pkg
make test            # host unit tests
make run-bios        # boot it
```

三步都通过，你的环境就是好的。注意 CI 以
`fetch-depth: 0` 克隆 —— 构建 id 是
`git rev-list --count HEAD + 21`，浅克隆会烘出一个低于
已发布版本的构建 id（每台已安装的机器都会把这个发布
当作"降级"而忽略）。一次变更的完整门槛见
[CONTRIBUTING.md](../root/CONTRIBUTING.md)。

## 4. 仓库布局

```
ocos/kernel/      L0 core: boot chain, sched, PMM/VMM/heap, usermode
ocos/kernel/arch/x86_64/  the 64-bit arch layer (cross archs in build.mk)
ocos/services/    one folder per service facet: block, net, audio,
                  fs, selfhost (OTA), power, diag, health, klog,
                  watchdog, display, tui, crypto, camera, usb, ...
ocos/drivers/     one folder per class: storage, net, audio, bus
                  (pci/acpi/i2c), usb, input, display, time, misc
ocos/interface/   the ring-3 contract headers: l1_lib.h,
                  l1svc_ops.h, i18n.h, wizard_lib.h, fs_lib.h,
                  editor_core.h, oc_l1_api.h, crt0.asm
ocos/l1apps/      one command = one app = one folder (284 programs)
                  + _i18n/ for the flat legacy test programs
ocire/            the independent recovery kernel (second L0)
ocbl/             the in-tree bootloader (BIOS stage1 + EFI entry)
ocse/             the vendored install-environment kernel
tools/            build + test + docs generators (117 files)
tests/            host-side unit tests (make test)
docs/             this documentation tree (en + zh mirror)
etc/              the default /etc volume content + l0 i18n
```

## 5. 立刻就要遵守的约定

- `-Wall -Wextra -Werror` —— 零警告，无例外，适用于每个
  组件。
- 每个文件都带 SPDX 头。
- 内核/服务可见的字符串是 ASCII 英文（它们是脚本 +
  向导锚点契约）；人类界面通过 `tr_()` 以 en+zh 资源
  渲染。
- 命令面只增不减（接口冻结；编号只追加 —— 见
  [interface-dictionary.md](interface-dictionary.md)）。
- 大缓冲区放在堆（内核）或静态 BSS（ring 3）—— 内核镜像
  被钉在 0x400000 以下（见 [build.md](build.md) 第 4 节）。

## 6. QEMU 配方

```
# quick BIOS boot, serial on stdio
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 -serial mon:stdio -display none

# UEFI (OVMF; the make target copies OVMF.fd to a pflash work copy)
qemu-system-x86_64 -m 512M -machine q35 \
    -drive if=pflash,format=raw,file=OVMF_WORK.fd \
    -cdrom build/opencube.iso -display none -serial mon:stdio

# with a scratch disk + display for the TUI
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -drive file=scratch.qcow2,if=ide -netdev user,id=n1 \
    -device e1000,netdev=n1

# boot log to a file
... -serial file:boot.log
```

`make run-bios`、`make run-uefi`、`make run-bios-gui`、
`make run-uefi-gui`、`make run-bios-persist`、
`make shot-*` 封装了常见用法（包括全新 32 MiB `/etc`
卷的建立，让持久化测试从干净状态开始）。迁移过的工具链：
`make OC_TOOLS=<tree> run-bios`。更深入的 QEMU 指南 ——
勘误、sendkey、固件路径 —— 见
[qemu-testing.md](qemu-testing.md)。

## 7. 宿主单元测试

`make test`（`tools/run_host_tests.py`）从每个
`tests/host_*.c` 头部提取 gcc 命令行，重映射重构前路径，
在宿主编译器上编译并运行：加密向量（NIST AES/SHA/HMAC、
RSA、X.509、AEAD）、TLS 密钥调度、TCP 拥塞控制、
block-512 门禁、multiboot2 解析、BT/WiFi/相机/SCSI/
IR/触摸板子系统。若干套件在本地启用 ASAN；依赖夹具的
（openssl RSA/链夹具）在夹具缺失时声明 SKIP-FIXTURE。
这里的一次回归比一次启动循环的迭代快得多。完整清单：
[host-tests.md](host-tests.md)。
