import{_ as s,o as n,c as t,a2 as e}from"./chunks/framework.BM5YPQmL.js";const b=JSON.parse('{"title":"Application dictionary - Kernel log (2)","description":"","frontmatter":{},"headers":[],"relativePath":"manual/dictionary/klog.md","filePath":"manual/dictionary/klog.md"}'),l={name:"manual/dictionary/klog.md"};function o(p,a,i,c,r,d){return n(),t("div",null,[...a[0]||(a[0]=[e(`<h1 id="application-dictionary-kernel-log-2" tabindex="-1">Application dictionary - Kernel log (2) <a class="header-anchor" href="#application-dictionary-kernel-log-2" aria-label="Permalink to &quot;Application dictionary - Kernel log (2)&quot;">​</a></h1><h3 id="logcat" tabindex="-1">logcat <a class="header-anchor" href="#logcat" aria-label="Permalink to &quot;logcat&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>logcat</td></tr><tr><td>Category</td><td>Kernel log</td></tr><tr><td>Function</td><td>filtered kernel log stream</td></tr><tr><td>Usage</td><td><code>logcat -l lvl -m mod -k key -t sec -n N [-f]</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/klog/src/svc_klog_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>logcat</span></span>
<span class="line"><span>[0.260] INFO  boot: [00:00:00.250] VFS (virtual file system)......................... OK</span></span>
<span class="line"><span>[0.290] INFO  boot: [00:00:00.280] ramfs (in-memory file system, mounted at /)....... OK</span></span>
<span class="line"><span>[0.500] INFO  boot: [00:00:00.490] ATA/IDE PIO driver (LBA28)........................ OK</span></span>
<span class="line"><span>[0.520] INFO  boot: [00:00:00.510] virtio-blk + AHCI + NVMe drivers.................. OK</span></span>
<span class="line"><span>[0.680] INFO  boot: [00:00:00.670] USB hosts up: 0................................... OK</span></span>
<span class="line"><span>[0.710] INFO  boot: [00:00:00.680] USB stack (4 HC backends, HID/MSC/serial/audio)... OK</span></span>
<span class="line"><span>[0.720] INFO  boot: [00:00:00.710] boot chain 3/4: driver-dependent services......... OK</span></span>
<span class="line"><span>[0.800] INFO  boot: [00:00:00.790] FAT32 (R/W) + exFAT (R/W) + ext4 (RO) drivers..... OK</span></span>
<span class="line"><span>[0.850] INFO  boot: config: created default /etc/opencube.conf (ramfs)</span></span>
<span class="line"><span>[0.860] INFO  boot: [00:00:00.850] system config (/etc/opencube.conf, fat32/ramfs) (OK)</span></span>
<span class="line"><span>[0.870] INFO  boot: [00:00:00.860] ACPI PM timer (FADT PM_TMR_BLK)................... OK</span></span>
<span class="line"><span>[0.880] INFO  boot: abupdate: no disk with an A/B partition layout found</span></span>
<span class="line"><span>[1.450] INFO  boot: [00:00:01.440] LAPIC (local APIC)................................ OK</span></span>
<span class="line"><span>[2.040] INFO  boot: [00:00:02.030] network stack (e1000 + TCP/IP + socket API)....... OK</span></span>
<span class="line"><span>[2.040] INFO  watchdog: slot 1 registered: kernel-health 15000 ms policy 0</span></span>
<span class="line"><span>[2.050] INFO  watchdog: slot 1 (kernel-health) ARMED, 15000 ms</span></span>
<span class="line"><span>[2.060] INFO  health: monitor started (1 s samples, alarms: mem&gt;90% tasks&gt;64 cpu&gt;=90%x10s)</span></span>
<span class="line"><span>[2.080] INFO  boot: [00:00:02.070] health monitor (cpu/mem/tasks/disk/net + alarms).. OK</span></span>
<span class="line"><span>[2.260] INFO  boot: [00:00:02.250] sound: no sound card present (probe OK)........... OK</span></span>
<span class="line"><span>[2.270] INFO  boot: block devices: 0 registered</span></span>
<span class="line"><span>[2.280] INFO  boot: disk cache: 64 slots, LRU write-back</span></span>
<span class="line"><span>[2.290] INFO  boot: [00:00:02.280] disk subsystem (block dev + cache + partition + FS). OK</span></span>
<span class="line"><span>[2.310] INFO  boot: pmm: 131040 pages, 128681 free</span></span>
<span class="line"><span>[2.320] INFO  boot: [00:00:02.310] kmalloc/kfree self-test........................... OK</span></span>
<span class="line"><span>[2.410] INFO  boot: [00:00:02.400] boot complete..................................... OK</span></span>
<span class="line"><span>[2.420] INFO  boot: [00:00:02.410] boot chain 4/4: interfaces ready.................. OK</span></span>
<span class="line"><span>[2.430] INFO  boot: [00:00:02.420] crash recovery armed (panic + dump + crashlog).... OK</span></span>
<span class="line"><span>[2.440] INFO  boot: [00:00:02.430] L0 ready, waiting for L1.......................... OK</span></span>
<span class="line"><span>[2.450] INFO  boot: l1 launcher: booting &#39;ush&#39; (ring 3, 23496 bytes)</span></span>
<span class="line"><span>[13.060] ERROR health: alarm: CPU &gt;=90% busy for 10 consecutive seconds</span></span>
<span class="line"><span>-- 30 lines --</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="logctl" tabindex="-1">logctl <a class="header-anchor" href="#logctl" aria-label="Permalink to &quot;logctl&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>logctl</td></tr><tr><td>Category</td><td>Kernel log</td></tr><tr><td>Function</td><td>kernel log level control</td></tr><tr><td>Usage</td><td><code>logctl status|level|...; logctl enable|disable debug|info|warn|error|fatal ; logctl filter clear | filter [level] [module] [keyword] ; logctl status|level|enable|disable|clear|flush|rotate|threshold|export|filter|test ; logctl threshold BYTES (min 512, default 1048576</code></td></tr><tr><td>Hidden parameters</td><td>hidden parameter, not a public interface; compatibility is not guaranteed: <code>-</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/klog/src/svc_klog_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>logctl</span></span>
<span class="line"><span>klog status:</span></span>
<span class="line"><span>  ring: 81/1024 entries, dropped 0</span></span>
<span class="line"><span>  live level (console+serial): 1 (INFO)</span></span>
<span class="line"><span>  level switches: DEBUG=on INFO=on WARN=on ERROR=on FATAL=on</span></span>
<span class="line"><span>  file: /var/log/kernel.log open, 3141 bytes (rotate at 1048576, keep 3)</span></span>
<span class="line"><span>  filter: none</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,9)])])}const h=s(l,[["render",o]]);export{b as __pageData,h as default};
