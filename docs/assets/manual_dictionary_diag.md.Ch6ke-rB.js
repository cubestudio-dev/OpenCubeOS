import{_ as t,o as a,c as n,a2 as e}from"./chunks/framework.BM5YPQmL.js";const u=JSON.parse('{"title":"Application dictionary - Diagnostics (39)","description":"","frontmatter":{},"headers":[],"relativePath":"manual/dictionary/diag.md","filePath":"manual/dictionary/diag.md"}'),d={name:"manual/dictionary/diag.md"};function p(i,s,c,l,r,o){return a(),n("div",null,[...s[0]||(s[0]=[e(`<h1 id="application-dictionary-diagnostics-39" tabindex="-1">Application dictionary - Diagnostics (39) <a class="header-anchor" href="#application-dictionary-diagnostics-39" aria-label="Permalink to &quot;Application dictionary - Diagnostics (39)&quot;">​</a></h1><h3 id="acpi" tabindex="-1">acpi <a class="header-anchor" href="#acpi" aria-label="Permalink to &quot;acpi&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>acpi</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>ACPI table summary</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>acpi</span></span>
<span class="line"><span>RSDP: 0x00050178 rev 0 OEM BOCHS </span></span>
<span class="line"><span>tables: 5</span></span>
<span class="line"><span>  RSDT rev=1 len=52 @0x1ffe232f</span></span>
<span class="line"><span>  FACP rev=1 len=116 @0x1ffe21e3</span></span>
<span class="line"><span>  APIC rev=3 len=120 @0x1ffe2257</span></span>
<span class="line"><span>  HPET rev=1 len=56 @0x1ffe22cf</span></span>
<span class="line"><span>  WAET rev=1 len=40 @0x1ffe2307</span></span>
<span class="line"><span>FADT: rev 1 SCI=IRQ9 PM1a_CNT=0x0604 GPE0=0xafe0 GPE0len=4</span></span>
<span class="line"><span>DSDT @0x1ffe0040 SSDTs=0 FACS @0x1ffe0000</span></span>
<span class="line"><span>MCFG: not present (legacy CF8/CFC only)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="acpidev" tabindex="-1">acpidev <a class="header-anchor" href="#acpidev" aria-label="Permalink to &quot;acpidev&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>acpidev</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>ACPI device tree</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>acpidev</span></span>
<span class="line"><span>AML namespace: nodes=342 methods=102 devices=54</span></span>
<span class="line"><span>  \\_SB_.PCI0 HID=PNP0a03</span></span>
<span class="line"><span>  \\_SB_.PCI0.PRES HID=PNP0a06</span></span>
<span class="line"><span>  \\_SB_.PCI0.GPE0 HID=PNP0A06 STA=0x0b</span></span>
<span class="line"><span>  \\_SB_.PCI0.PHPR HID=PNP0A06 STA=0x0b</span></span>
<span class="line"><span>  \\_SB_.PCI0.FWCF HID=QEMU0002 STA=0x0b</span></span>
<span class="line"><span>  \\_SB_.PCI0.S00_</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_ ADR=0x10000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_.KBD_ HID=PNP0303 STA=0x0f</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_.MOU_ HID=PNP0f13 STA=0x0f</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_.FDC0 HID=PNP0700</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_.FDC0.FLPA</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_.LPT1 HID=PNP0400 STA=0x0f</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_.COM1 HID=PNP0501 STA=0x0f</span></span>
<span class="line"><span>  \\_SB_.PCI0.S08_.RTC_ HID=PNP0b00</span></span>
<span class="line"><span>  \\_SB_.PCI0.S09_ ADR=0x10001</span></span>
<span class="line"><span>  \\_SB_.PCI0.S0B_ ADR=0x10003</span></span>
<span class="line"><span>  \\_SB_.PCI0.S10_ ADR=0x20000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S18_ ADR=0x30000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S20_ ADR=0x40000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S28_ ADR=0x50000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S30_ ADR=0x60000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S38_ ADR=0x70000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S40_ ADR=0x80000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S48_ ADR=0x90000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S50_ ADR=0xa0000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S58_ ADR=0xb0000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S60_ ADR=0xc0000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S68_ ADR=0xd0000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S70_ ADR=0xe0000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S78_ ADR=0xf0000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S80_ ADR=0x100000</span></span>
<span class="line"><span>  \\_SB_.PCI0.S88_ ADR=0x110000</span></span>
<span class="line"><span>  ... (truncated at 32)</span></span>
<span class="line"><span>D</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="acpipow" tabindex="-1">acpipow <a class="header-anchor" href="#acpipow" aria-label="Permalink to &quot;acpipow&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>acpipow</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>ACPI power states</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>acpipow</span></span>
<span class="line"><span>sleep states offered: 0x38 (S1..S5 bit i-1)</span></span>
<span class="line"><span>  S1: not offered</span></span>
<span class="line"><span>  S2: not offered</span></span>
<span class="line"><span>  S3: SLP_TYP=0x01</span></span>
<span class="line"><span>  S4: SLP_TYP=0x02</span></span>
<span class="line"><span>  S5: SLP_TYP=0x00</span></span>
<span class="line"><span>SCI: IRQ 9  events: sci=0 gpe=0 pwrbtn=0</span></span>
<span class="line"><span>FACS: waking_vector=0x00000000 x_waking=0x00000000</span></span>
<span class="line"><span>resume: no S3 wake yet this boot</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="acpitest" tabindex="-1">acpitest <a class="header-anchor" href="#acpitest" aria-label="Permalink to &quot;acpitest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>acpitest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>ACPI parser test</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>acpitest</span></span>
<span class="line"><span>acpitest: AML interpreter self-test</span></span>
<span class="line"><span>  fixture table eval:        PASS</span></span>
<span class="line"><span>  \\_S5 SLP_TYP resolved: 0x00      PASS</span></span>
<span class="line"><span>  namespace device walk: 54        PASS</span></span>
<span class="line"><span>acpitest: PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="cr3test" tabindex="-1">cr3test <a class="header-anchor" href="#cr3test" aria-label="Permalink to &quot;cr3test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>cr3test</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>CR3 register audit</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>cr3test</span></span>
<span class="line"><span>CR3 switch test:</span></span>
<span class="line"><span>  current CR3=0x0000003ec000</span></span>
<span class="line"><span>  new AS CR3=0x000000767000</span></span>
<span class="line"><span>  map at 4GiB: OK</span></span>
<span class="line"><span>  switched CR3 to 0x000000767000</span></span>
<span class="line"><span>  verify mapped in new AS: OK phys=0x000000769000</span></span>
<span class="line"><span>  switched back to 0x0000003ec000</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="crashlog" tabindex="-1">crashlog <a class="header-anchor" href="#crashlog" aria-label="Permalink to &quot;crashlog&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>crashlog</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>crash dump records</td></tr><tr><td>Usage</td><td><code>crashlog [flush]</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>crashlog</span></span>
<span class="line"><span>crashlog: no exceptions recorded</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="crashtest" tabindex="-1">crashtest <a class="header-anchor" href="#crashtest" aria-label="Permalink to &quot;crashtest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>crashtest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>crash handling test triggers</td></tr><tr><td>Usage</td><td><code>crashtest panic|exc|...|policy ...; crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]</code></td></tr><tr><td>Hidden parameters</td><td>hidden parameter, not a public interface; compatibility is not guaranteed: <code>0</code>, <code>1</code>, <code>help</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>crashtest</span></span>
<span class="line"><span>usage: crashtest panic|exc|pf|assert|wd|ovf|dump|policy [halt|reboot]</span></span>
<span class="line"><span>  panic   : kernel_panic() direct</span></span>
<span class="line"><span>  exc     : ring-0 #UD exception</span></span>
<span class="line"><span>  pf      : ring-0 unmapped-address write fault</span></span>
<span class="line"><span>  assert  : OC_ASSERT(0)</span></span>
<span class="line"><span>  wd      : forced watchdog timeout</span></span>
<span class="line"><span>  ovf     : kernel stack overflow (guard page)</span></span>
<span class="line"><span>  dump    : capture a core dump without panicking</span></span>
<span class="line"><span>  policy  : show/set the after-panic policy</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="dumpinfo" tabindex="-1">dumpinfo <a class="header-anchor" href="#dumpinfo" aria-label="Permalink to &quot;dumpinfo&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>dumpinfo</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>core dump status</td></tr><tr><td>Usage</td><td><code>dumpinfo : core dump status</code></td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>dumpinfo</span></span>
<span class="line"><span>core dump status:</span></span>
<span class="line"><span>  no dump captured yet (trigger: crashtest panic|exc|pf)</span></span>
<span class="line"><span>policy: HALT  watchdog: off (timeout 15000 ms)</span></span>
<span class="line"><span></span></span>
<span class="line"><span>Block devices:</span></span>
<span class="line"><span>drivers: ATA-DMA drives=0  AHCI drives=0  NVMe queues=0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="exc" tabindex="-1">exc <a class="header-anchor" href="#exc" aria-label="Permalink to &quot;exc&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>exc</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>exception report status</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>exc</span></span>
<span class="line"><span>Running exception self-test (#DE, #UD, #PF)...</span></span>
<span class="line"><span>passed 3 exceptions, kernel alive</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="frag" tabindex="-1">frag <a class="header-anchor" href="#frag" aria-label="Permalink to &quot;frag&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>frag</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>PMM fragment report</td></tr><tr><td>Usage</td><td><code>frag : PMM fragments</code></td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>frag</span></span>
<span class="line"><span>PMM: free_pages=128537 fragments=3 avg_frag=42845</span></span>
<span class="line"><span>Heap: free_blocks=19 free_bytes=63360</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="fstest" tabindex="-1">fstest <a class="header-anchor" href="#fstest" aria-label="Permalink to &quot;fstest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>fstest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>VFS stress test</td></tr><tr><td>Usage</td><td><code>G10 range (chipset) - reserved, bodies land with the group</code></td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>fstest</span></span>
<span class="line"><span>vfs: cannot unmount the root filesystem</span></span>
<span class="line"><span>VFS self-test:</span></span>
<span class="line"><span>  mkdir /tmp/vfstest: OK</span></span>
<span class="line"><span>  open(O_CREAT): OK</span></span>
<span class="line"><span>  write: OK</span></span>
<span class="line"><span>  read: OK -&gt; &quot;hello from ramfs&quot;</span></span>
<span class="line"><span>  ls /tmp/vfstest:</span></span>
<span class="line"><span>    hello.txt</span></span>
<span class="line"><span>  ramfs: 12 nodes, 4054 bytes</span></span>
<span class="line"><span>  p2 regressions:</span></span>
<span class="line"><span>    BUG-0178 dual-sided: PASS</span></span>
<span class="line"><span>    BUG-0179 dual-sided: PASS</span></span>
<span class="line"><span>    BUG-0180 hole is zeros: PASS</span></span>
<span class="line"><span>    BUG-0182 dual-sided: PASS</span></span>
<span class="line"><span>    BUG-0184 umount / refused: PASS</span></span>
<span class="line"><span>    p2 regressions: ALL PASS</span></span>
<span class="line"><span>  p1fix4 regressions:</span></span>
<span class="line"><span>    BUG-0061 dual-sided: PASS</span></span>
<span class="line"><span>    p1fix4 regressions: ALL PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="heap" tabindex="-1">heap <a class="header-anchor" href="#heap" aria-label="Permalink to &quot;heap&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>heap</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>kernel heap statistics</td></tr><tr><td>Usage</td><td><code>heap : kernel heap stats</code></td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>heap</span></span>
<span class="line"><span>Kernel heap:</span></span>
<span class="line"><span>  size:  2220032 bytes</span></span>
<span class="line"><span>  alloc: 1215232 bytes (117 blocks)</span></span>
<span class="line"><span>  free:  997632 bytes (107 blocks)</span></span>
<span class="line"><span>  overhead: 7168 bytes</span></span>
<span class="line"><span>  total allocs: 263  total frees: 146</span></span>
<span class="line"><span>£</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="heapbounds" tabindex="-1">heapbounds <a class="header-anchor" href="#heapbounds" aria-label="Permalink to &quot;heapbounds&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>heapbounds</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>heap boundary check</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>heapbounds</span></span>
<span class="line"><span>Heap bounds test (BUG-0140 wrap / BUG-0141 ceiling):</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="heaptest" tabindex="-1">heaptest <a class="header-anchor" href="#heaptest" aria-label="Permalink to &quot;heaptest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>heaptest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>heap stress test</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>heaptest</span></span>
<span class="line"><span>Heap overhead test (100 allocs):</span></span>
<span class="line"><span>  heap_size=2400256 allocated=1247056 overhead=10848</span></span>
<span class="line"><span>  requested=26542 waste=1220514 overhead%=0</span></span>
<span class="line"><span>  after free: alloc=119 free=127 total_allocs=375 total_frees=256</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="i2cdetect" tabindex="-1">i2cdetect <a class="header-anchor" href="#i2cdetect" aria-label="Permalink to &quot;i2cdetect&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>i2cdetect</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>scan the I2C bus</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>i2cdetect</span></span>
<span class="line"><span>i2c-0 (piix4): -- -- -- -- -- -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>  50 51 52 53 54 55 56 57 -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>  -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>  -- -- -- -- -- -- -- -- </span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="i2cget" tabindex="-1">i2cget <a class="header-anchor" href="#i2cget" aria-label="Permalink to &quot;i2cget&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>i2cget</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>read an I2C register</td></tr><tr><td>Usage</td><td><code>; i2cget &lt;bus&gt; &lt;addr&gt; [reg] (addr/reg in hex, 0x..</code></td></tr><tr><td>Parameters</td><td><code>bus</code>, <code>addr</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>i2cget</span></span>
<span class="line"><span>usage: i2cget &lt;bus&gt; &lt;addr&gt; [reg]  (addr/reg in hex, 0x..)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="i2cset" tabindex="-1">i2cset <a class="header-anchor" href="#i2cset" aria-label="Permalink to &quot;i2cset&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>i2cset</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>write an I2C register</td></tr><tr><td>Usage</td><td><code>; i2cset &lt;bus&gt; &lt;addr&gt; &lt;reg&gt; &lt;val&gt; (hex values</code></td></tr><tr><td>Parameters</td><td><code>bus</code>, <code>addr</code>, <code>reg</code>, <code>val</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>i2cset</span></span>
<span class="line"><span>usage: i2cset &lt;bus&gt; &lt;addr&gt; &lt;reg&gt; &lt;val&gt;  (hex values)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="i2ctest" tabindex="-1">i2ctest <a class="header-anchor" href="#i2ctest" aria-label="Permalink to &quot;i2ctest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>i2ctest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>I2C bus test</td></tr><tr><td>Hidden parameters</td><td>hidden parameter, not a public interface; compatibility is not guaranteed: <code>dw-i2c</code></td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>i2ctest</span></span>
<span class="line"><span>I2C core: 1 bus(es) registered</span></span>
<span class="line"><span>controller: PIIX4 (8086:7113) base 0x0700 xfers=80 errs=202</span></span>
<span class="line"><span>  diag: cfg0x90=0x00000701 sts=0x00</span></span>
<span class="line"><span>DesignWare: not present on this platform (honest skip)</span></span>
<span class="line"><span>10-bit addressing: supported on DesignWare bus (none present)</span></span>
<span class="line"><span>i2ctest: PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="irqabitest" tabindex="-1">irqabitest <a class="header-anchor" href="#irqabitest" aria-label="Permalink to &quot;irqabitest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>irqabitest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>IRQ affinity test</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>irqabitest</span></span>
<span class="line"><span>IRQ-entry ABI test (BUG-0137 align / BUG-0138 DF):</span></span>
<span class="line"><span>  timer-IRQ bits=0x3  int0x80 bits=0x3</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span>probe runs against the secondary 16550 at boot)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="mem" tabindex="-1">mem <a class="header-anchor" href="#mem" aria-label="Permalink to &quot;mem&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>mem</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>physical memory statistics</td></tr><tr><td>Usage</td><td><code>mem : PMM stats</code></td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>mem</span></span>
<span class="line"><span>Physical memory:</span></span>
<span class="line"><span>  total: 536739840 bytes (131040 pages)</span></span>
<span class="line"><span>  used:  10473472 bytes (2557 pages)</span></span>
<span class="line"><span>  free:  526266368 bytes (128483 pages)</span></span>
<span class="line"><span>  fragments: 4</span></span>
<span class="line"><span>  allocs: 821  frees: 392  failures: 0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="memtest" tabindex="-1">memtest <a class="header-anchor" href="#memtest" aria-label="Permalink to &quot;memtest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>memtest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>memory service test battery</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>memtest</span></span>
<span class="line"><span>PMM test:</span></span>
<span class="line"><span>  alloc 10 pages: OK</span></span>
<span class="line"><span>  free 10 pages: OK</span></span>
<span class="line"><span>Heap test:</span></span>
<span class="line"><span>  kmalloc 5 blocks: OK</span></span>
<span class="line"><span>  krealloc to 512: OK</span></span>
<span class="line"><span>  kfree all: OK</span></span>
<span class="line"><span>  double-free detection: OK (no crash)</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="msitest" tabindex="-1">msitest <a class="header-anchor" href="#msitest" aria-label="Permalink to &quot;msitest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>msitest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>MSI interrupt test</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>msitest</span></span>
<span class="line"><span>LAPIC: present, id=0</span></span>
<span class="line"><span>  self-IPI vector 0x50: fired 1 time(s)  PASS</span></span>
<span class="line"><span>MSI-capable devices: 0 of 5</span></span>
<span class="line"><span>  e1000 not present: MSI device-fire skipped</span></span>
<span class="line"><span>msitest: PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="multi" tabindex="-1">multi <a class="header-anchor" href="#multi" aria-label="Permalink to &quot;multi&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>multi</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>multi-core wake test</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>multi</span></span>
<span class="line"><span>Spawning 3 tasks...</span></span>
<span class="line"><span>3 tasks spawned (same priority). Output should interleave.</span></span>
<span class="line"><span>  [task 1] iter 0</span></span>
<span class="line"><span>  [task 2] iter 0</span></span>
<span class="line"><span>  [task 3] iter 0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="nice" tabindex="-1">nice <a class="header-anchor" href="#nice" aria-label="Permalink to &quot;nice&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>nice</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>priority demo</td></tr><tr><td>Usage</td><td><code>; nice &lt;tid&gt; &lt;prio&gt; (0=highest..31=lowest</code></td></tr><tr><td>Parameters</td><td><code>tid</code>, <code>prio</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>nice</span></span>
<span class="line"><span>usage: nice &lt;tid&gt; &lt;prio&gt; (0=highest..31=lowest)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="pcibars" tabindex="-1">pcibars <a class="header-anchor" href="#pcibars" aria-label="Permalink to &quot;pcibars&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>pcibars</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>PCI BAR assignment check</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pcibars</span></span>
<span class="line"><span>PCI devices: 5</span></span>
<span class="line"><span> 00:00.0 vid=0x8086 did=0x1237 class=0x060000</span></span>
<span class="line"><span> 00:01.0 vid=0x8086 did=0x7000 class=0x060100</span></span>
<span class="line"><span> 00:01.1 vid=0x8086 did=0x7010 class=0x010180</span></span>
<span class="line"><span>   BAR4: base=0x0000c000 size=0x00000010 io</span></span>
<span class="line"><span> 00:01.3 vid=0x8086 did=0x7113 class=0x068000</span></span>
<span class="line"><span> 00:02.0 vid=0x1234 did=0x1111 class=0x030000</span></span>
<span class="line"><span>   BAR0: base=0xfd000000 size=0x01000000 mem prefetch</span></span>
<span class="line"><span>   BAR2: base=0xfebf0000 size=0x00001000 mem</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="pcidump" tabindex="-1">pcidump <a class="header-anchor" href="#pcidump" aria-label="Permalink to &quot;pcidump&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>pcidump</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>dump PCI configuration</td></tr><tr><td>Usage</td><td><code>; pcidump &lt;bus:dev.fn&gt; e.g. pcidump 0:1f.3</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pcidump</span></span>
<span class="line"><span>usage: pcidump &lt;bus:dev.fn&gt;  e.g. pcidump 0:1f.3</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="pcie" tabindex="-1">pcie <a class="header-anchor" href="#pcie" aria-label="Permalink to &quot;pcie&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>pcie</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>PCIe link status</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pcie</span></span>
<span class="line"><span>MMCONFIG: not active (MCFG absent) - legacy CF8/CFC only</span></span>
<span class="line"><span>capabilities per device:</span></span>
<span class="line"><span>  00:00.0 caps: (legacy)</span></span>
<span class="line"><span>  00:01.0 caps: (legacy)</span></span>
<span class="line"><span>  00:01.1 caps: (legacy)</span></span>
<span class="line"><span>  00:01.3 caps: (legacy)</span></span>
<span class="line"><span>  00:02.0 caps: (legacy)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="pftest" tabindex="-1">pftest <a class="header-anchor" href="#pftest" aria-label="Permalink to &quot;pftest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>pftest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>page-fault handler test</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pftest</span></span>
<span class="line"><span>Page fault test:</span></span>
<span class="line"><span>  test 1: illegal-PF-on-high-address (counter test)...</span></span>
<span class="line"><span>    faults delta=1 legal_delta=1</span></span>
<span class="line"><span>    OK (L1 handler caught the high-address PF)</span></span>
<span class="line"><span>  test 2: illegal PF (unmapped address)...</span></span>
<span class="line"><span>    faults before=2 after=3</span></span>
<span class="line"><span>    legal=2 illegal=1 stack=0 heap=0</span></span>
<span class="line"><span>    L1 handler ran, page mapped, write verified, 1 legal PFs counted</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="pmmrace" tabindex="-1">pmmrace <a class="header-anchor" href="#pmmrace" aria-label="Permalink to &quot;pmmrace&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>pmmrace</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>PMM race detector</td></tr><tr><td>Return codes</td><td>1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pmmrace</span></span></code></pre></div><p>(Note: restored from the f5af2ab6 capture (command behavior unchanged since))</p><h3 id="sched" tabindex="-1">sched <a class="header-anchor" href="#sched" aria-label="Permalink to &quot;sched&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>sched</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>scheduler statistics</td></tr><tr><td>Usage</td><td><code>sched : scheduler stats</code></td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>sched</span></span>
<span class="line"><span>switches=248 preemptions=110 current_tid=3 active_tasks=4</span></span>
<span class="line"><span>PED (no ISA SB16 (QEMU: add -device sb16,audiodev=snd0))</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="smbus" tabindex="-1">smbus <a class="header-anchor" href="#smbus" aria-label="Permalink to &quot;smbus&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>smbus</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>SMBus status</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>smbus</span></span>
<span class="line"><span>SMBus devices: 8</span></span>
<span class="line"><span>  0x50  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>  0x51  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>  0x52  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>  0x53  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>  0x54  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>  0x55  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>  0x56  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>  0x57  EEPROM  first bytes: 00 00 00 00 00 00 00 00 </span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="smbustest" tabindex="-1">smbustest <a class="header-anchor" href="#smbustest" aria-label="Permalink to &quot;smbustest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>smbustest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>SMBus test</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>smbustest</span></span>
<span class="line"><span>smbustest: byte/word/block round-trips  PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="spawn" tabindex="-1">spawn <a class="header-anchor" href="#spawn" aria-label="Permalink to &quot;spawn&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>spawn</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>background spawn demo</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>spawn</span></span>
<span class="line"><span>spawned tid=4</span></span>
<span class="line"><span></span></span>
<span class="line"><span>  (none)</span></span>
<span class="line"><span>  [test thread] iteration 0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="syncstat" tabindex="-1">syncstat <a class="header-anchor" href="#syncstat" aria-label="Permalink to &quot;syncstat&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>syncstat</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>sync primitive statistics</td></tr><tr><td>Usage</td><td><code>syncstat : sync primitive stats</code></td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>syncstat</span></span>
<span class="line"><span>spin_locks=8940347 spin_unlocks=8940347</span></span>
<span class="line"><span>sem_waits=0 sem_posts=0</span></span>
<span class="line"><span>mutex_locks=0 mutex_unlocks=0</span></span>
<span class="line"><span>cond_waits=0 cond_signals=0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="synctest" tabindex="-1">synctest <a class="header-anchor" href="#synctest" aria-label="Permalink to &quot;synctest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>synctest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>sync primitive test</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>synctest</span></span>
<span class="line"><span>=== Spinlock test (3 tasks x 1000) ===</span></span>
<span class="line"><span>  counter=3000 (expected 3000) PASS</span></span>
<span class="line"><span>=== Mutex test (3 tasks x 1000) ===</span></span>
<span class="line"><span>  counter=3000 (expected 3000) PASS</span></span>
<span class="line"><span>=== Semaphore test (3 tasks x 1000) ===</span></span>
<span class="line"><span>  counter=3000 (expected 3000) PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="timer" tabindex="-1">timer <a class="header-anchor" href="#timer" aria-label="Permalink to &quot;timer&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>timer</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>timer test</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>timer</span></span>
<span class="line"><span>Registered 500ms one-shot timer</span></span>
<span class="line"><span>arser=ok synrcvd-recycle=ok sack-map=ok; live negotiation covered by HTTPS E2E)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="vmkernelpt" tabindex="-1">vmkernelpt <a class="header-anchor" href="#vmkernelpt" aria-label="Permalink to &quot;vmkernelpt&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>vmkernelpt</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>kernel page-table dump</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>vmkernelpt</span></span>
<span class="line"><span>VMM kernel-PT test (BUG-0136 PT_OWNER):</span></span>
<span class="line"><span>  user AS created (copies split PD0)</span></span>
<span class="line"><span>  user AS destroyed</span></span>
<span class="line"><span>  pmm failures=0</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="vmmap" tabindex="-1">vmmap <a class="header-anchor" href="#vmmap" aria-label="Permalink to &quot;vmmap&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>vmmap</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>address-space map dump</td></tr><tr><td>Return codes</td><td>0</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>vmmap</span></span>
<span class="line"><span>Kernel address space (PML4 at 0x0000003ec000):</span></span>
<span class="line"><span>  PML4[0] vaddr=0x000000000000 -&gt; 0x0000003ed000</span></span>
<span class="line"><span></span></span>
<span class="line"><span>Page faults: total=0 legal=0 illegal=0 stack=0 heap=0</span></span>
<span class="line"><span>T@</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="vmtest" tabindex="-1">vmtest <a class="header-anchor" href="#vmtest" aria-label="Permalink to &quot;vmtest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>vmtest</td></tr><tr><td>Category</td><td>Diagnostics</td></tr><tr><td>Function</td><td>VMM unit test</td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/diag/src/svc_diag_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>vmtest</span></span>
<span class="line"><span>VMM test:</span></span>
<span class="line"><span>  created AS at 0x000000782000</span></span>
<span class="line"><span>  map_page(0x000100000000 -&gt; 0x000000784000) = 0</span></span>
<span class="line"><span>  is_mapped = 1 phys=0x000000784000</span></span>
<span class="line"><span>  protect(read-only) = 0</span></span>
<span class="line"><span>  unmap returned 0x000000784005</span></span>
<span class="line"><span>  is_mapped after unmap = 0</span></span>
<span class="line"><span>  destroyed AS</span></span>
<span class="line"><span>  PASS</span></span>
<span class="line"><span></span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,158)])])}const g=t(d,[["render",p]]);export{u as __pageData,g as default};
