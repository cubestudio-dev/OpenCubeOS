import{_ as a,o as n,c as t,a2 as e}from"./chunks/framework.BM5YPQmL.js";const g=JSON.parse('{"title":"应用字典 - 摄像头 (10)","description":"","frontmatter":{},"headers":[],"relativePath":"zh/manual/dictionary/camera.md","filePath":"zh/manual/dictionary/camera.md"}'),p={name:"zh/manual/dictionary/camera.md"};function l(d,s,r,c,i,o){return n(),t("div",null,[...s[0]||(s[0]=[e(`<h1 id="应用字典-摄像头-10" tabindex="-1">应用字典 - 摄像头 (10) <a class="header-anchor" href="#应用字典-摄像头-10" aria-label="Permalink to &quot;应用字典 - 摄像头 (10)&quot;">​</a></h1><h3 id="gameport" tabindex="-1">gameport <a class="header-anchor" href="#gameport" aria-label="Permalink to &quot;gameport&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>gameport</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>游戏端口状态</td></tr><tr><td>用法</td><td><code>gameport</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>g6_oom</span></span>
<span class="line"><span>g6_oom: start</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000007  rip=0x00000000004000D4  cs=0x0000000000000033  rflags=0x0000000000010246</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x000000003C000000  (faulting address)</span></span>
<span class="line"><span>*** KILLING user process (pid=28) due to exception 14, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=14), kernel continues</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="gameporttest" tabindex="-1">gameporttest <a class="header-anchor" href="#gameporttest" aria-label="Permalink to &quot;gameporttest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>gameporttest</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>游戏端口测试</td></tr><tr><td>用法</td><td><code>gameporttest</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>g6_test</span></span>
<span class="line"><span>g6_test: start</span></span>
<span class="line"><span>g6_test: T2 fork-mmap-inherit start</span></span>
<span class="line"><span>S0</span></span>
<span class="line"><span>S1</span></span>
<span class="line"><span>S2</span></span>
<span class="line"><span>SP</span></span>
<span class="line"><span>SC</span></span>
<span class="line"><span>g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: T5 per-proc-cwd start</span></span>
<span class="line"><span>g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: T1 exit0-reap start</span></span>
<span class="line"><span>[user] process 33 exited (code=42)</span></span>
<span class="line"><span>g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: T3 write2-no-reader start</span></span>
<span class="line"><span>g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: T4 sig-restorer start</span></span>
<span class="line"><span>g6_test: T4 handler entered</span></span>
<span class="line"><span>g6_test: T4 back from handler</span></span>
<span class="line"><span>g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: T7 solib-reject start</span></span>
<span class="line"><span>g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: T6 solib-window start</span></span>
<span class="line"><span>g6_test: T6 dlopen count = 3734g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: failed tests = 0g6_test: PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="ir" tabindex="-1">ir <a class="header-anchor" href="#ir" aria-label="Permalink to &quot;ir&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>ir</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>红外接收状态</td></tr><tr><td>用法</td><td><code>ir</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>install</span></span>
<span class="line"><span>usage: install &lt;device&gt;  (install the running system; DESTROYS existing data)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="irtest" tabindex="-1">irtest <a class="header-anchor" href="#irtest" aria-label="Permalink to &quot;irtest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>irtest</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>红外接收测试</td></tr><tr><td>用法</td><td><code>irtest</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>ip</span></span>
<span class="line"><span>IP: 0.0.0.0</span></span>
<span class="line"><span>Mask: 0.0.0.0</span></span>
<span class="line"><span>Gateway: 0.0.0.0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="parallel" tabindex="-1">parallel <a class="header-anchor" href="#parallel" aria-label="Permalink to &quot;parallel&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>parallel</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>并口状态</td></tr><tr><td>用法</td><td><code>parallel</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>parallel</span></span>
<span class="line"><span>parallel ports: 1</span></span>
<span class="line"><span>  lpt1 io=0x378 irq=7 mode=SPP ecp=no bidir=no tx=0 rx=0 timeouts=0 status=0xd9</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="paralleltest" tabindex="-1">paralleltest <a class="header-anchor" href="#paralleltest" aria-label="Permalink to &quot;paralleltest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>paralleltest</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>并口测试</td></tr><tr><td>用法</td><td><code>paralleltest</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>paralleltest</span></span>
<span class="line"><span>paralleltest: IEEE 1284 register-level self test</span></span>
<span class="line"><span>  mode SPP: PASS</span></span>
<span class="line"><span>  spp forward write 8 bytes: PASS</span></span>
<span class="line"><span>  status read: PASS</span></span>
<span class="line"><span>  status: raw=0x59 busy=0 ack=1 sel=1 err=1</span></span>
<span class="line"><span>  nibble reverse honest: PASS</span></span>
<span class="line"><span>  mode EPP1.7: PASS</span></span>
<span class="line"><span>  epp1.7 write honest: PASS</span></span>
<span class="line"><span>  mode EPP1.9: PASS</span></span>
<span class="line"><span>  epp1.9 read honest: PASS</span></span>
<span class="line"><span>  epp addr write honest: PASS</span></span>
<span class="line"><span>  ecp capability: no</span></span>
<span class="line"><span>  ecp absent honest: PASS</span></span>
<span class="line"><span>paralleltest: 0 failures - PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="scsi" tabindex="-1">scsi <a class="header-anchor" href="#scsi" aria-label="Permalink to &quot;scsi&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>scsi</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>SCSI 设备扫描</td></tr><tr><td>用法</td><td><code>scsi</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>scsi</span></span>
<span class="line"><span>scsi HBAs: 0</span></span>
<span class="line"><span>scsi devices: 0</span></span>
<span class="line"><span>  (no SCSI targets present; QEMU models one via -device lsi)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="scsitest" tabindex="-1">scsitest <a class="header-anchor" href="#scsitest" aria-label="Permalink to &quot;scsitest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>scsitest</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>SCSI 设备测试</td></tr><tr><td>用法</td><td><code>scsitest</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>scsitest</span></span>
<span class="line"><span>scsitest: CDB goldens, parsers, live target checks</span></span>
<span class="line"><span>  read10 golden: PASS</span></span>
<span class="line"><span>  write10 golden: PASS</span></span>
<span class="line"><span>  inquiry golden: PASS</span></span>
<span class="line"><span>  tur golden: PASS</span></span>
<span class="line"><span>  rc16 action: PASS</span></span>
<span class="line"><span>  inq parse: PASS</span></span>
<span class="line"><span>  cap10 parse: PASS</span></span>
<span class="line"><span>  cap16 parse: PASS</span></span>
<span class="line"><span>  sense parse: PASS</span></span>
<span class="line"><span>  (no live SCSI target: pure-function checks above ran on the real code paths)</span></span>
<span class="line"><span>scsitest: 0 failures - PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="uvc" tabindex="-1">uvc <a class="header-anchor" href="#uvc" aria-label="Permalink to &quot;uvc&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>uvc</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>UVC 摄像头状态</td></tr><tr><td>用法</td><td><code>uvc</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>uvc</span></span>
<span class="line"><span>camera subsystem: 0 cameras</span></span>
<span class="line"><span>  no camera bound (UVC class driver active; QEMU has no UVC model - attach real hardware or check \`usb\`)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="uvctest" tabindex="-1">uvctest <a class="header-anchor" href="#uvctest" aria-label="Permalink to &quot;uvctest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>uvctest</td></tr><tr><td>分类</td><td>摄像头</td></tr><tr><td>功能</td><td>UVC 摄像头测试</td></tr><tr><td>用法</td><td><code>uvctest</code></td></tr><tr><td>来源</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>uvctest</span></span>
<span class="line"><span>uvctest: lib_mjpeg + camera core + UVC descriptor parse</span></span>
<span class="line"><span>  mjpeg decode rc: PASS</span></span>
<span class="line"><span>  mjpeg size 16x16: PASS</span></span>
<span class="line"><span>  mjpeg pixels all 200: PASS</span></span>
<span class="line"><span>  mjpeg truncated rejected: PASS</span></span>
<span class="line"><span>  mjpeg small buffer -&gt; -2: PASS</span></span>
<span class="line"><span>  yuy2-&gt;rgb BT.601: PASS</span></span>
<span class="line"><span>  camera register: PASS</span></span>
<span class="line"><span>  camera get: PASS</span></span>
<span class="line"><span>  set format: PASS</span></span>
<span class="line"><span>  set format unknown rejected: PASS</span></span>
<span class="line"><span>  set frame: PASS</span></span>
<span class="line"><span>  set frame mismatch rejected: PASS</span></span>
<span class="line"><span>  set interval: PASS</span></span>
<span class="line"><span>  capture refused before start: PASS</span></span>
<span class="line"><span>  stream start: PASS</span></span>
<span class="line"><span>  capture while streaming: PASS</span></span>
<span class="line"><span>  release frame: PASS</span></span>
<span class="line"><span>  brightness step snap: PASS</span></span>
<span class="line"><span>  stream stop: PASS</span></span>
<span class="line"><span>  camera unregister: PASS</span></span>
<span class="line"><span>  uvc bind: PASS</span></span>
<span class="line"><span>  uvc version 1.1: PASS</span></span>
<span class="line"><span>  uvc 2 formats: PASS</span></span>
<span class="line"><span>  uvc MJPEG 640x480: PASS</span></span>
<span class="line"><span>  uvc intervals 666666/1333333: PASS</span></span>
<span class="line"><span>  uvc YUY2 320x240: PASS</span></span>
<span class="line"><span>  uvc terminals CT=1 PU=2: PASS</span></span>
<span class="line"><span>  uvc caps: PASS</span></span>
<span class="line"><span>  uvc descriptor dump: PASS</span></span>
<span class="line"><span>  uvc disconnect: PASS</span></span>
<span class="line"><span>  live cameras: 0 (none bound - no UVC hardware in this machine; the tests above ran the real code paths on spec fixtures)</span></span>
<span class="line"><span>uvctest: 0 failures - PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,41)])])}const u=a(p,[["render",l]]);export{g as __pageData,u as default};
