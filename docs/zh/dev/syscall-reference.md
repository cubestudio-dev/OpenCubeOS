<!-- SPDX-License-Identifier: Apache-2.0 -->
# 系统调用参考（int 0x80，54 个编号）

OCOS 内核完整的 ring-3 系统调用面：入口约定、54 个已注册
编号及其参数、返回值与错误路径，以及 `SYS_L1SVC` 特例。
来源：`ocos/services/sysapi/interface/svc_sysapi_syscall.h`
（编号）、`ocos/services/sysapi/src/svc_sysapi_syscall.c`
（函数体）、`ocos/kernel/core_usermode.c`（分发）、
`ocos/kernel/arch/x86_64/arch_idt*.c/S`（入口）。宏定义
为原文照录。

## 1. 入口路径与 ABI

- **触发**：ring-3 代码执行 `int 0x80`。
- **处理**：IDT 向量 128 是一个 DPL=3 中断门
  （选择子 0x18，标志 0xEE），因此 ring 3 可以进入；
  `arch_isr_stub_128` 压入一个伪造的错误码 + 向量号，
  公共 stub 压入 15 个 GPR，执行 `cld`，对齐栈并调用
  `arch_isr_dispatch`，后者路由到 `core_usermode.c` 中的
  `core_syscall_dispatch((u64*)frame)`。
- **影响**：处理函数运行在任务的内核栈上（TSS 的 rsp0）；
  返回值落在 `rax`；信号投递发生在处理函数返回之后、
  `iretq` 之前。

寄存器 ABI：

```c
/* Syscall ABI: rax=core_syscall_num, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4.
 * Return: rax = result (>= 0 success, < 0 error). */
```

- **第 5 个参数走 `r8`**，仅被 `SYS_L1SVC` 使用；入口路径
  在处理函数运行前把它暂存进 `g_syscall_arg5`
  （`core_syscall_arg5()` 读取它）。
- 在中断帧（`arch_irq_frame_t`）中，`regs[5]` 是 `r10`，
  `regs[7]` 是 `r8`。
- **没有** `sysenter`/`syscall` 指令快速路径 —— 只有
  `int 0x80` —— 也没有按任务嵌套；未知或未注册的编号
  返回 `-1`。
- 注册：`core_syscall_register(num, handler)` 填充
  `g_syscalls[256]`；`usermode_init()` 播种编号 0/1/2，
  然后调用 `core_syscall_wp08a_init()`、
  `core_syscall_wp08fix1_init()` 和
  `core_syscall_l1svc_init()`（都在 services/sysapi）。

ring-3 辅助函数位于 `ocos/interface/l1_lib.h`：
`syscall0..syscall4`（r10 作第 4 参）和 `syscall5`
（r8 作第 5 参，供 L1SVC）。

## 2. 完整表格（54 个编号）

错误以 `rax` 中的小负值返回（多数路径为 `-1`；成功时
文件描述符/pid 为 `>= 0`）。"错误"列描述值得注意的失败
路径。

| # | 名称 | 参数 | 返回 / 错误路径 |
|---|---|---|---|
| 0 | `SYS_EXIT` | `(code)` | 遗留 exit；现在先运行统一回收器 `user_process_reap_resources` 再进入 EXITED |
| 1 | `SYS_WRITE` | `(buf, len)` | 控制台写；对用户缓冲区做逐页映射检查（内核地址探测被围栏拦截 —— 见 `p0leak`） |
| 2 | `SYS_WRITE_AND_EXIT` | `(buf, len)` | 写入后退出（crt0 回退拼法） |
| 3 | `SYS_OPEN` | `(path, flags)` | fd >= 0 / -1；标志 RDONLY/WRONLY/RDWR/CREAT/APPEND/TRUNC |
| 4 | `SYS_CLOSE` | `(fd)` | 0 / -1（坏 fd） |
| 5 | `SYS_STAT` | `(path, struct ush_stat*)` | 0 / -1；内核把 `sizeof(struct ush_stat)` memcpy 进调用方缓冲区 |
| 6 | `SYS_READDIR` | `(path, idx, buf)` | 0 / -1；填充 `struct ush_dirent` |
| 7 | `SYS_MKDIR` | `(path)` | 0 / -1（已存在、无父目录） |
| 8 | `SYS_RMDIR` | `(path)` | 0 / -1（非空） |
| 9 | `SYS_UNLINK` | `(path)` | 0 / -1（忙、fs 不支持） |
| 10 | `SYS_FORK` | `()` | 完整帧保存 + 地址空间拷贝；子进程经 `enter_ring3_fork` 返回 0，父进程返回子 pid |
| 11 | `SYS_EXECVE` | `(name/argv)` | 替换镜像；gzip 注册表查找（`core_userprog_lookup`）+ ELF 加载；未知程序 / 坏 ELF 返回 -1 |
| 12 | `SYS_WAIT4` | `(pid, &status)` | 被收割的 pid / -1（无此子进程） |
| 13 | `SYS_KILL` | `(pid, sig)` | 0 / -1；**受守卫保护的进程拒绝一切信号**（ocire_guard） |
| 14 | `SYS_GETPID` | `()` | pid |
| 15 | `SYS_GETPPID` | `()` | 父 pid |
| 16 | `SYS_EXIT2` | `(code)` | 现代 exit（crt0 使用这个） |
| 20 | `SYS_PIPE` | `(int pipefd[2])` | 0 / -1（表满，至多 32 个管道） |
| 30 | `SYS_MMAP` | `(addr/len/prot...)` | [0x3C000000, 0x3FFF0000) 内的匿名递增分配器，16 区域所有权表 |
| 31 | `SYS_MUNMAP` | `(addr, len)` | 0 / -1 |
| 32 | `SYS_MPROTECT` | `(addr, len, prot)` | 0 / -1；供 ld.so 的 W^X |
| 33 | `SYS_BRK` | `(addr)` | 新 brk / 旧 brk（addr=0 时为查询） |
| 40 | `SYS_SIGNAL` | `(sig, handler)` | 旧 handler / -1 |
| 41 | `SYS_SIGACTION` | `(sig, act)` | 0 / -1 |
| 42 | `SYS_SIGRETURN` | `()` | 经栈上 restorer 蹦床恢复保存的 22 个 u64 帧 |
| 50 | `SYS_DUP` | `(fd)` | 新 fd / -1 |
| 51 | `SYS_DUP2` | `(oldfd, newfd)` | newfd / -1 |
| 60 | `SYS_CHDIR` | `(path)` | 0 / -1；每进程 cwd |
| 61 | `SYS_GETCWD` | `(buf, size)` | 0 / -1 |
| 62 | `SYS_IOCTL` | `(fd, req, arg)` | 0 / -1 |
| 70 | `SYS_READ` | `(fd, buf, len)` | 读到的字节数（0 = EOF；`read(0,buf,0)==0` —— p2test 的 read0 探针）/ -1 |
| 72 | `SYS_WRITE2` | `(fd, buf, len)` | 感知 fd 的写（管道/重定向类别 1=VFS 2=pipe-read 3=pipe-write）/ -1 |
| 73 | `SYS_READLINE` | `(buf, maxlen)` | 内核行编辑器（回显、历史键）；0 / -1 |
| 74 | `SYS_GETCH` | `()` | 原始键码（无回显、非阻塞轮询），无键时为 0 |
| 75 | `SYS_UPTIME` | `()` | 自启动以来的 u64 毫秒数 |
| 76 | `SYS_MEMINFO` | `(buf)` | 从 PMM 填充 3 个 u64（总/已用/空闲字节）；返回 0 |
| 80 | `SYS_SELECT` | `(...)` | 就绪计数 / -1 |
| 81 | `SYS_POLL` | `(...)` | 就绪计数 / -1 |
| 90 | `SYS_MAP_SOLIB` | `(name...)` | dlopen 映射（WP-08b）；句柄 / -1 |
| 96 | `SYS_SYMLINK` | `(target, linkpath)` | 0 / -1 |
| 97 | `SYS_READLINK` | `(path, buf, cap)` | 长度 / -1 |
| 98 | `SYS_LINK` | `(oldpath, newpath)` | 0 / -1（ramfs 维护 nlink） |
| 99 | `SYS_CHMOD` | `(path, mode)` | 0 / -1 |
| 100 | `SYS_CHOWN` | `(path, uid, gid)` | 0 / -1 |
| 101 | `SYS_NETCMD` | `(op, arg, out, cap)` | 0 / -1；op 码见下 |
| 102 | `SYS_PS` | `(buf, cap)` | 条目数 / -1；填充 `struct ush_ps_entry[]` |
| 103 | `SYS_SOCKET` | `(type SOCK_TCP=1/SOCK_UDP=2)` | 套接字 id / -1（32 个套接字） |
| 104 | `SYS_NETBIND` | `(sock, ip_le, port)` | 0 / -1 |
| 105 | `SYS_NETCONNECT` | `(sock, ip_le, port)` | 0 / -1 |
| 106 | `SYS_NETACCEPT` | `(lsock, &ip, &port)` | 接受的套接字 / -1 |
| 107 | `SYS_NETSEND` | `(sock, buf, len)` | 字节数 / -1（would-block = -1，rtx accept 为全有或全无） |
| 108 | `SYS_NETRECV` | `(sock, buf, maxlen)` | 字节数 / -1 |
| 109 | `SYS_NETCLOSE` | `(sock)` | 0 / -1 |
| 110 | `SYS_L1SVC` | `(cat, op, arg, out, cap)` | 0 / -1 —— 见第 4 节 |

`SYS_NETCMD` 操作码（必须与 ush 镜像一致）：

```c
#define NETCMD_IFCONFIG  1
#define NETCMD_PING      2
#define NETCMD_NETSTAT   3
#define NETCMD_WGET      4
```

## 3. ring 3 共享的底层结构

系统调用方必须精确排布的线上结构体（来自
`ocos/interface/fs_lib.h`；必须与内核 `vfs.h` 一致，
`VFS_NAME_LEN=64` —— 内核按 `sizeof(struct)` memcpy）：

```c
#define USH_TYPE_FILE 1
#define USH_TYPE_DIR  2
#define USH_TYPE_DEV  3
struct ush_dirent { char name[64]; int type; u64 inode; };
struct ush_stat  { int type; u64 size; u32 mode; u32 uid; u32 gid; u32 nlink; char name[64]; };
```

`SYS_PS` 的 ABI 行（`l1_lib.h`）：

```c
struct ush_ps_entry {i32 tid; i32 state; i32 priority; char name[32]; u64 cpu_time_ticks;}
```

信号（core_usermode.c）：SIGHUP 1、SIGINT 2、SIGQUIT 3、
SIGKILL 9、SIGSEGV 11、SIGUSR1 10、SIGUSR2 12、SIGTERM 15、
SIGCHLD 17、NSIG 32。待决信号在系统调用返回时投递；
SIGSEGV 也从 ring-3 异常 kill 中投递；随后 3 次投递的
乒乓守卫会杀死该进程。

## 4. SYS_L1SVC（110）详解

`sys_l1svc(cat, op, arg, out)`，其中
`cap = core_syscall_arg5()`：

1. 允许 `out == 0 && cap == 0`（OCIRE 发后即忘拼法）：
   文本渲染进一个 256 字节内核暂存缓冲区（`kout_scratch`，
   静态，仅任务上下文可用，因为系统调用在一个任务内从不
   嵌套），跳过回拷。
2. 否则 `cap` 必须在 **1..32768** 之间；`out` 在任何回写
   之前经 `valid_user_ptr` 校验。
3. `arg` 被拷入一个 **256 字节内核缓冲区** —— 唯独
   `cat == L1SVC_CAT_OCIRE` 获得一个专用的 **4096 字节
   栈缓冲区**（日志代理一次 LOG_APPEND 最多追加 4 KiB）。
4. 执行 `sti`，然后
   `svc_framework_dispatch(L1SVC_CAT_NAME(cat), op, arg, out, cap)`，然后 `cli`（op 体内的设备等待循环是 tick
   驱动的）。
5. 服务渲染的文本此前已经由 `core_kcap_run_fn` 捕获
   （或由缓冲区渲染分发器直接写入）；服务自身的非零退出
   状态仍算调用成功 —— 只有不可路由的 cat/op（或 `rc < 0`
   且 `out` 为空）才返回 -1。
6. 成功时回拷 NUL 结尾的文本并返回 0。

经由此门的 20 个类别与 208 个 op 汇总在
[interface-dictionary.md](interface-dictionary.md)；
完整调用路径（ring-3 包装 -> int 0x80 -> kcap 捕获 ->
回拷）在 [call-chains.md](call-chains.md) 中解剖。

## 5. 内核侧包装服务（ipc 家族）

`services/ipc` 目录为同样的接口面提供 ring-0 调用方，
使内核代码（自检、l1test）无需用户进程即可演练它们：

- **wp8a（接口 33-40）**：`sys_proc_fork/exec/wait/exit/ getpid/getppid`、`pipe_create`、`fd_dup/fd_dup2`、
  `signal_register/send/return`、`sys_mem_mmap/munmap/ mprotect/brk`、`sys_chdir/getcwd/ioctl/select/poll`；
  外加 ring-3 套接字包装 `l1_socket/l1_net_bind/connect/ accept/send/recv/close`（网络套接字 id，不是进程 fd ——
  它们不参与 dup/select/poll/fork）。
- **wp8cd（幸存者 53-57）**：`tool_register`/`tool_list`
  （内嵌工具注册表）、`job_create`（孵化指定的内嵌程序
  并把作业绑定到真实 pid —— 不做"最高存活 pid"猜测）、
  `job_list`、`job_control`（0=前台等待、1=后台、
  2=经统一回收器 kill）。

## 6. 刻意不做成系统调用的东西

- **没有共享内存**：进程各自拥有私有地址空间
  （fork = 完整拷贝；COW PTE 标志存在但拷贝是急切的）；
  `SYS_MMAP` 是每进程的匿名递增分配器。
- **没有 `user_process_kill(pid)`**：该函数已连同墓碑
  注释一起删除（三处不变量违规）；统一回收器
  `user_process_kill_by_tid` 是唯一被认可的 kill 路径，
  经由 SIGKILL/`SYS_KILL` 与异常退出到达。
- **没有特权端口 I/O**：TSS IOPB 对 ring 3 拒绝 [0,256)
  内的每个端口（PRIV_TEST 根因修复）—— ring 3 中的
  `in`/`out` 会触发故障而不是泄漏硬件。
- **没有 L0 shell 逃逸**：服务只能经 L1SVC/NETCMD 文本
  到达；不存在"运行任意内核函数"的门。
