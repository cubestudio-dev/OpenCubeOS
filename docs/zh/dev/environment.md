<!-- SPDX-License-Identifier: Apache-2.0 -->
# 开发环境

如何从一台全新机器开始进行内核开发。

## 必要条件

- Linux 主机（已测试 Debian/Ubuntu；安装了以下任何包的发行版均可）。Windows：WSL2 可提供完全相同的环境。
- `gcc`（测试 14.x）、`binutils`（ld）、`nasm` 2.16+、`xorriso`、`mtools`、`grub-common` + `grub-pc-bin` + `grub-efi-amd64-bin`、`python3`（3.9+）、`zip`。
- 用于测试：`qemu-system-x86`、`ovmf`、`seabios`、`ipxe-qemu`。
- 用于端到端测试套件：Python `pexpect`（回归测试、看门狗 E2E）和 `paramiko`（SSH E2E）。

`tools/build.sh` 会验证构建工具集并为缺失项打印确切的 Debian 包名；当你以 root 身份运行时，`./build.sh --install` 会安装这些包。无 root 权限的替代方案是 `tools/install-toolchain.sh`（解压一个可重定位的工具树并导出 `OC_TOOLS`）。

## 获取源代码树

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
```

## 基础构建验证

```
./build.sh iso        # kernel + ISO
make test             # host unit tests
make run-bios         # boot it
```

如果这三项全部通过，你的环境就配置正确了。完整的变更标准请参见 [CONTRIBUTING.md](../root/CONTRIBUTING.md)。

## 仓库布局

```
kernel/    core (sched/syscalls/...), mem (PMM/VMM/heap), lib, crypto, ota
boot/      boot.S - the 32->64-bit entry
arch/      arm64/ riscv/ cross-compilation bring-up shells
drivers/   16 families: block, nic, usb, snd, input, display, pci,
           acpi, i2c, time, wireless, bluetooth, sd, camera, misc, scsi
fs/        VFS + ramfs + fat32 + exfat + ext4
net/       net_core (stack), ssh, sshd, tls, tcp_cc
shell/     the oc> shell + all command modules
l1/        the L1 extension surface headers + solib support
tui/       the terminal UI engine and apps
chinese/   CN console layer (font/encoding/IME)
libs/      ld_so.c - the in-tree shared-object loader
userprogs/ ring-3 test programs (fork/exec/pipe/... )
tools/     build + test + docs generators
tests/     host-side unit tests (make test)
docs/      this documentation tree
```

## 重要约定

- `-Wall -Wextra -Werror` - 零警告，无例外。
- 每个文件都带有 SPDX 头。
- 内核可见的字符串为 ASCII 编码。
- 命令接口只增不减（参见 CONTRIBUTING）。

## QEMU 使用技巧

记住这些单行命令：

```
# quick BIOS boot, serial on stdio
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -netdev user,id=n1 -device e1000,netdev=n1 -serial mon:stdio

# UEFI
qemu-system-x86_64 -m 512 -machine q35 -bios /usr/share/ovmf/OVMF.fd \
    -cdrom build/opencube.iso -display none -serial mon:stdio

# with a scratch disk + TUI visible
qemu-system-x86_64 -m 512 -cdrom build/opencube.iso \
    -drive file=scratch.qcow2,if=ide -netdev user,id=n1 \
    -device e1000,netdev=n1

# boot log to a file
... -serial file:boot.log
```

`make run-bios`、`make run-uefi`、`make run-bios-gui`、`make shot-bios` 封装了常用的启动方式（包括 /etc 卷设置）。
