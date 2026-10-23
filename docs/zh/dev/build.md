<!-- SPDX-License-Identifier: Apache-2.0 -->
# 构建与打包

这里是构建系统的开发者视角。用户级指南见仓库根目录的
[BUILDING.md](../root/BUILDING.md)——本页补充开发者工作流的细节。

## 你真正会用的目标

```
make -j$(nproc) kernel     # fast inner loop: ELF only
make iso                   # + bootable ISO
make test                  # host unit tests (no QEMU needed)
make run-bios              # boot the ISO
make clean                 # start fresh
make reproducible-check    # two clean builds -> byte-identical ELF
```

## 编译缓存让内循环飞快

`tools/oc_cc.sh` 给每条编译规则包了一层内容寻址缓存：
键 = 编译器身份 + 完整旗标集 + 源文件内容哈希
+ 每个头文件依赖的内容哈希（依赖探测是一次 `gcc -MM -MG` 扫描，并剥掉
-MMD/-MP 旗标，让两者互不打架）。

- 发布树冷构建：46.6 秒。热构建：14.4 秒（186/186 命中，
  3.2 倍提速）。热对象与冷对象逐字节一致。
- `make cache-stats` / `make cache-clear` 负责管理它。
- 过期缓存条目不可能存在：任何头文件改动都会改变键。

## 版本号与构建号

- `OC_RELEASE_VERSION` 默认取 `git describe --tags`；在命令行
  覆盖（`make OC_RELEASE_VERSION=WP-14-test1 iso`），否则它会
  进入横幅、`uname -a`、OTA 清单与 ISO 卷标。
- 构建号是提交计数（`git rev-list count`）——它供给 `uname`
  以及发布时 update.json 的 "build" 字段。

## 打包确定性

- **SRC zip**（`make src-zip`）：一次 `git archive`——目录树
  顺序、提交时间戳、索引权限全固定；同一提交产出逐字节一致。
  经由 `SRC_EXCLUDE` 排除 website/out/build/deliverables 树。
- **OTA**（`make ota`）：内核 ELF + 清单；清单携带构建日
  （UTC），使包在同一天内逐字节可复现。
- **ELF**：全树任何位置都没有 __DATE__/__TIME__/__TIMESTAMP__
  （一道 make 门禁专门 grep 它们）、链接顺序固定、`strip
  --strip-debug` 移除携带路径的节。

## 多配置与交叉构建（开发者须知）

```
make defconfig          # .config with everything on
make menuconfig         # interactive (tools/oc_menuconfig.sh)
make CONFIG_TUI=n CONFIG_CHINESE=n iso    # 216-command image
make ARCH=arm64         # PL011 bring-up shell ELF
make ARCH=riscv         # NS16550A bring-up shell ELF
```

配置器生成带比较保护的宏到 `kernel/generated/config_gen.h`；
被关闭的特性会路由到真正的 ASCII 等价物（console_cn ->
screen_console），而不是删代码。GNU make 的 `%` 通配符限制
（一条模式里只有第一个 `%` 生效）正是 TUI/中文源码过滤器长成
那个样子的原因——不要去"简化"它们。

## 可脚本化 API

`tools/build.sh` 同时是一个函数库：

```
source tools/build.sh
build_all; build_iso; config_load; cache_lookup; cross_compile ...
tools/build.sh api-selftest     # verifies the 8 spec'd interfaces
```

发布自动化（见 [../release/process.md](../release/process.md)）
直接组合这些函数，而不是去解析 make 输出。
