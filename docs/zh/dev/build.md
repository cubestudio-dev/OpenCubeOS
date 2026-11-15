<!-- SPDX-License-Identifier: Apache-2.0 -->
# 构建与打包

构建系统的开发者视图。用户级指南见仓库根部的
[BUILDING.md](../root/BUILDING.md) - 本页补充开发者工作流的
细节。

## 你真正会用到的目标

```
make -j$(nproc) kernel     # fast inner loop: ELF only
make iso                   # + bootable ISO
make userprogs             # regenerate the embedded program registry
make test                  # host unit tests (no QEMU needed)
make run-bios              # boot the ISO
make all                   # userprogs + kernel + ISO + src zip + OTA
make clean                 # start fresh
make reproducible-check    # two clean builds -> byte-identical ELF
```

## 应用内嵌链

`make userprogs`（属于 `make all`，也属于过期门禁）运行
`tools/gen_userprogs.py`：它发现每个 `ocos/l1apps/<name>/` 文件夹
+ 每个平铺的旧式程序，逐个独立环境编译（`-I ocos/interface`），
按镜像模式链接，生成每个应用的 i18n 表，并写出
`ocos/kernel/userprogs_data.h`（blob + `OC_USERPROGS[]` 注册表）。
没有任何手工注册；一个新文件夹就是一条新命令。

过期门禁：任何比生成头更新的源文件、接口头或 i18n json 都会强制
重新生成 - `make dist` 在内嵌内容过期时失败。

## 编译缓存让内层循环变快

`tools/oc_cc.sh` 用一个内容寻址缓存包装每条编译规则：
键 = 编译器标识 + 完整标志集 + 源内容哈希 + 每个头文件依赖的
内容哈希。

- 热对象与冷对象逐字节一致。
- `make cache-stats` / `make cache-clear` 管理它。
- 过期的缓存条目不可能存在：任何头文件编辑都会改变键。

## 版本号与构建号

- `OC_RELEASE_VERSION` 默认取 Makefile 中的版本字面量；可在
  命令行覆盖（`make OC_RELEASE_VERSION=3.0.0-test1 iso`），它
  会进入横幅、`uname -a`、OTA 清单和 ISO 卷 ID。
- `OC_KERNEL_BUILD` = `git rev-list --count HEAD` + 永久的
  +21 对齐偏移（`OC_KERNEL_BUILD_OFFSET := 21`）- 它在发布时
  供给 `uname` 和 update.json 的 "build" 字段。数值比较
  （BUG-0347）使 OTA 检查感知构建号。
- 版本重编译触发器（`kernel/generated/version_gen.h`，
  cmp 守卫）让 -D 标志的变化对依赖图可见 - 新代码上挂着旧横幅
  的情况不会再发生。

## 打包确定性

- **SRC zip**（`make src-zip`）：一次 `git archive` - 树序、
  提交 mtime、索引权限；同一提交逐字节一致。
- **OTA**（`make ota`）：内核 ELF + 清单；清单携带构建日
  （UTC），因此包在同一天内逐字节可复现。
- **ELF**：树中任何地方都没有 `__DATE__`/`__TIME__`/
  `__TIMESTAMP__`（一个 make 门禁会 grep 它们），固定链接顺序，
  `strip --strip-debug` 移除携带路径的节。

## 多配置与交叉构建（开发者注记）

```
make defconfig          # .config with everything on
make menuconfig         # interactive (tools/oc_menuconfig.sh)
make CONFIG_NVME=n iso  # hardware profile: NVME off
make CONFIG_AHCI=n CONFIG_VIRTIO_BLK=n iso
make ARCH=arm64         # PL011 bring-up shell ELF
make ARCH=riscv         # NS16550A bring-up shell ELF
```

配置器生成 `kernel/generated/config_gen.h`，其中是带比较守卫的
宏（3.0.0 的旋钮是硬件配置选项 NVME / AHCI / VIRTIO_BLK；TUI
与 CJK 管线始终编译在内）。被禁用的驱动改接到桩，绝不删除
代码。

## 可脚本化的 API

`tools/build.sh` 还兼作函数库：

```
source tools/build.sh
build_all; build_iso; config_load; cache_lookup; cross_compile ...
tools/build.sh api-selftest     # verifies the spec'd interfaces
```

发布自动化（见 [../release/process.md](../release/process.md)）
组合调用这些函数，而不是解析 make 输出。
