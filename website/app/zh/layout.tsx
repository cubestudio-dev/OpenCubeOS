// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>

import type { Metadata } from "next";
import Chrome from "@/app/_components/Chrome";

export const metadata: Metadata = {
  title: "Open Cube OS - 开源 x86_64 L0 内核",
  description:
    "Open Cube OS：开源 x86_64 内核（L0），可被扩展成任何东西。WP-AUDIT-01 完成 18 路逐行完整审查（607 条发现）：41 个 P0 全部修复（p0fix1 前 20 + p0fix2 后 21），P1 全部 94 条修复完成（p1fix1 前 31 条 + p1fix2 第 32~62 条 + p1fix3 剩余 31 条：A/B 更新目标槽修正、OTA 整数/栈加固、恒定时间 EC 标量乘与 modexp、GPT/NVMe/ATA 边界、回写缓存不再静默丢数据、umount / 防护、PCI BAR 校验、网卡探测/假发送/描述符布局/EOR/回收门、virtio-snd 能力解析、HDA 音量真生效、sshd 阻塞 accept 可 ^C 取消、调度器 100ms 交互上限 + kill 就绪 tid、真正的双模式 vi）；p1fix4 修复 HTTPS E2E 中途断连（IP 字面量参考身份，RFC 6125，OpenSSL 真对端实测握手/GET/保存/关闭全通），每条修复均有 QEMU/宿主/构造级验证；WP-10-wp08fix1 Shell 补全（oc>/ush 双端行编辑 + 15 工具 + nano/vi 编辑器）；USB 四控制器、声卡六族、系统内 A/B 更新、网卡九族/存储四驱动；SSH + TLS 1.3 / TLS 1.2 + HTTPS；P2 第一批 53 条（WP-10-AUDIT_P2-fix1，BUG-0136..0188）已全部修复：内存/arch/USB 33 条/VFS-FAT 11 条，并发现修复堆块头 40 字节潜伏缺陷（负载对齐漂移致 ATA-DMA 偏移 8 字节、/etc 无法挂载）；BIOS + UEFI 双引导，P2 第二批 53 条（WP-10-AUDIT_P2-fix2，BUG-0189..0241）已全部修复：fs 12 条 / net_core 13 条 / ld_so 4 条 / ssh 10 条 / tls 8 条 / shell 6 条，并完成发布链四项加固（git describe 版本串、HTTPS E2E 双向 close_notify 断言、INTERFACES.md 176 命令、docs/verification/ 转公开）与嵌入链 Makefile 规则；P2 第三批 53 条（WP-10-AUDIT_P2-fix3，BUG-0242..0294）已全部修复：shell 6 / 混合 6 / 文本工具 5 / 文档 7 / 卫生 8 / syscall-usermode 7 / 调度同步 6 / RTC 电源定时 5 / crypto 3，并查明并修复了 G6 证据阶段暴露的内核 #UD 根因（sys_fork 从不设置子任务 rsp0，TSS 保留父任务栈顶致父子内核栈交叉污染，fork 路径补设 rsp0 修复）；P2 第四批 55 条（WP-10-AUDIT_P2-fix4，BUG-0295..0349）已全部修复：crypto fail-closed 4 条、tar/install/download 6 条、A/B 启动预算 1 条、块层 9 条、ATA/AHCI/NVMe 3 条、控制台/输入/PCI 锁 10 条、NIC 10 条、声卡/shell/版本 12 条，并重建 fix3 发布 ISO（tag 后构建，uname 实测 WP-10-AUDIT_P2-fix3）；112,589 行源码，138 个 L1 扩展接口，46 个系统调用，192 条 shell 命令，41/41 P0 修复测试。WP-10-input_chipset 落地输入+芯片组五域（I2C-HID 触摸板手势、PCI/PCIe MMCONFIG+BAR+经 xAPIC 的 MSI、ACPI 表+AML+S3/S4/S5+GPE、SMBus block 协议、I2C PIIX4/i801/DesignWare），新增 16 条 shell 命令（总数 192），芯片组 E2E 在 BIOS 与 UEFI 双启动均 28/28 实测通过，另全量回归 28/28、dhtest 5/5、SSH E2E 5/5、HTTPS E2E 7/7，许可证恢复为官方 Apache-2.0 逐字节文本。WP-10-tui-qa 对统一桌面完成全量质检：上轮 22 项待复核发现三通道复核后 20 项确认修复、1 项撤回误报、1 项入 KNOWN_ISSUES——含 P1 LSI SCRIPTS 缓冲溢出、P1 X509 根搜索悬垂指针（现代真实证书链崩溃，修复后真链验证）、P2 串口 TUI 输入一字节滞后（Esc 后纯键滞后，tui-test 90→93）；新增五应用深驱动 E2E（34 检查×双启动：Files 真 VFS 操作、Editor 真 nano 往返、ring-3 ush 受控、控制面板全分支、F2 热切换）与帧缓冲截图门禁（像素级 CJK 验证 + 放大导出 + VLM 亲读）；门禁全绿：0E0W、回归 28/28、TUI E2E 37/37×2、中文 E2E 21/21×2、深驱动 34/34×2、截图 4/4×2、SSH 5/5、HTTPS 7/7。WP-10-tui-native 完成 TUI 应用原生补全：原生编辑器（多行缓冲、F4 拼音输入法、保存/另存/重载往返、cat 逐字节 UTF-8 回读）与文件管理器完成体（新建/重命名/复制/移动/删除全路径 + 确认弹窗 + 属性页 + 内置查看器 + 挂载页）、数字 1-9 快开对齐、焦点/生命周期修复、全局中文化扫尾（终端横幅、桌面初始化错误、扩展实验室双话）与删除取消交互修复（取消不再退出整个应用）；新增 36 检查原生 E2E × BIOS/UEFI 与隔离场景验证器（编辑器 ASCII+CJK 往返、zh 终端横幅、delete-cancel、tz 持久化）；门禁：0E0W、回归 28/28、tui-test 93/93、原生 E2E 31/36×2（5 项长会话抖动隔离复跑 6/6，QEMU 16550 串口丢键为宿主限制、内核无损，KNOWN_ISSUES 15.12）。WP-11-crash_recovery 落成完整崩溃恢复层：统一 Panic 捕获（横幅+16 寄存器快照+调用栈回溯，串口与 VGA 红字双通道；L1 观察者链；OC_ASSERT）、内核转储（持久化到 /var/crash/core.dump 或无文件系统时串口十六进制输出，dumpinfo 解码）、32 项崩溃日志环 + /var/log/crash.log 持久化、HALT/REBOOT 恢复策略（QEMU 实测 panic→复位→二次启动）、软看门狗、内核栈保护页 + 调度边界检查 + KERNEL STACK OVERFLOW 归因、Ring 3 SIGSEGV 真实投递（乒乓保护后强杀）、TUI 崩溃隔离（代码窗口内 ring-0 异常 longjmp 回存活桌面）；新命令 crashtest + dumpinfo，crashlog 补全（226 条命令；L1 接口 146）；发布 ISO 实测门禁：WP-11 E2E 14/14、回归 28/28、dhtest、SSH 5/5、HTTPS 7/7、BIOS+UEFI、0E0W；#DF 边界如实申报（KNOWN_ISSUES 15.13）。137,495 行源码。。Apache 2.0。",
  alternates: { canonical: "/OpenCubeOS/zh/" },
};

export default function ZhLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <>
      {/* root <html lang> is already zh-CN; kept explicit for clarity */}
      <script
        dangerouslySetInnerHTML={{
          __html: `document.documentElement.lang="zh-CN"`,
        }}
      />
      <Chrome locale="zh">{children}</Chrome>
    </>
  );
}
