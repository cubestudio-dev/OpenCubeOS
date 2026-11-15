<!-- SPDX-License-Identifier: Apache-2.0 -->
# 构建 Open Cube OS

下文的一切都是可执行的现实：文件名、目标与检查来自
`Makefile`、`tools/build.sh`、`tools/build.ps1` 和 CI 流水线
（`.github/workflows/build.yml`）。

## 你得到什么

| 目标      | 产出                                        |
|-------------|-------------------------------------------------|
| `make kernel` | `build/opencube.elf`——内核 ELF（0 警告，`-Wall -Wextra -Werror`） |
| `make iso`  | `build/opencube.iso`——混合 BIOS+UEFI 可启动 ISO |
| `make src-zip` | `dist/OpenCubeOS-<version>-src.zip`——确定性源码 zip |
| `make ota`  | `dist/OpenCubeOS-<version>-update.tar.gz`——OTA 更新包 |
| `make all`  | 内核 + ISO + SRC zip + OTA                    |
| `make test` | 宿主机侧单元测试（`tools/run_host_tests.py`） |
| `make reproducible-check` | 两次干净构建，逐字节相同的 ELF |
| `make api-docs` | 真实代码树的文档数据（`docs/data/*.json`） |

一键入口：`tools/build.sh`（Linux；同样的目标名，外加依赖
检测和 `--install`）、`build.ps1`（Windows，经 WSL2 /
MSYS2）。

## Linux

### 依赖

`gcc, ld (binutils), nasm, grub-mkimage (grub-common), xorriso,
mtools (mformat/mcopy), python3, zip`。

`tools/build.sh` 会检查它们并打印缺失项的确切 Debian 包名；
你是 root 时 `tools/build.sh --install` 用 apt 安装。构建
ISO 还需要 GRUB 模块树（`i386-pc` 用 `grub-pc-bin`、
`x86_64-efi` 用 `grub-efi-amd64-bin`）和 `grub-common` 的
`unicode.pf2`。

QEMU 测试：`qemu-system-x86`、`seabios`、`ipxe-qemu`、`ovmf`
（UEFI）。在 Debian/Ubuntu 宿主机上：

```
sudo apt-get install gcc nasm xorriso mtools \
     grub-common grub-pc-bin grub-efi-amd64-bin zip python3 \
     qemu-system-x86 ovmf
```

### 构建

```
git clone https://github.com/cubestudio-dev/OpenCubeOS.git
cd OpenCubeOS
tools/build.sh        # = make all
# or step by step:
make -j"$(nproc)" kernel
make iso
```

版本横幅来自 Makefile 中的 `2.2.0` 字面量（用
`make OC_RELEASE_VERSION=1.0.1-test1` 覆盖）。构建号是
提交计数加上 +21 对齐偏移（见 Makefile 注记），公开序列因此
保持在已发布的 274 之上。

### 启动与测试

```
make run-bios     # QEMU legacy BIOS boot
make run-uefi     # QEMU UEFI boot (OVMF)
make test         # host unit tests
make shot-bios    # boot screenshot capture
```

开启网络：加上 `-netdev user,id=n1 -device e1000,netdev=n1`
（`run-bios` 目标已经加了），然后在客户机里 `dhcp`。

### 可复现验证

```
make reproducible-check
# reproducible-check: PASS - ELF byte-identical across two clean builds
```

确定性来源（全部在树内验证）：无 `__DATE__`/`__TIME__`/
`__TIMESTAMP__` 宏、相同输入下稳定的 `grub-mkimage` 输出、
固定的链接顺序、`strip --strip-debug` 移除携带路径的节。SRC
zip 构造上就是确定性的（`git archive`：树序、提交 mtime、
索引权限）。OTA 包内嵌构建日（UTC）——它一天之内逐字节
可复现，这是文档声明的保证。

导出的 `SOURCE_DATE_EPOCH` 会被遵守并转发给打包
子工具。

### 交叉编译（ARM64 / RISC-V 上线 shell）

```
make ARCH=arm64    # build/arch/arm64/opencube-arm64.elf (PL011 UART shell)
make ARCH=riscv    # build/arch/riscv/opencube-riscv.elf (NS16550A UART shell)
```

这些是真实的裸机上线 shell：带真除数编程的 PL011 /
NS16550A UART、ARM 通用定时器 / RISC-V CLINT mtime
计数器，以及 1 秒心跳——横幅与递增的心跳在 QEMU 中实测
验证（`qemu-system-aarch64 -M virt`、
`qemu-system-riscv64 -M virt`）。完整内核特性集仅限
x86_64。

交叉工具链包（Debian）：
`gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu`、
`gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu`、
`qemu-system-arm`、`qemu-system-misc`。

## Windows

`build.ps1`（PowerShell）把构建转发给同一份 Makefile，从
最优的可用环境开始，顺序如下：

1. **WSL2**（推荐）——带上述 Linux 依赖的 Debian/Ubuntu
   WSL 发行版。脚本用 `wslpath` 翻译 Windows 路径并在
   WSL 里运行 `make`。
2. **MSYS2**——本地安装的 MSYS2，PATH 上有 `mingw-w64`
   gcc、nasm、mtools、xorriso、grub 包。
3. 否则脚本打印需要安装的确切包并以非零
   状态退出。

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

选项：`CONFIG_TUI`、`CONFIG_CHINESE`、`CONFIG_NVME`、`CONFIG_AHCI`、
`CONFIG_VIRTIO_BLK`。`CONFIG_CHINESE=n` 强制 `CONFIG_TUI=n`。
`CONFIG_TUI=n CONFIG_CHINESE=n` 构建会从 ush 面上摘掉 9 条 TUI
命令，并把 8 条中文 locale
SVC 操作变成干净的 -1 错误（命令体住在被过滤的树里；操作号
保持稳定）；完整配置启动 99 条 L0 命令、总共 251 个命令归属
（2.1.0 实测验证：off-path ISO 上横幅 + `help -a`）。
off-path 是真实代码，不是被删除的代码——配置器生成的
头把禁用的特性映射为 no-op 等价物，L1SVC 分发守护 locale
操作（2.1.0：v2.0.0 树曾链接不出 off-path 镜像——已修复）。

## 构建缓存

编译缓存（`tools/oc_cc.sh`）按内容寻址：键是编译器身份 +
完整参数 + 源文件及每个头依赖的内容哈希。发布树上实测：
冷 46.6 秒 -> 热 14.4 秒（186/186 个对象命中，3.2 倍），
热输出与冷输出逐字节一致。缓存控制：`make cache-stats`、
`make cache-clear`。

## 可脚本化的构建 API

`tools/build.sh` 把自己的流水线暴露为可 source 的函数
（`build_all`、`build_iso`、`config_load`、`cache_lookup`、
`cross_compile`、……），`tools/build.sh api-selftest` 验证
8 个规格化接口。发布脚本与 CI 组合它们，而不是去解析
make 输出。

## CI

`.github/workflows/build.yml` 在每次向 `main` 的推送和 PR 上
运行：检出（完整历史 + tag，`git describe` 得以喂给版本
横幅）、依赖安装、内核（`-Wall -Wextra -Werror`）、混合
ISO、确定性 SRC zip、OTA 包、宿主机测试套件、工件
摘要 + 上传，以及可复现性门禁。QEMU 启动与 E2E
套件是发布宿主机的活动（它们需要持久的 /etc
卷夹具）——完整发布门禁见 `docs/release/`。

## 故障排查

- **`grub-mkimage: cannot open ... unicode.pf2`**——字体在
  `grub-common` 里；若你的发行版拆分了它，把 `OC_TOOLS`
  指向包含 `usr/share/grub/unicode.pf2` 的目录树。
- **`src-zip: not a git checkout`**——确定性源码 zip
  需要 git 元数据；从克隆构建，或用 `make dist` 走基于
  tar 的普通回退。
- **QEMU 找不到固件**——`OC_TOOLS` 重定位整个
  工具链；QEMU 的 datadir 会在 `$OC_TOOLS/usr/share/qemu`
  和 `$OC_TOOLS/share/qemu` 下探测。
- **很新的 GCC/binutils 上的 `setc` / `packed-member` 构建
  错误**——已在树内修复；更新越过 CI 绿提交 `fc4e5a5`。
