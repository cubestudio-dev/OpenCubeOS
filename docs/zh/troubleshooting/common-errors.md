<!-- SPDX-License-Identifier: Apache-2.0 -->
# 常见错误与修复

## 构建

| 症状 | 原因 | 修复方法 |
|---------|-------|-----|
| `grub-mkimage: cannot open ... unicode.pf2` | 字体文件位于 `grub-common` 中 | 安装它 / 将 OC_TOOLS 指向包含 `usr/share/grub/unicode.pf2` 的目录树 |
| `src-zip: not a git checkout` | 没有 git 元数据 | 从克隆版本构建或使用 `make dist` |
| `error: operand size mismatch for 'setc'` | 新版 GAS 上的前缀树 | 更新到 fc4e5a5 提交之后（u8 标志） |
| 新版 GCC 上的 packed-member 警告 | 前缀树 | 更新到 fc4e5a5 提交之后（packed 标记类型） |
| `embed_grub: missing /usr/usr/lib/grub...` | 前缀脚本 | 更新到 fc4e5a5 提交之后（双布局探测） |

## 启动

| 症状 | 含义 | 下一步 |
|---------|---------|-----------|
| 没有 `multiboot2 handshake OK` | GRUB 没有传递有效的 mbi | 检查 ISO 是否启动了正确的条目；捕获串口输出 |
| 启动停在非 OK 行 | 该子系统失败 | 实现它的文件 - 阅读其初始化错误路径 |
| 三重故障/复位循环 | 在转储路径工作前出现早期异常 | `-d int,cpu_reset -D qemu.log` + 通过 `-s -S` 使用 gdb |
| GRUB 菜单启动了错误的槽位 | 过期的 A/B 标志 | 从菜单强制选择另一个槽位，然后执行 `update rollback` |

## 运行时

| 症状 | 含义 | 下一步 |
|---------|---------|-----------|
| `mount` 拒绝有效的 FAT32 卷 | BPB 检查失败 | `fatstat <dev>` 显示解析的 BPB；设备错误？ |
| 网卡静默 | 驱动未绑定 | `lspci` + 家族命令（`e1000`...） |
| TCP 连接后停滞 | 校验和/重构回归 | `tcpstats`, `tcptest` |
| `usda` 缺失 | USB MSC 未绑定 | `usb`, `usb_storage_test` |
| fsck 报告在干净卸载后仍为脏 | FSINFO/FAT 镜像回归 | 报告此问题 - 这属于一类错误 |

## 测试

- SSH E2E 需要预置的 /etc 卷（`host_keygen_etc`）。
- HTTPS E2E 需要可访问的参考服务器。
- 回归测试套件需要 pexpect；SSH 套件需要 paramiko。
