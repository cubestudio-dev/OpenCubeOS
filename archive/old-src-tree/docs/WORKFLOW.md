# Open Cube OS — 工作包制作流程 (WP Production Workflow)

从 WP-06 修复起生效。**每个 WP（或修复）必须按以下 4 步执行，每步都要有实际输出，禁止用文字描述代替实际执行。**

---

## 第一步：制作 / 修复（直面问题）

根据用户发来的工作包或修复内容，完整地进行制作、修复。**要直面问题，而不是绕开。**

- 先定位根因（root cause），再改代码。禁止"试错式"打补丁。
- 根因定位必须用真实证据：QEMU 调试日志、pcap 抓包、寄存器 dump、源码引用。
- 改完后必须在 QEMU 里跑真实测试（不是只编译通过）。
- **实际输出**：测试脚本的标准输出 / 截图 / pcap，证明功能真的工作。

工具：
- `tools/wp0X_test.py` — 单实例功能测试
- `tools/wp06_socket_test.py` — 双实例 L2 直连验证
- `-object filter-dump,id=fd,netdev=n0,file=X.pcap` — QEMU 抓包
- `certutil -hashfile`（Windows）/ `sha256sum`（Linux）— 校验

## 第二步：编译检查 + 上传到 Windows Server

1. `make` 编译内核，`make iso` 构建 ISO。`-Werror`，零警告零错误才算通过。
2. `bun run lint` 检查网站代码。
3. 上传到 Windows Server（`sj.frp.one:18600`，用户 `administrator`）：
   - ISO → `J:\Open_Cube_OS_server\OpenCubeOS-WP0X-latest.iso`
   - SRC zip → `J:\Open_Cube_OS_server\OpenCubeOS-src-WP0X-latest.zip`
   - 也同步到 `J:\Open_Cube_OS_server\build\opencube.iso`
   - **隧道慢时用分块上传**：`tools/release.sh upload` 会自动 gzip + 200KB 分块 + 重试 + 服务端重组解压。
- **实际输出**：`make` 的编译输出、`sha256sum` 结果、SFTP 上传日志。

## 第三步：读取文件参数，更新 HTML

1. 用 `certutil`（服务端）和 `sha256sum`（本地）读取 ISO / SRC 的 SHA256 与字节数。
2. 比对三处一致：本地 `public/downloads/`、服务端、网站 `src/app/page.tsx` 常量。
3. 更新 `src/app/page.tsx` 中的 `ISO_SHA256` / `ISO_SIZE_BYTES` / `SRC_ZIP_SHA256` / `SRC_ZIP_SIZE_BYTES`。
4. 把 ISO / SRC 复制到 `public/downloads/opencube-wp0X.{iso,zip}`（网站从这里提供下载）。
- **实际输出**：三处 SHA256 + size 的对比表，必须完全一致。

## 第四步：最终检查 + 真实汇报

1. `bun run lint` 通过。
2. dev server (`bun run dev`) 在 3000 端口运行，`dev.log` 无致命错误。
3. 用 **agent-browser** 打开 `http://localhost:3000/`，确认：
   - 页面渲染正常（非白屏 / 非 error boundary）
   - 下载链接可点击，`curl -I` 返回 200 且 `Content-Length` 与实际字节数一致
   - SHA256 / 大小正确显示
   - 页脚 sticky 到底部
4. 用 **VLM** 复核截图（布局 / 内容 / 页脚）。
5. 向用户汇报**真实开发情况**：哪些通过、哪些是已知限制、哪些待办。**不许夸大。**
- **实际输出**：agent-browser snapshot、VLM 分析结果、dev.log 尾部。

---

## 一键发布脚本

`tools/release.sh` 封装了第 2~4 步的机械操作：

```bash
# 完整发布（构建 + 上传 + 读参数 + 输出待粘贴的常量）
bash tools/release.sh

# 仅上传（ISO 已构建）
bash tools/release.sh upload

# 仅读取并比对三处 SHA256/size
bash tools/release.sh verify
```

脚本会打印每一步的实际输出，不依赖文字描述。

---

## 已知限制（必须诚实记录）

- QEMU slirp 不转发 ICMP 到外网：`ping 8.8.8.8` 超时是 slirp 限制，不是协议栈缺陷。真实 L2 验证用 socket 模式或 TAP。
- frp 隧道慢（~17KB/s）：大文件上传用 gzip + 分块。10MB ISO 压缩到 3.7MB，分 19 块上传。
- `-display none` 模式无键盘输入：只有串口能输入。要键盘用 `make run-bios-gui`（`-display gtk`）。
