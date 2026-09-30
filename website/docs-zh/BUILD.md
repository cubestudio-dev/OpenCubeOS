<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of docs/BUILD.md (website-provided, for reading
     convenience). The English original in the repository is authoritative.
     Commands, paths and identifiers are kept verbatim. -->

# Open Cube OS - WP-03 构建指南

## 前置条件

WP-03 需要一个 freestanding x86_64 工具链：C 编译器、汇编器、链接器、ISO 构建器、QEMU 与 OVMF（UEFI 固件）。在标准 Debian/Ubuntu 系统上，这些来自 `gcc`、`nasm`、`xorriso`、`grub-pc-bin`、`grub-efi-amd64-bin`、`grub-common`、`qemu-system-x86`、`ovmf`、`mtools` 与 `python3-pil`。

安装后，以下命令都必须能报告版本：

```sh
gcc --version          # 任意较新的 gcc
nasm --version         # 2.16+
xorriso --version      # 1.5+
grub-mkrescue --version # 2.12+
grub-mkimage --version  # 2.12+
qemu-system-x86_64 --version # 10.0+
# OVMF 固件文件（位置随发行版不同）：
#   Debian/Ubuntu: /usr/share/OVMF/OVMF_CODE_4M.fd 与 OVMF_VARS_4M.fd
#                  或 /usr/share/ovmf/OVMF.fd
```

如果工具不在默认系统路径（例如你把它们解压到了自定义位置），设置以下环境变量：

```sh
export OC_TOOLS=/path/to/your/toolchain/extract
export OVMF_CODE=/path/to/OVMF_CODE.fd
export OVMF_VARS=/path/to/OVMF_VARS.fd
```

构建脚本（`tools/build_iso.sh`、`tools/qemu_shot_vnc.py`）读取 `OC_TOOLS`
来定位 GRUB 模块、SeaBIOS 与 QEMU 数据文件。

## 构建

```sh
cd /path/to/oc-os
make            # 构建 build/opencube.elf
make iso        # 构建 build/opencube.iso（BIOS+UEFI 混合）
```

`iso` 目标调用 `tools/build_iso.sh`，它会：

1. 把内核 ELF、`grub.cfg`、unicode 字体以及 GRUB 的 `i386-pc` 与 `x86_64-efi` 模块暂存到 `build/iso-stage/`。
2. 调用 `grub-mkimage -O i386-pc-eltorito` 构建 BIOS El Torito 引导镜像。
3. 调用 `grub-mkimage -O x86_64-efi` 构建 `EFI/BOOT/BOOTX64.EFI`。
4. 构建一个包含 `EFI/BOOT/BOOTX64.EFI` 的 1 MiB FAT 镜像——即 EFI 系统分区（ESP）。
5. 调用 `xorriso -as mkisofs`，同时带 `-b`（BIOS El Torito）与 `-e`（UEFI ESP）选项，生成最终的混合 ISO。

得到的 ISO 有 **两个** El Torito 启动项：

```
El Torito boot img :   1  BIOS  y   none  0x0000  0x00      4         579
El Torito boot img :   2  UEFI  y   none  0x0000  0x00   2048          67
```

BIOS 固件选第 1 项，UEFI 固件选第 2 项。

## 运行

```sh
make run-bios    # SeaBIOS，经 -cdrom
make run-uefi    # OVMF，经 -cdrom（带 -boot d）
```

两个目标都把内核的串口输出转储到 stdout。内核的 COM1（端口 0x3F8）接到了控制台输出钩子上，所以画到 framebuffer 的每一行也同时 tee 到串口。

## 截图

```sh
make shot-bios   # -> build/shot-bios.png  (800x600)
make shot-uefi   # -> build/shot-uefi.png  (800x600)
```

这些使用 `tools/qemu_shot_vnc.py`：以 VNC 显示启动 QEMU，等待内核完成引导，然后向 QEMU 的 monitor socket 发送 `screendump`。PPM 经 PIL 转为 PNG。

## 分发

```sh
make dist
```

产出：

- `dist/OpenCubeOS-src-WP<NN>-<timestamp>.zip` - 源码树（排除 `build/`、`dist/`、`.git/`）
- `dist/OpenCubeOS-WP<NN>-<timestamp>.iso` - `build/opencube.iso` 的副本
- `dist/OpenCubeOS-WP<NN>-<timestamp>.iso.sha256` - SHA256 校验和

## 清理

```sh
make clean
```

删除 `build/`、`dist/` 与 `iso/` 下暂存的内核 ELF。
