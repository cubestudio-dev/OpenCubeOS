<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 cubestudio-dev <cubestudio@qq.com> -->
<!-- Chinese translation of docs/BUILD.md (website-provided, for reading
     convenience). The English original in the repository is authoritative.
     Commands, paths and identifiers are kept verbatim.
     Structurally re-aligned with the current English original at
     WP-10-AUDIT_P2-fix3 (fix3 G5, BUG-0270). -->

# Open Cube OS - WP-03 构建指南

## 前置条件

WP-03 需要一套 freestanding x86_64 工具链：C 编译器、汇编器、链接器、
ISO 构建器、QEMU 与 OVMF（UEFI 固件）。在原生 Debian/Ubuntu 系统上，
它们来自 `gcc`、`nasm`、`xorriso`、`grub-pc-bin`、`grub-efi-amd64-bin`、
`grub-common`、`qemu-system-x86`、`ovmf`、`mtools` 与 `python3-pil`。

安装完成后，以下命令都必须能报告版本：

```sh
gcc --version          # 任意较新的 gcc
nasm --version         # 2.16+
xorriso --version      # 1.5+
grub-mkrescue --version # 2.12+
grub-mkimage --version  # 2.12+
qemu-system-x86_64 --version # 10.0+
# OVMF 固件文件（路径随发行版而异）：
#   Debian/Ubuntu: /usr/share/OVMF/OVMF_CODE_4M.fd 和 OVMF_VARS_4M.fd
#                  或 /usr/share/ovmf/OVMF.fd
```

如果你的工具不在默认系统路径（例如解压到了自定义位置），请设置以下
环境变量：

```sh
export OC_TOOLS=/path/to/your/toolchain/extract
export OVMF_CODE=/path/to/OVMF_CODE.fd
export OVMF_VARS=/path/to/OVMF_VARS.fd
```

构建脚本（`tools/build_iso.sh`、`tools/qemu_shot_vnc.py`）读取
`OC_TOOLS` 来定位 GRUB 模块、SeaBIOS 与 QEMU 数据文件。

## 构建

```sh
cd /path/to/oc-os
make            # 构建 build/opencube.elf
make iso        # 构建 build/opencube.iso（BIOS+UEFI 混合）
```

`iso` 目标调用 `tools/build_iso.sh`，它：

1. 把内核 ELF、`grub.cfg`、unicode 字体与 GRUB 的 `i386-pc`、
   `x86_64-efi` 模块暂存到 `build/iso-stage/`。
2. 调用 `grub-mkimage -O i386-pc-eltorito` 构建 BIOS El Torito 引导镜像。
3. 调用 `grub-mkimage -O x86_64-efi` 构建 `EFI/BOOT/BOOTX64.EFI`。
4. 构建一个 1 MiB FAT 镜像，内含 `EFI/BOOT/BOOTX64.EFI`
   ——即 EFI 系统分区（ESP）。
5. 调用 `xorriso -as mkisofs`，同时带 `-b`（BIOS El Torito）与
   `-e`（UEFI ESP）选项，产出最终的混合 ISO。

产出的 ISO 有**两个** El Torito 引导项：

```
El Torito boot img :   1  BIOS  y   none  0x0000  0x00      4         579
El Torito boot img :   2  UEFI  y   none  0x0000  0x00   2048          67
```

BIOS 固件选第 1 项，UEFI 固件选第 2 项。

## 持久化 /etc 卷（build/etc.img）——每次测试运行前重建，绝不入库

SSH 与 HTTPS 端到端套件（`tools/ssh_mainstream_test.py`、
`tools/https_test_server.py` 运行、`tools/test_bug0132_ctrlc.py`）会把一个
持久化 `/etc` 卷作为第二块 IDE 盘挂到 QEMU 客户机：

```sh
-drive if=ide,format=raw,file=build/etc.img
```

规格与用途：

- **32 MiB FAT32 镜像，卷标 `OCS_ETC`。** 内核在启动时把它挂载为
  `/etc`（WP-09-fix5 `lib_config_init()`）；没有它，内核回退到
  ramfs `/etc`。
- 存放 `/etc/opencube.conf` 以及每台安装各自的 SSH 材料
  （BUG-0075 信任模型）：`ssh_host_key`（sshd 主机密钥）、
  `ssh_client_key`（公钥认证的客户端身份）与
  `ssh_authorized_keys`（sshd 信任的授权公钥）。

为什么它不进 git：`.gitignore` 排除整个 `build/` 树（构建/运行期产物），
且 `etc.img` 携带必须**按安装各自生成**的私钥材料。提交它等于给每个
checkout 发同一把共享主机密钥——这正是 p1fix2 的 BUG-0075 修复从内核中
移除的东西。把它当作生成的机器身份：本地重建。

全新 clone / 沙箱重置后请重建（在仓库根目录运行，
`mtools` 在 `$PATH` 或 `$OC_TOOLS/usr/bin` 下）：

```sh
mformat -i build/etc.img -C -F -T 65536 -v OCS_ETC   # 65536 x 512 B = 32 MiB, FAT32 (-F)
python3 tools/host_keygen_etc.py                     # 主机 RSA-2048 密钥生成 + 注入
                                                     # ssh_host_key / ssh_client_key /
                                                     # ssh_authorized_keys
```

没有 `etc.img` 时，sshd 首次启动会落入客户机一次性 Miller-Rabin
密钥生成（TCG 下需数分钟），通常拖到测试 harness 的横幅等待超时
——一个 0/5 的 SSH 矩阵结果属于环境问题，不是内核回归。有了它，
sshd 用预注入密钥立即启动。

## 运行

```sh
make run-bios    # 经 -cdrom 的 SeaBIOS
make run-uefi    # 经 -cdrom 的 OVMF（带 -boot d）
```

两个目标都把内核串口输出转储到 stdout。内核的 COM1（端口 0x3F8）
接在控制台输出钩子上，因此 framebuffer 上画的每一行也会同步打到串口。

## 截图

```sh
make shot-bios   # -> build/shot-bios.png  (800x600)
make shot-uefi   # -> build/shot-uefi.png  (800x600)
```

它们使用 `tools/qemu_shot_vnc.py`：以 VNC 显示启动 QEMU，等内核
引导完成后向 QEMU monitor socket 发送 `screendump`。PPM 经 PIL
转成 PNG。

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

移除 `build/`、`dist/` 与 `iso/` 下暂存的内核 ELF。
