<!-- SPDX-License-Identifier: Apache-2.0 -->
# 共享对象加载器

`ocos/l1apps/ld_so.c` - 树内动态加载器（本身是一个 ring-3 程序，
经 `ld_so.ld` 链接在 0x10000000），供动态链接的用户程序使用。

## 它处理什么

ELF 动态对象：DT_NEEDED 链、符号表、重定位处理
（R_X86_64_RELATIVE / GLOB_DAT / JUMP_SLOT / 64 各族）、加载时
的 init 数组，以及带引用处理的卸载。

## 镜像模式

`tools/gen_userprogs.py` 按程序保留 v2.x 的构建模式：静态应用
用 ET_EXEC，`dyn_hello` / `pie_test` / `so_test` / `dlsym_test` /
`reloc_test` 用 ET_DYN + `PT_INTERP=/lib/ld.so`（内核拒绝把
PIE 程序静默降级为 ET_EXEC - 镜像类型是接口的一部分）。

## 它能工作的证据

内嵌机队从 ring 3 演练每个角落：

```
ush> run dyn_hello           (loaded, init ran, hello printed)
ush> run so_test             (foo_add(2,3)=5 through the solib)
ush> run dlsym_test          (dlsym resolves foo_add(7,8)=15)
ush> run pie_test            (loaded at 0x... - PIE base assignment)
ush> run reloc_test          (foo_add=30 foo_global=42 fp=300)
```

外加 `l1test` 的 `ext_wp8b selftest 14/14` - 搭乘每次回归运行的
加载器自检。如果你的程序经这条路径加载，这些就是你的回归
锚点。

## 位置无关

PIE 对象经同一套重定位流程重定位；加载器的基址分配让
`-fno-pie` 应用与 PIE 程序共存于一个注册表 - `pie_test` 的
源码展示了一个动态程序可能携带的确切重定位组合。

## libfoo 这一环

`libfoo.c`（内嵌 solib）由 `tools/build_solib.py` 构建进
`solib_data.h` - 与其他每个程序相同的内嵌链，因此过期门禁
同样覆盖它。
