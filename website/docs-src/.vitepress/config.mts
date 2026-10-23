// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
// Open Cube OS docs center - VitePress config (bilingual: EN root + zh).
// srcDir points at the repository's docs/ tree (single source of truth);
// docs/zh/ holds the Simplified Chinese translation of the same tree.
// The facade (index.html / architecture.html / ...) lives in website/.
import { defineConfig } from 'vitepress'

// CJK-aware tokenizer shared by index build and query side (VitePress
// serializes themeConfig functions to the client, so one definition
// serves both). Strategy: CJK runs -> bigrams (+ isolated single chars);
// everything else -> lowercased word runs. Queries combine with AND,
// which gives precise results for words like 看门狗 / 文件系统.
function ocTokenize(text: string): string[] {
  const tokens: string[] = []
  const runs = String(text).split(/([\u4e00-\u9fff\u3400-\u4dbf]+)/)
  for (const run of runs) {
    if (!run) continue
    if (/^[\u4e00-\u9fff\u3400-\u4dbf]+$/.test(run)) {
      if (run.length === 1) {
        tokens.push(run)
      } else {
        for (let j = 0; j + 1 < run.length; j++) tokens.push(run.slice(j, j + 2))
      }
    } else {
      const words = run.toLowerCase().split(/[^\p{L}\p{N}_]+/u)
      for (const w of words) if (w) tokens.push(w)
    }
  }
  return tokens
}

const SITE = 'https://cubestudio-dev.github.io/OpenCubeOS'

export default defineConfig({
  // gh-pages serves this site under /OpenCubeOS/ (project pages):
  // the docs center lives at /OpenCubeOS/docs/ - base MUST match or every
  // asset URL 404s in production.
  base: '/OpenCubeOS/docs/',
  title: 'Open Cube OS',
  description: 'Documentation center for Open Cube OS - the from-scratch x86_64 kernel with the oc> recovery shell.',
  // docs-src/docs/ is a BUILD-TIME copy of the repository's docs/ tree
  // (synced by the build script: single source of truth stays docs/).
  srcDir: 'docs',
  srcExclude: ['docs/archive/**', 'docs/data/**'],
  outDir: '../out/docs',
  // cleanUrls would emit extensionless links; GitHub Pages does NOT
  // rewrite them (404). Keep the .html suffix - works everywhere.
  cleanUrls: false,
  cacheDir: './.vitepress/cache',

  locales: {
    root: {
      label: 'English',
      lang: 'en-US'
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      link: '/zh/',
      title: 'Open Cube OS',
      description: 'Open Cube OS 文档中心 —— 从零写起的 x86_64 内核与 oc> 恢复 Shell。',
      themeConfig: {
        nav: [
          { text: '官网', link: `${SITE}/?lang=zh` },
          { text: '架构', link: `${SITE}/architecture.html?lang=zh` },
          { text: '下载', link: `${SITE}/download.html?lang=zh` },
          { text: '关于', link: `${SITE}/about.html?lang=zh` },
          { text: 'GitHub', link: 'https://github.com/cubestudio-dev/OpenCubeOS' }
        ],
        outlineTitle: '本页目录',
        docFooter: { prev: '上一页', next: '下一页' },
        darkModeSwitchLabel: '外观',
        lightModeSwitchTitle: '切换到浅色模式',
        darkModeSwitchTitle: '切换到深色模式',
        sidebarMenuLabel: '菜单',
        returnToTopLabel: '回到顶部',
        footer: {
          message: '基于 Apache 2.0 协议发布。',
          copyright: '版权所有 2026 cubestudio-dev'
        }
      }
    }
  },

  head: [
    ['link', { rel: 'icon', href: 'data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 32 32%22%3E%3Crect width=%2232%22 height=%2232%22 rx=%226%22 fill=%22%230b0f0c%22/%3E%3Ctext x=%2216%22 y=%2222%22 font-family=%22monospace%22 font-size=%2214%22 fill=%22%2346d977%22 text-anchor=%22middle%22%3Eoc%3E%3C/text%3E%3C/svg%3E' }]
  ],

  themeConfig: {
    // Facade links are absolute: the docs center lives at /OpenCubeOS/docs/
    // and pages exist at several depths, so '../' would resolve wrongly on
    // deep pages (pre-existing defect on gh-pages, fixed here).
    nav: [
      { text: 'Website', link: `${SITE}/` },
      { text: 'Architecture', link: `${SITE}/architecture.html` },
      { text: 'Download', link: `${SITE}/download.html` },
      { text: 'About', link: `${SITE}/about.html` },
      { text: 'GitHub', link: 'https://github.com/cubestudio-dev/OpenCubeOS' }
    ],

    outline: { level: [2, 3] },

    search: {
      provider: 'local',
      options: {
        translations: {
          button: { buttonText: 'Search docs', buttonAriaLabel: 'Search' },
          modal: {
            noResultsText: 'No results',
            resetButtonTitle: 'Clear query',
            footer: { selectText: 'Select', navigateText: 'Navigate', closeText: 'Close' }
          }
        },
        locales: {
          zh: {
            translations: {
              button: { buttonText: '搜索文档', buttonAriaLabel: '搜索' },
              modal: {
                noResultsText: '无结果',
                resetButtonTitle: '清空查询',
                footer: { selectText: '选择', navigateText: '切换', closeText: '关闭' }
              }
            }
          }
        },
        miniSearch: {
          options: {
            tokenize: ocTokenize
          },
          searchOptions: {
            fuzzy: 0.2,
            prefix: true,
            combineWith: 'AND',
            boost: { title: 4, text: 2, titles: 1 }
          }
        }
      }
    },

    sidebar: {
      '/': [
        { text: 'Overview', link: '/' },
        {
          text: 'User manual',
          collapsed: false,
          items: [
            { text: 'Getting started', link: '/manual/getting-started' },
            { text: 'Boot modes & A/B', link: '/manual/boot-modes' },
            { text: 'Shell basics', link: '/manual/shell-basics' },
            { text: 'Filesystems & storage', link: '/manual/filesystems' },
            { text: 'Networking', link: '/manual/network' },
            { text: 'A/B slots & OTA', link: '/manual/update-ota' },
            { text: 'TUI guide', link: '/manual/tui-guide' },
            { text: 'Chinese console', link: '/manual/chinese-console' },
            { text: 'Recovery workflow', link: '/manual/recovery' },
            { text: 'All 233 commands', link: '/manual/commands/' }
          ]
        },
        {
          text: 'Developer handbook',
          collapsed: false,
          items: [
            { text: 'Environment', link: '/dev/environment' },
            { text: 'Building & packaging', link: '/dev/build' },
            { text: 'QEMU testing', link: '/dev/qemu-testing' },
            { text: 'Debugging', link: '/dev/debugging' },
            { text: 'Host unit tests', link: '/dev/host-tests' },
            { text: 'E2E suites', link: '/dev/e2e-suites' },
            { text: 'The PR bar', link: '/dev/pr-workflow' },
            { text: 'Portability', link: '/dev/portability' },
            { text: 'Coding style', link: '/dev/coding-style' }
          ]
        },
        {
          text: 'Architecture',
          collapsed: true,
          items: [
            { text: 'Boot flow', link: '/arch/boot-flow' },
            { text: 'Memory', link: '/arch/memory' },
            { text: 'Scheduler', link: '/arch/scheduler' },
            { text: 'Interrupts & exceptions', link: '/arch/interrupts' },
            { text: 'Ring 3 & syscalls', link: '/arch/syscall-ring3' },
            { text: 'VFS & filesystems', link: '/arch/vfs' },
            { text: 'Network stack', link: '/arch/network-stack' },
            { text: 'Driver model', link: '/arch/driver-model' },
            { text: 'TUI engine', link: '/arch/tui-engine' },
            { text: 'Chinese layer', link: '/arch/chinese-layer' },
            { text: 'Crash recovery', link: '/arch/crash-recovery' },
            { text: 'OTA & A/B', link: '/arch/ota-ab' }
          ]
        },
        {
          text: 'L1 extension guide',
          collapsed: true,
          items: [
            { text: 'Overview', link: '/l1/overview' },
            { text: 'The surface map (162)', link: '/l1/surface-map' },
            { text: 'Block drivers', link: '/l1/driver-block' },
            { text: 'NIC drivers', link: '/l1/driver-nic' },
            { text: 'Sound drivers', link: '/l1/driver-snd' },
            { text: 'OTA & A/B', link: '/l1/ota' },
            { text: 'Watchdog & health', link: '/l1/watchdog-health' },
            { text: 'Shell bridge', link: '/l1/shell-bridge' },
            { text: 'TUI extensions', link: '/l1/tui-ext' },
            { text: 'Writing modules', link: '/l1/writing-modules' },
            { text: 'The solib loader', link: '/l1/solib-loader' }
          ]
        },
        {
          text: 'Driver manuals',
          collapsed: true,
          items: [
            { text: 'Storage', link: '/drivers/storage' },
            { text: 'Network cards', link: '/drivers/network' },
            { text: 'USB', link: '/drivers/usb' },
            { text: 'Sound', link: '/drivers/sound' },
            { text: 'Input', link: '/drivers/input' },
            { text: 'Display', link: '/drivers/display' },
            { text: 'PCI / PCIe', link: '/drivers/pci-pcie' },
            { text: 'ACPI', link: '/drivers/acpi' },
            { text: 'I2C / SMBus', link: '/drivers/i2c-smbus' },
            { text: 'Time sources', link: '/drivers/time' },
            { text: 'Wireless', link: '/drivers/wireless' },
            { text: 'Bluetooth', link: '/drivers/bluetooth' },
            { text: 'SD & cameras', link: '/drivers/sd-camera' },
            { text: 'Misc & SCSI', link: '/drivers/misc-scsi' }
          ]
        },
        {
          text: 'Internals',
          collapsed: true,
          items: [
            { text: 'Watchdog', link: '/internals/watchdog' },
            { text: 'Kernel log', link: '/internals/klog' },
            { text: 'Health monitor', link: '/internals/health' },
            { text: 'Crash dumps', link: '/internals/crash-dump' },
            { text: 'Entropy & crypto', link: '/internals/drbg-crypto' },
            { text: 'OTA/A-B mechanics', link: '/internals/ota-ab' }
          ]
        },
        {
          text: 'Troubleshooting',
          collapsed: true,
          items: [
            { text: 'Common errors', link: '/troubleshooting/common-errors' },
            { text: 'QEMU debugging', link: '/troubleshooting/qemu-debugging' },
            { text: 'Boot failures', link: '/troubleshooting/boot-failures' },
            { text: 'Network issues', link: '/troubleshooting/network-issues' },
            { text: 'Storage issues', link: '/troubleshooting/storage-issues' }
          ]
        },
        {
          text: 'Release engineering',
          collapsed: true,
          items: [
            { text: 'The release SOP', link: '/release/process' },
            { text: 'Versioning', link: '/release/versioning' },
            { text: 'Release note template', link: '/release/release-notes-template' },
            { text: 'Website update', link: '/release/website-update' },
            { text: 'Wiki sync', link: '/release/wiki-sync' }
          ]
        }
      ],

      '/zh/': [
        { text: '总览', link: '/zh/' },
        {
          text: '用户手册',
          collapsed: false,
          items: [
            { text: '快速上手', link: '/zh/manual/getting-started' },
            { text: '启动模式与 A/B', link: '/zh/manual/boot-modes' },
            { text: 'Shell 基础', link: '/zh/manual/shell-basics' },
            { text: '文件系统与存储', link: '/zh/manual/filesystems' },
            { text: '网络', link: '/zh/manual/network' },
            { text: 'A/B 槽位与 OTA 升级', link: '/zh/manual/update-ota' },
            { text: 'TUI 指南', link: '/zh/manual/tui-guide' },
            { text: '中文控制台', link: '/zh/manual/chinese-console' },
            { text: '恢复工作流', link: '/zh/manual/recovery' },
            { text: '全部 233 条命令', link: '/zh/manual/commands/' }
          ]
        },
        {
          text: '开发者手册',
          collapsed: false,
          items: [
            { text: '开发环境', link: '/zh/dev/environment' },
            { text: '构建与打包', link: '/zh/dev/build' },
            { text: 'QEMU 测试', link: '/zh/dev/qemu-testing' },
            { text: '调试', link: '/zh/dev/debugging' },
            { text: '宿主侧单元测试', link: '/zh/dev/host-tests' },
            { text: '端到端测试套件', link: '/zh/dev/e2e-suites' },
            { text: '合入门槛（PR 规范）', link: '/zh/dev/pr-workflow' },
            { text: '可移植性', link: '/zh/dev/portability' },
            { text: '代码风格', link: '/zh/dev/coding-style' }
          ]
        },
        {
          text: '架构深度解析',
          collapsed: true,
          items: [
            { text: '启动流程', link: '/zh/arch/boot-flow' },
            { text: '内存管理', link: '/zh/arch/memory' },
            { text: '进程调度', link: '/zh/arch/scheduler' },
            { text: '中断与异常', link: '/zh/arch/interrupts' },
            { text: 'Ring 3 与系统调用', link: '/zh/arch/syscall-ring3' },
            { text: 'VFS 与文件系统', link: '/zh/arch/vfs' },
            { text: '网络协议栈', link: '/zh/arch/network-stack' },
            { text: '驱动模型', link: '/zh/arch/driver-model' },
            { text: 'TUI 引擎', link: '/zh/arch/tui-engine' },
            { text: '中文支持层', link: '/zh/arch/chinese-layer' },
            { text: '崩溃恢复', link: '/zh/arch/crash-recovery' },
            { text: 'OTA 与 A/B', link: '/zh/arch/ota-ab' }
          ]
        },
        {
          text: 'L1 扩展指南',
          collapsed: true,
          items: [
            { text: '总览', link: '/zh/l1/overview' },
            { text: '接口面全图（162）', link: '/zh/l1/surface-map' },
            { text: '块设备驱动', link: '/zh/l1/driver-block' },
            { text: '网卡驱动', link: '/zh/l1/driver-nic' },
            { text: '声卡驱动', link: '/zh/l1/driver-snd' },
            { text: 'OTA 与 A/B', link: '/zh/l1/ota' },
            { text: '看门狗与健康监控', link: '/zh/l1/watchdog-health' },
            { text: 'Shell 桥', link: '/zh/l1/shell-bridge' },
            { text: 'TUI 扩展', link: '/zh/l1/tui-ext' },
            { text: '编写模块', link: '/zh/l1/writing-modules' },
            { text: '共享对象加载器', link: '/zh/l1/solib-loader' }
          ]
        },
        {
          text: '驱动点亮手册',
          collapsed: true,
          items: [
            { text: '存储', link: '/zh/drivers/storage' },
            { text: '网卡', link: '/zh/drivers/network' },
            { text: 'USB', link: '/zh/drivers/usb' },
            { text: '声卡', link: '/zh/drivers/sound' },
            { text: '输入设备', link: '/zh/drivers/input' },
            { text: '显示', link: '/zh/drivers/display' },
            { text: 'PCI / PCIe', link: '/zh/drivers/pci-pcie' },
            { text: 'ACPI', link: '/zh/drivers/acpi' },
            { text: 'I2C / SMBus', link: '/zh/drivers/i2c-smbus' },
            { text: '时钟源', link: '/zh/drivers/time' },
            { text: '无线（Wi-Fi）', link: '/zh/drivers/wireless' },
            { text: '蓝牙', link: '/zh/drivers/bluetooth' },
            { text: 'SD 卡与摄像头', link: '/zh/drivers/sd-camera' },
            { text: '杂项与 SCSI', link: '/zh/drivers/misc-scsi' }
          ]
        },
        {
          text: '内部机制',
          collapsed: true,
          items: [
            { text: '看门狗', link: '/zh/internals/watchdog' },
            { text: '内核日志', link: '/zh/internals/klog' },
            { text: '健康监控', link: '/zh/internals/health' },
            { text: '崩溃转储', link: '/zh/internals/crash-dump' },
            { text: '熵与加密核心', link: '/zh/internals/drbg-crypto' },
            { text: 'OTA/A-B 运作原理', link: '/zh/internals/ota-ab' }
          ]
        },
        {
          text: '故障排查',
          collapsed: true,
          items: [
            { text: '常见错误与修复', link: '/zh/troubleshooting/common-errors' },
            { text: 'QEMU 调试', link: '/zh/troubleshooting/qemu-debugging' },
            { text: '启动故障', link: '/zh/troubleshooting/boot-failures' },
            { text: '网络问题', link: '/zh/troubleshooting/network-issues' },
            { text: '存储问题', link: '/zh/troubleshooting/storage-issues' }
          ]
        },
        {
          text: '发布工程',
          collapsed: true,
          items: [
            { text: '发布流程（SOP）', link: '/zh/release/process' },
            { text: '版本号规则', link: '/zh/release/versioning' },
            { text: '发布说明模板', link: '/zh/release/release-notes-template' },
            { text: '官网更新机制', link: '/zh/release/website-update' },
            { text: 'Wiki 同步机制', link: '/zh/release/wiki-sync' }
          ]
        }
      ]
    },

    footer: {
      message: 'Released under the Apache 2.0 License.',
      copyright: 'Copyright 2026 cubestudio-dev'
    },

    docFooter: { prev: 'Previous', next: 'Next' },

    externalLinkIcon: true
  }
})
