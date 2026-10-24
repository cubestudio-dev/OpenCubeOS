<!-- GENERATED mirror of the repository-root BUILDING.md - maintained by tools/gen_api_docs.py (make api-docs); the source of truth is the root file. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# 构建 Open Cube OS

以下所有内容都是可执行的实体：文件名、目标及检查项均来自 `Makefile`、`tools/build.sh`、`tools/build.ps1` 和 CI 管道（`.github/workflows/build.yml`）。

## 构建产物

| 目标         | 产物                                                   |
|--------------|--------------------------------------------------------|
| `make kernel` | `build/opencube.elf` - 内核 ELF（0 警告，`-Wall -Wextra -Werror`） |
| `make iso`    | `build/opencube.iso` - 混合 BIOS+UEFI 可启动 ISO      |
| `make src-zip` | `dist/OpenCubeOS-<version>-src.zip` - 确定性源码压缩包 |
| `make ota`    | `dist/OpenCubeOS-<version>-update.tar.gz` - OTA 更新包 |
| `make all`    | 内核 + ISO + 源码压缩包 + OTA                         |
| `make test`   | 宿主侧单元测试（`tools/run_host_tests.py`）           |
| `make reproducible-check` | 两次干净构建，字节完全一致的 ELF |
| `make api-docs` | 真实树文档数据（`docs/data/*.json`） |

一键式入口：`tools/build.sh`（Linux；相同的目标名称，附带依赖检测和 `--install`）、`build.ps1`（通过 WSL2 / MSYS2 在 Windows 上运行）。

## Linux

### 依赖项

`gcc, ld (binutils), nasm, grub-mkimage (grub-common), xorriso,
mtools (mformat/mcopy), python3, zip`。

`tools/build.sh` 会检查所有依赖项，并打印任何缺失依赖的确切 Debian 包名；当你是 root 用户时，`./build.sh --install` 会通过 apt 安装它们。构建 ISO 额外需要 GRUB 模块树（`i386-pc` 需要 `grub-pc-bin`，`x86_64-efi` 需要 `grub-efi-amd64-bin`）和来自 `grub-common` 的 `unicode.pf2`。

用于 QEMU 测试：`qemu-system-x86`, `seabios`, `ipxe-qemu`, `ovmf`
(UEFI)。在 Debian/Ubuntu 主机上：

```
sudo apt-get install gcc nasm xorriso mtools \
     grub-common grub-pc-bin grub-efi-amd64-bin zip python3 \
     qemu-system-x86 ovmf
```

### 构建

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
./build.sh            # = make all
# or step by step:
make -j"$(nproc)" kernel
make iso
```

版本横幅来自 Makefile 中的 `1.0.0` 字面量（可通过 `make OC_RELEASE_VERSION=1.0.1-test1` 覆盖）。构建号是提交计数。

### 启动与测试

```
make run-bios     # QEMU legacy BIOS boot
make run-uefi     # QEMU UEFI boot (OVMF)
make test         # host unit tests
make shot-bios    # boot screenshot capture
```

启用网络：添加 `-netdev user,id=n1 -device e1000,netdev=n1`（`run-bios` 目标已添加），然后在客户机内执行 `dhcp`。

### 确定性验证

```
make reproducible-check
# reproducible-check: PASS - ELF byte-identical across two clean builds
```

确定性源码（全部在树内验证）：无 `__DATE__`/`__TIME__`/`__TIMESTAMP__` 宏，相同输入下稳定的 `grub-mkimage` 输出，固定的链接顺序，`strip --strip-debug` 移除带路径的段。源码压缩包通过构造保证确定性（`git archive`：树顺序、提交 mtimes、索引权限）。OTA 包包含 UTC 构建日期 - 在同一天内可字节完全复现，这是文档保证的。

当导出并传递给打包子工具时，`SOURCE_DATE_EPOCH` 会被遵守。

### 交叉编译（ARM64 / RISC-V 点亮 Shell）

```
make ARCH=arm64    # build/arch/arm64/opencube-arm64.elf (PL011 UART shell)
make ARCH=riscv    # build/arch/riscv/opencube-riscv.elf (NS16550A UART shell)
```

这些是真实的裸机点亮 Shell：带有真实除数编程的 PL011 / NS16550A UART、ARM 通用定时器 / RISC-V CLINT mtime 计数器，以及 1 秒心跳 - 在 QEMU 中实时验证横幅和递增的心跳（`qemu-system-aarch64 -M virt`、`qemu-system-riscv64 -M virt`）。完整的内核功能集仅限 x86_64。

交叉工具链包（Debian）：
`gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu`,
`gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu`,
`qemu-system-arm`, `qemu-system-misc`。

## Windows

`build.ps1`（PowerShell）将构建转发给来自最佳可用环境的同一个 Makefile，顺序如下：

1. **WSL2**（推荐）- 一个包含上述 Linux 依赖项的 Debian/Ubuntu WSL 发行版。脚本使用 `wslpath` 转换 Windows 路径并在 WSL 内部运行 `make`。
2. **MSYS2** - 本地安装的 MSYS2，其 PATH 上有 `mingw-w64` gcc、nasm、mtools、xorriso、grub 包。
3. 否则脚本会打印确切的安装包并退出非零状态码。

```
powershell -ExecutionPolicy Bypass -File build.ps1            # full pipeline
powershell -ExecutionPolicy Bypass -File build.ps1 iso        # kernel + ISO
```

## 多配置构建

```
make defconfig          # write .config (all options on)
make menuconfig         # interactive configurator (tools/oc_menuconfig.sh)
make CONFIG_TUI=n       # one-shot override
```

选项：`CONFIG_TUI`, `CONFIG_CHINESE`, `CONFIG_NVME`, `CONFIG_AHCI`,
`CONFIG_VIRTIO_BLK`。`CONFIG_CHINESE=n` 会强制 `CONFIG_TUI=n`。
`CONFIG_TUI=n CONFIG_CHINESE=n` 的构建启动时有 216 条命令（233 - 17 条 TUI/中文命令）；完整配置启动时有 233 条。禁用路径是真实代码，而非被删除的代码 - 配置器生成的头文件将禁用的功能映射为空操作等价物。

## 构建缓存

编译缓存（`tools/oc_cc.sh`）是内容寻址的：键是编译器身份 + 完整标志 + 源码及所有头文件依赖的内容哈希。在发布树上测量：冷启动 46.6 秒 -> 热启动 14.4 秒（186/186 个对象命中，3.2 倍加速），热启动输出字节与冷启动完全一致。缓存控制：`make cache-stats`, `make cache-clear`。

## 可脚本化的构建 API

`tools/build.sh` 将其管道暴露为可源化的函数（`build_all`, `build_iso`, `config_load`, `cache_lookup`,
`cross_compile`, ...），并通过 `tools/build.sh api-selftest` 验证规定的 8 个接口。发布脚本和 CI 组合这些函数而非解析 make 输出。

## CI

`.github/workflows/build.yml` 在每次推送到 `main` 和 PR 时运行：检出（完整历史 + 标签以便 `git describe` 提供版本横幅）、依赖安装、内核（`-Wall -Wextra -Werror`）、混合 ISO、确定性源码压缩包、OTA 包、宿主测试套件、摘要 + 上传，以及可复现性门禁。QEMU 启动和 E2E 套件是发布主机活动（它们需要持久化的 /etc 卷固件）- 完整发布门禁见 `docs/release/`。

## 故障排查

- **`grub-mkimage: cannot open ... unicode.pf2`** - 字体位于 `grub-common`；如果发行版将其分割，请将 `OC_TOOLS` 指向包含 `usr/share/grub/unicode.pf2` 的目录树。
- **`src-zip: not a git checkout`** - 确定性源码压缩包需要 git 元数据；从克隆构建，或使用 `make dist` 作为基于 tar 的后备方案。
- **QEMU 找不到固件** - `OC_TOOLS` 会移动整个工具链；QEMU 的数据目录会在 `$OC_TOOLS/usr/share/qemu` 和 `$OC_TOOLS/share/qemu` 下探测。
- **在非常新的 GCC/binutils 上出现 `setc` / `packed-member` 构建错误** - 已在树内修复；更新至 CI 通过的提交 `fc4e5a5` 之后。
