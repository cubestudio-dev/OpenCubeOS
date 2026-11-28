<!-- SPDX-License-Identifier: Apache-2.0 -->
# Ring 3 与系统调用面

用户代码如何触达内核：一条指令（`int 0x80`）、54 个系统调用号、
一扇 5 参数的服务之门（`SYS_L1SVC`），以及让 208 个服务操作返回
文本的输出捕获机制。ABI 契约镜像于 `ocos/interface/`
（[private-headers.md](private-headers.md)）；进程/ELF 一侧位于
`ocos/kernel/core_usermode.c`（1518 行）。

## 入口路径

Ring 3 执行 `int 0x80`。向量 128 的 IDT 门携带标志 **0xEE
（DPL=3）**，因此该指令在用户态合法（选择子 0x18，中断门——处理器
期间中断关闭）。链条：

```
int 0x80
  -> arch_isr_stub_128        (pushes fake error code + vector number)
  -> arch_isr_common          (pushes 15 GPRs, cld, 16-byte align)
  -> arch_isr_dispatch        (frame = 22 u64s, arch_irq_frame_t)
  -> core_syscall_dispatch((u64*)frame)     [core_usermode.c]
```

**ABI**：`rax` = 系统调用号；参数在 `rdi, rsi, rdx, r10`（第 4 参）
与 **`r8`（第 5 参）**；返回值在 `rax`；未知或未注册的编号返回 -1。
第 5 参数槽位恰为一个系统调用所用——`SYS_L1SVC` 的 `cap`——并在
处理器运行前暂存进 `g_syscall_arg5`（帧槽 `regs[7]`；`regs[5]`
是 r10）。没有 `sysenter`/`syscall` 指令的快速路径——只有 int
0x80——也没有按任务的嵌套：系统调用从不在一个任务内递归。信号
投递发生在处理器返回之后、`iretq` 之前。

注册：`core_syscall_register(num, handler)` 注册进 `g_syscalls[256]`；
`usermode_init()` 播种 0/1/2，随后调用
`core_syscall_wp08a_init()` + `core_syscall_wp08fix1_init()` +
`core_syscall_l1svc_init()`（函数体位于
`ocos/services/sysapi/src/svc_sysapi_syscall.c`）。

## 完整系统调用表（54 个编号）

来自 `ocos/services/sysapi/interface/svc_sysapi_syscall.h`：

| # | 名称 | 用途 |
|---|---|---|
| 0 | `SYS_EXIT` | 遗留退出（现在也运行统一收割器） |
| 1 | `SYS_WRITE` | 控制台写，带按页映射检查 |
| 2 | `SYS_WRITE_AND_EXIT` | 写后退出 |
| 3 | `SYS_OPEN` | (path, flags) |
| 4 | `SYS_CLOSE` | (fd) |
| 5 | `SYS_STAT` | (path, struct ush_stat*) |
| 6 | `SYS_READDIR` | (path, idx, buf) |
| 7 | `SYS_MKDIR` | (path) |
| 8 | `SYS_RMDIR` | (path) |
| 9 | `SYS_UNLINK` | (path) |
| 10 | `SYS_FORK` | 完整帧保存 + 地址空间拷贝 + fork 子路径 |
| 11 | `SYS_EXECVE` | (name/argv)——gzip 注册表查找 + ELF 装载 |
| 12 | `SYS_WAIT4` | (pid, &status) |
| 13 | `SYS_KILL` | (pid, sig)——受守护进程拒绝一切信号 |
| 14 | `SYS_GETPID` | |
| 15 | `SYS_GETPPID` | |
| 16 | `SYS_EXIT2` | 现代退出（crt0 使用） |
| 20 | `SYS_PIPE` | (int pipefd[2]) |
| 30 | `SYS_MMAP` | 每进程匿名递增分配器，位于 [0x3C000000, 0x3FFF0000) |
| 31 | `SYS_MUNMAP` | (addr, len) |
| 32 | `SYS_MPROTECT` | (addr, len, prot)——为 ld.so 提供 W^X |
| 33 | `SYS_BRK` | (addr) |
| 40 | `SYS_SIGNAL` | (sig, handler) |
| 41 | `SYS_SIGACTION` | (sig, act) |
| 42 | `SYS_SIGRETURN` | 恢复保存的 22-u64 信号帧 |
| 50 | `SYS_DUP` | (fd) |
| 51 | `SYS_DUP2` | (oldfd, newfd) |
| 60 | `SYS_CHDIR` | 每进程 cwd |
| 61 | `SYS_GETCWD` | |
| 62 | `SYS_IOCTL` | |
| 70 | `SYS_READ` | (fd, buf, len) |
| 72 | `SYS_WRITE2` | fd 感知的写（管道/重定向） |
| 73 | `SYS_READLINE` | 内核行编辑器 |
| 74 | `SYS_GETCH` | 原始按键，无回显，非阻塞轮询 |
| 75 | `SYS_UPTIME` | 自引导以来的毫秒数 |
| 76 | `SYS_MEMINFO` | 从 PMM 填 3 个 u64（总计/已用/空闲） |
| 80 | `SYS_SELECT` | |
| 81 | `SYS_POLL` | |
| 90 | `SYS_MAP_SOLIB` | dlopen 内核侧（WP-08b） |
| 96 | `SYS_SYMLINK` | (target, linkpath) |
| 97 | `SYS_READLINK` | |
| 98 | `SYS_LINK` | 硬链接 |
| 99 | `SYS_CHMOD` | |
| 100 | `SYS_CHOWN` | |
| 101 | `SYS_NETCMD` | (op, arg, out, cap)；ops：IFCONFIG=1 PING=2 NETSTAT=3 WGET=4 |
| 102 | `SYS_PS` | (buf, cap) -> struct ush_ps_entry[] |
| 103 | `SYS_SOCKET` | (type SOCK_TCP=1/SOCK_UDP=2) |
| 104 | `SYS_NETBIND` | |
| 105 | `SYS_NETCONNECT` | |
| 106 | `SYS_NETACCEPT` | |
| 107 | `SYS_NETSEND` | |
| 108 | `SYS_NETRECV` | |
| 109 | `SYS_NETCLOSE` | |
| 110 | `SYS_L1SVC` | (cat, op, arg, out, cap)——服务之门 |

套接字系统调用取的是**网络套接字 id，不是进程 fd**——它们不参与
dup/select/poll/fork（文档见 `services/ipc`）；NETCMD 桥让历史上的
ush 内建命令（ifconfig/ping/netstat/wget）留在同一套被捕获的函数
体上。

## SYS_L1SVC——唯一的服务之门

`sys_l1svc(cat, op, arg, out)`，`cap = core_syscall_arg5()`（r8
槽位），实现于 `svc_sysapi_syscall.c`：

- `out == 0 && cap == 0` 是 OCIRE 发后即忘的拼写 -> 渲染进一个
  256 字节内核暂存缓冲，跳过拷回。
- `cap` 必须为 1..32768（WP06g2 中从 8192 上调，以便双语注册表
  清单装得下）；`out` 由 `valid_user_ptr` 校验。
- `arg` 被拷进 256 字节内核缓冲——**唯 cat OCIRE（18）例外，它得到
  专用的 4096 字节栈缓冲**，用于日志代理的追加。
- 分派之前：`sti` ... `svc_framework_dispatch(...)` ... `cli`——
  op 体内的设备等待循环是节拍驱动的，不是死自旋。
- 服务的非零退出状态仍算成功调用；只有无法路由的类别/op（或 rc&lt;0
  且 out 为空）才返回 -1。其余情况把 NUL 终止的文本拷回并返回 0。

路由按类别字符串对照 `.oc_svcs` 表（链接段注册表——见
[hidden-mechanisms.md](hidden-mechanisms.md)）。**20 个类别、208 个
op**，编号只增不改，经 `tools/gen_l1svc_ops.py` 逐字节镜像进
ring 3：

| Cat | 名称 | 目录 | Ops | Cat | 名称 | 目录 | Ops |
|---|---|---|---|---|---|---|---|
| 1 | time | services/time | 4 | 11 | watchdog | services/watchdog | 1 |
| 2 | block | services/block | 23 | 12 | crypto | services/crypto | 2 |
| 3 | nic | services/net | 20 | 13 | l1 | services/l1 | 3 |
| 4 | snd | services/audio | 17 | 14 | display | services/display | 17 |
| 5 | power | services/power | 8 | 15 | input | services/display | 2 |
| 6 | ota | services/selfhost | 16 | 16 | camera | services/camera | 10 |
| 7 | fs | services/fs | 6 | 17 | usb | services/usb | 9 |
| 8 | net | services/net | 14 | 18 | ocire | services/ocembed | 2 |
| 9 | health | services/health | 3 | 19 | diag | services/diag | 39 |
| 10 | klog | services/klog | 3 | 20 | tui | services/tui | 9 |

Cat 18 是仅限守卫的 OCIRE 通道（op 4 LOG_APPEND / 5
KLOG_SNAPSHOT）——针对 `ocire/agent/ocire_agent.c` 的冻结 ABI，
对一切非守护调用者拒绝（见
[hidden-mechanisms.md](hidden-mechanisms.md)）。

## 输出捕获——旧命令体为何仍然可用

多数 op 体是 v1/v2.2.0 时代经 `screen_console_puts` 打印的 shell
命令体。为了让它们经 L1SVC 服务，分派把它们包进
`core_kcap_run_fn(fn, arg, out, cap)`（`ocos/kernel/core_kcap.c`）：
捕获激活期间，**捕获钩子即控制台钩子**（控制台驱动恰好持有一个
钩子槽位），把字符收进调用者缓冲，同时抑制帧缓冲绘制与串口分岔。
捕获可叠至 4 层；只有最内层的活跃捕获接受数据；基础钩子（串口
编码器分岔）在最后一次捕获结束时重新装回；溢出被标记，绝不静默
截断。

每个 op 有三种路由风格（文档见各 l1 文件）：

1. **捕获**——一次性文本 op（大多数）。
2. **直通**——`out[0]=0; return fn(arg);`，用于实时控制台会话：
   `sshd`、全部 9 个 TUI op（[tui-engine.md](tui-engine.md)）、
   `ime -i` 与 `power sleep`（被捕获的多秒驻留会吞掉其间打印的
   每一行控制台输出——监督引导期间实测发现）。
3. **缓冲渲染**——服务用自己的 `put()` 辅助直接写进 `out`
   （`l1`、`klog`、`health`、`diag`、`fs`、`time`、`watchdog`
   分派器）。

## 进程、ELF 与 ring 3 世界

- **入口**：`execve` 留下 `[rsp]=argc, [rsp+8]=argv`；每个应用
  链接 `ocos/interface/l1_crt_entry.asm`，由它安全读取并调用
  `app_main(argc, argv)`，经 `SYS_EXIT2` 退出。
- **ELF 装载**：静态 ET_EXEC 与 ET_DYN（PIE）带 ASLR，外加动态
  路径——PT_INTERP 探测、ld.so 映射于 0x10000000、由用户态 ld.so
  完成 `.dynamic`/GOT/PLT 重定位、DT_NEEDED 对照内嵌 solib 表解析
  （见 [../l1/solib-loader.md](../l1/solib-loader.md)）。
- **程序注册表**：`core_userprog_lookup(name, &size)` 找到 284 个
  gzip 内嵌程序之一（见
  [hidden-mechanisms.md](hidden-mechanisms.md)）；`SYS_EXECVE` 在
  首次使用时解压。
- **信号**：SIGHUP 1、SIGINT 2、SIGQUIT 3、SIGKILL 9、SIGUSR1 10、
  SIGSEGV 11、SIGUSR2 12、SIGTERM 15、SIGCHLD 17、NSIG 32；在
  系统调用返回时挂起投递；ring 3 异常的 SIGSEGV 投递带 3 次乒乓
  守卫，随后击杀（见 [interrupts.md](interrupts.md)）。
- **管道**（内核侧）：32 个管道、4096 字节缓冲、fd 种类记录于
  `user_proc_t.fds[]`（0=空闲、1=VFS fd、2=管道读、3=管道写）、
  每端等待队列；`pipe_close_fd` 为统一收割器导出。
- **mmap**：每进程匿名递增分配器，位于 [0x3C000000, 0x3FFF0000)，
  带 16 区域所有权表——没有共享内存系统调用；fork 是急切拷贝
  （`VMM_FLAG_COW` 已定义但未使用）。
- **作业控制**（services/ipc）：`job_create` 拉起指定内嵌程序并把
  作业绑定到真实 pid（不做“最高存活 pid”式猜测——BUG-0249）；
  `job_control` 0=前台等待、1=后台、2=经统一收割器击杀。

以上全部内容的 L1 开发者视图：
[../l1/overview.md](../l1/overview.md) 与
[../l1/surface-map.md](../l1/surface-map.md)。
