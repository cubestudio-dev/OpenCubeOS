<!-- SPDX-License-Identifier: Apache-2.0 -->
# 常见错误及其修复

## 构建

| 症状 | 原因 | 修复 |
|---------|-------|-----|
| `grub-mkimage: cannot open ... unicode.pf2` | 字体在 `grub-common` 里 | 安装它 / 把 OC_TOOLS 指向包含 `usr/share/grub/unicode.pf2` 的目录树 |
| `src-zip: not a git checkout` | 没有 git 元数据 | 从克隆构建，或使用 `make dist` |
| `error: operand size mismatch for 'setc'` | 新 GAS 上的修复前代码树 | 更新越过提交 fc4e5a5（u8 标志） |
| 新 GCC 上的 packed-member 警告 | 修复前代码树 | 更新越过 fc4e5a5（packed 标签类型） |
| `embed_grub: missing /usr/usr/lib/grub...` | 修复前脚本 | 更新越过 fc4e5a5（双布局探测） |

## 启动

| 症状 | 含义 | 下一步 |
|---------|---------|-----------|
| 没有 `multiboot2 handshake OK` | OCBL 没有交出有效的 mbi | 检查 ISO 启动的是否为正确条目（OCBL 菜单）；捕获串口 |
| 启动停在某个非 OK 行 | 该子系统失败 | 找到实现它的文件——读它的初始化错误路径 |
| 三重故障/复位循环 | 转储路径可用之前的早期异常 | `-d int,cpu_reset -D qemu.log` + 经 `-s -S` 接入的 gdb |
| 启动菜单选错槽位 | A/B 标志过期 | 从菜单强制另一个槽位，然后 `update rollback` |

## 运行期

| 症状 | 含义 | 下一步 |
|---------|---------|-----------|
| `mount` 拒绝有效的 FAT32 卷 | BPB 检查失败 | `fatstat <dev>` 显示解析出的 BPB；设备搞错了？ |
| 网卡沉默 | 驱动未绑定 | `lspci` + 家族命令（`e1000`…） |
| TCP 连上后停摆 | 校验和/重构回归 | `tcpstats`、`tcptest` |
| U 盘缺失 | USB MSC 未绑定 | `usb`、`usb_storage_test` |
| 干净卸载后 fsck 报 dirty | FSINFO/FAT 镜像回归 | 上报它——这一类就是 bug |

## 测试

- SSH E2E 需要预置的主机密钥（[../dev/e2e-suites.md](../dev/e2e-suites.md)
  中的 mtools 配方）。
- HTTPS E2E 需要参考服务器可达。
- 回归套件需要 pexpect；SSH 套件需要 paramiko。
