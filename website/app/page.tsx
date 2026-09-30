import {
  BASE,
  GITHUB_REPO,
  ISO_FILE,
  ISO_SHA256,
  ISO_SIZE_B,
  ISO_SIZE_MB,
  ISO_URL,
  RELEASE_WP09,
  SRC_FILE,
  SRC_SHA256,
  SRC_SIZE_B,
  SRC_SIZE_MB,
  SRC_URL,
  STATS,
  FEATURES,
  BASE_KERNEL,
  WORK_PACKAGES,
  VERIFY,
} from "@/lib/site";

function fmtBytes(n: number): string {
  return n.toLocaleString("en-US");
}

export default function Home() {
  return (
    <div className="container">
      {/* Hero */}
      <section className="hero">
        <span className="badge">WP-09 · 正式版 · Apache 2.0</span>
        <h1 className="hero-title">
          Open Cube OS
          <br />
          开源 x86_64 L0 内核
        </h1>
        <p className="hero-sub">
          它不是"一个能日常使用的系统"，而是
          <b>"一个能被扩展成任何东西的内核"</b>
          。价值不在自带什么，而在向上层暴露的接口：L0 = 完整内核，L1 =
          上层扩展，L0 不内置任何 L1。WP-09 新增安全传输层：
          <b>SSH（客户端 + 服务端）、TLS 1.2 / HTTPS、crypto 核心</b>。
        </p>
        <div className="cta-row">
          <a href={ISO_URL} className="btn btn-primary">
            下载 ISO（{ISO_SIZE_MB} MB）
          </a>
          <a href={`${BASE}/docs/`} className="btn">
            阅读文档
          </a>
          <a
            href={GITHUB_REPO}
            target="_blank"
            rel="noopener noreferrer"
            className="btn"
          >
            GitHub 仓库
          </a>
        </div>

        {/* Stats */}
        <div className="stats">
          {STATS.map((s) => (
            <div className="stat" key={s.label}>
              <b>{s.value}</b>
              <span>{s.label}</span>
            </div>
          ))}
        </div>
      </section>

      {/* Downloads */}
      <section id="downloads">
        <div className="sec-head">
          <h2>下载 WP-09</h2>
          <p>
            BIOS + UEFI 双引导 ISO 与完整源码包。文件在本站、GitHub Release
            与本地构建三处 SHA256 字节级一致（核对方法见下方命令）。
          </p>
        </div>
        <div className="dl-grid">
          <div className="dl-card">
            <h3>{ISO_FILE}</h3>
            <div className="dl-meta">
              可引导 ISO · {fmtBytes(ISO_SIZE_B)} 字节（{ISO_SIZE_MB} MB）·
              GRUB multiboot2 · SeaBIOS / OVMF 双引导
            </div>
            <div className="sha-box">
              <b>SHA256</b>
              <br />
              {ISO_SHA256}
            </div>
            <div className="dl-actions">
              <a href={ISO_URL} className="btn btn-primary">
                本站下载 ISO
              </a>
              <a
                href={RELEASE_WP09}
                target="_blank"
                rel="noopener noreferrer"
                className="btn"
              >
                GitHub Release
              </a>
            </div>
          </div>
          <div className="dl-card">
            <h3>{SRC_FILE}</h3>
            <div className="dl-meta">
              完整源码（git archive）· {fmtBytes(SRC_SIZE_B)} 字节（
              {SRC_SIZE_MB} MB）· 内核 + 引导 + 用户程序 + 文档 + 工具
            </div>
            <div className="sha-box">
              <b>SHA256</b>
              <br />
              {SRC_SHA256}
            </div>
            <div className="dl-actions">
              <a href={SRC_URL} className="btn btn-primary">
                本站下载源码包
              </a>
              <a
                href={RELEASE_WP09}
                target="_blank"
                rel="noopener noreferrer"
                className="btn"
              >
                GitHub Release
              </a>
            </div>
          </div>
        </div>
        <p className="dl-note">
          核对：<code className="inline">sha256sum opencube-wp09.iso</code>
          ，结果应与上方及 GitHub Release 资产一致。
        </p>
      </section>

      {/* WP-09 features */}
      <section>
        <div className="sec-head">
          <h2>功能</h2>
          <p>
            网络栈与安全传输（WP-06 基础 + WP-09 增强），全部在 QEMU
            真机回归中验证。
          </p>
        </div>
        <div className="feat-grid">
          {FEATURES.map((f) => (
            <div className="feat" key={f.name}>
              <h3>{f.name}</h3>
              <p>{f.desc}</p>
              <span className="tag">{f.tag}</span>
            </div>
          ))}
        </div>
      </section>

      {/* Base kernel */}
      <section>
        <div className="sec-head">
          <h2>内核基础（WP-01 ~ WP-08）</h2>
          <p>从裸机引导到用户态动态链接，每一层都可扩展。</p>
        </div>
        <div className="base-list">
          {BASE_KERNEL.map((b) => (
            <div className="base-item" key={b.k}>
              <span className="k">{b.k}</span>
              <span className="v">{b.v}</span>
            </div>
          ))}
        </div>
      </section>

      {/* Work packages */}
      <section>
        <div className="sec-head">
          <h2>工作包</h2>
          <p>9 个工作包，WP-01 到 WP-09，全部完成（done）。</p>
        </div>
        <table className="wp-table">
          <thead>
            <tr>
              <th>编号</th>
              <th>内容</th>
              <th>状态</th>
            </tr>
          </thead>
          <tbody>
            {WORK_PACKAGES.map((w) => (
              <tr key={w.no}>
                <td>{w.no}</td>
                <td>{w.title}</td>
                <td className="done">done</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>

      {/* Verification */}
      <section>
        <div className="sec-head">
          <h2>验证</h2>
          <p>
            完整证据（真实输出 + 双侧日志）：
            <a
              href={`${GITHUB_REPO}/blob/main/docs/VERIFICATION_BATCH_B.md`}
              target="_blank"
              rel="noopener noreferrer"
            >
              docs/VERIFICATION_BATCH_B.md
            </a>
            ，原始日志在 docs/verification/。
          </p>
        </div>
        <div className="verify-list">
          {VERIFY.map((v) => (
            <div className="verify-item" key={v.name}>
              <span className="ok">PASS</span> · <b>{v.name}</b>
              <br />
              {v.desc}
            </div>
          ))}
        </div>
      </section>
    </div>
  );
}
