<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 指南：共享对象加载器

`libs/ld_so.c` - 一个真正的内核动态加载器，被 L1 模块和动态链接的用户程序共同使用。

## 接口面（l1/l1_wp8b.h）

```
elf_load_dynamic(elf_data, size, name)    -> load + relocate
ldso_run()                                 -> run loaded objects' init
so_load(name, flags) / so_unload(handle)
symbol_resolve(handle, name)
dlopen_impl(name, flags) / dlsym_impl(handle, name) / dlclose_impl(handle)
reloc_apply(rela, count, base, ...)
elf_get_needed(elf_data, size, ...)
```

## 处理内容

ELF 动态对象：DT_NEEDED 链表（`elf_get_needed`）、符号表、重定位处理（`reloc_apply` - R_X86_64_RELATIVE/GLOB_DAT/JUMP_SLOT/64 系列）、`ldso_run` 时的初始化数组，以及带引用处理的卸载功能。

## 功能验证

`userprogs/` 目录下的程序在 ring 3 层面测试了每个角落：`run dyn_hello`、`run so_test`、`run dlsym_test`、`run pie_test`、`run reloc_test`，并且 `ext_wp8b_selftest()` 在内核中对加载器进行了自检。如果你的模块通过此路径加载，这些就是你的回归测试基准点。

## 位置无关性

PIE 对象通过相同的 `reloc_apply` 进行重定位；加载器的基地址分配使得 `-fno-pie` 内核代码和 PIE 模块能够共存 - 具体的重定位组合请参见 `pie_test` 源码。
