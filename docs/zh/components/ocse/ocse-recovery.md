<!-- SPDX-License-Identifier: Apache-2.0 -->
# OCSE：安装失败与恢复

安装出问题时会发生什么，以及为什么中断的安装不会留下一个写了一半
的系统。

## 错误页

向导运行的每条 OCSECL 命令都返回状态。首条失败的命令使向导停止：
错误页显示

- 失败的**命令行**（逐字复制），以及
- 该命令**捕获的输出**（与 shell 会打印的文本相同），

外加 **Retry** 与 **Back**。Retry 重新运行失败阶段；Back 返回
前一页。错误页驻留期间机器上的其他内容不受影响。

## 失败模式与处置

| 位置 | 失败 | 处置 |
|---|---|---|
| 第 2 步 | 目标盘已能启动 `opencube.elf` | 任何写入之前即拒绝——既有系统原封不动 |
| 第 3 步 | 没有足够大的空闲间隙 | 向导如实说明；不写入任何内容 |
| 阶段 1 | 此前失败重试残留的挂载导致 `mount` 失败 | 向导丢弃残留挂载（`umount /mnt`）后**重试一次**挂载，再失败才放弃 |
| 阶段 2 | `install` 内部的介质检查、恢复分区划分、解包、引导代码或引导标志失败 | 子步骤自己的 FATAL 文本指明失败点（`extraction failed` / `/boot/opencube.elf missing after extraction` / `ocbl-install failed` / `could not set the boot flag`）；引导代码只在解包完全成功（已验证 `/boot/opencube.elf` 存在）之后才写入 |
| 阶段 3/4 | 配置值或用户记录装不下 | 明确报错拒绝（有界构建器——绝不截断、绝不溢出） |
| 阶段 5/6 | `sync`/`umount` 失败 | 经错误页重试；已安装的文件已在盘上，之后从 OCSECL 手动 `sync` 即可收尾 |

## 用 OCSECL 手工恢复

因为向导只是 OCSECL 之上的控制器，失败的安装可以从 shell 检查
并手工完成：

```
lsdisk                       选定目标
lspart hd0                   向导创建了什么？
mount hd0p2 /mnt             挂载已安装分区
ls /mnt                      检查写入了什么
cat /mnt/etc/opencube.conf   已写入的配置键
config set hostname mybox    补齐缺失的键
sync                         刷盘
umount /mnt                  完成——重启
```

`hwcheck` 随时重新运行四项硬件检查——磁盘行为异常时的第一
选择。

## 目标已有系统时

既有系统拒绝是外层守卫：OCSE 绝不格式化、重分区或安装到已能启动
`opencube.elf` 的分区上。要有意重装旧的 Open Cube OS，请先删除旧
分区（`rmpart <disk> <n> confirm`）或另选目标——两者都是显式的、
需确认的动作。

## `install` 的写入顺序

阶段 2 内部，`install` 命令按固定顺序执行，首个 FATAL 即中止：
可选的专用 OCIRE 恢复分区划分（布局需要时，系统卷上保留
`boot/ocire.elf`）、载荷 tar 解包（以 `/boot/opencube.elf` 存在为
验证）、OCBL 引导代码（`ocbl-install`——镜像有效性、首分区前的
空间、stage1 + core 校验），最后是所建分区的引导标志。

## 安装成功之后

末页提供**重启**。装好的磁盘包含：OCOS 载荷、OCBL 引导代码
（`ocbl-install` 已在 `install` 内运行）、所建分区的引导标志、
配置键与用户记录。A/B 恢复盘布局的恢复盘另行准备（见 OCBL
页面）——OCSE 的 `install` 面向普通单系统布局。

另见：[OCBL 故障排查与状态码](../ocbl/ocbl-troubleshooting.md)——
已安装系统头几次启动时引导加载器会报告什么。
