<!-- SPDX-License-Identifier: Apache-2.0 -->
# 开发环境

如何从一台全新机器开始投入内核开发。

## 环境要求

- 一台 Linux 宿主机（已在 Debian/Ubuntu 上测试；装有下列软件包
  的任何系统均可）。Windows：WSL2 给你的正是这套环境。
- `gcc`（已测试 14.x）、`binutils`（ld）、`nasm` 2.16+、`xorriso`、
  `mtools`、`grub-common` + `grub-pc-bin` + `grub-efi-amd64-bin`、
  `python3`（3.9+）、`zip`。
- 测试需要：`qemu-system-x86`、`ovmf`、`seabios`、`ipxe-qemu`。
- E2E 套件需要：Python `pexpect`（回归、大应用冒烟）和
  `paramiko`（SSH E2E）。

`tools/build.sh` 会校验构建工具集，并为每个缺失项打印确切的
Debian 软件包名；当你是 root 时 `./build.sh --install` 会安装
它们。无 root 的替代方案是 `tools/install-toolchain.sh`（解包
一棵可重定位的工具树并导出 `OC_TOOLS` - 用 `make OC_TOOLS=<tree>`
让 Makefile 指向它；它会探测 `<tree>/usr/...` 与 `<tree>/...`
两种布局）。

## 获取代码树

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
```

## 健全性构建

```
make all             # userprogs + kernel + ISO + src zip + OTA pkg
make test            # host unit tests
make run-bios        # boot it
```

三项全部通过，你的环境就绪。一个变更需要满足的完整门槛见
[CONTRIBUTING.md](../root/CONTRIBUTING.md)。

## 仓库布局

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
                  editor_core.h
ocos/l1apps/      one command = one app = one folder (284 programs)
                  + _i18n/ for the flat legacy test programs
ocire/            the independent recovery kernel (second L0)
tools/            build + test + docs generators
tests/            host-side unit tests (make test)
docs/             this documentation tree (en + zh mirror)
etc/              the default /etc volume content + l0 i18n
```

## 必须立刻知道的约定

- `-Wall -Wextra -Werror` - 零警告，无例外。
- 每个文件都带 SPDX 头。
- 内核/服务可见的字符串是 ASCII 英文（它们是脚本 + 向导锚点
  契约）；面向人的表面通过 `tr_()` 以 en+zh 资源渲染。
- 命令表面只增不减（接口冻结，见 CONTRIBUTING）。

## QEMU 配方

把下面这些单行命令留在手边：

```
# quick BIOS boot, serial on stdio
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 -serial mon:stdio

# UEFI
qemu-system-x86_64 -m 512M -machine q35 -bios /usr/share/ovmf/OVMF.fd \
    -cdrom build/opencube.iso -display none -serial mon:stdio

# with a scratch disk + TUI visible
qemu-system-x86_64 -m 512M -cdrom build/opencube.iso \
    -drive file=scratch.qcow2,if=ide -netdev user,id=n1 \
    -device e1000,netdev=n1

# boot log to a file
... -serial file:boot.log
```

`make run-bios`、`make run-uefi`、`make run-bios-gui`、
`make shot-bios` 封装了最常见的几种（包括 /etc 卷的设置）。
可重定位工具链：`make OC_TOOLS=<tree> run-bios`（当工具树布局
不同时再加 `OVMF_FD=...`）。
