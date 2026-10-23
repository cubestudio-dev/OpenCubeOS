// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 cubestudio-dev <cubestudio@qq.com>
// Open Cube OS docs center - VitePress config.
// srcDir points at the repository's docs/ tree (single source of truth);
// the facade (index.html / download.html) lives in website/ itself.
import { defineConfig } from 'vitepress'

export default defineConfig({
  // gh-pages serves this site under /OpenCubeOS/ (project pages):
  // the docs center lives at /OpenCubeOS/docs/ - base MUST match or every
  // asset URL 404s in production.
  base: '/OpenCubeOS/docs/',
  title: 'Open Cube OS',
  description: 'Documentation center for Open Cube OS - the from-scratch x86_64 kernel with the oc> recovery shell.',
  lang: 'en-US',
  // docs-src/docs/ is a BUILD-TIME copy of the repository's docs/ tree
  // (synced by the build script: single source of truth stays docs/).
  // Keeping the copy inside the project root is what makes bare-import
  // resolution (vue/server-renderer) work from every markdown file.
  srcDir: 'docs',
  srcExclude: ['docs/archive/**', 'docs/data/**'],
  outDir: '../out/docs',
  // cleanUrls would emit extensionless links; GitHub Pages does NOT
  // rewrite them (404). Keep the .html suffix - works everywhere.
  cleanUrls: false,
  cacheDir: './.vitepress/cache',

  head: [
    ['link', { rel: 'icon', href: 'data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 32 32%22%3E%3Crect width=%2232%22 height=%2232%22 rx=%226%22 fill=%22%230b0f0c%22/%3E%3Ctext x=%2216%22 y=%2222%22 font-family=%22monospace%22 font-size=%2214%22 fill=%22%2346d977%22 text-anchor=%22middle%22%3Eoc%3E%3C/text%3E%3C/svg%3E' }]
  ],

  themeConfig: {
    nav: [
      { text: 'Website', link: '../' },
      { text: 'Architecture', link: '../architecture.html' },
      { text: 'Download', link: '../download.html' },
      { text: 'About', link: '../about.html' },
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
