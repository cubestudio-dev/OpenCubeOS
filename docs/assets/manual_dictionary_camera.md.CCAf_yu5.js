import{_ as s,o as n,c as t,a2 as e}from"./chunks/framework.BM5YPQmL.js";const u=JSON.parse('{"title":"Application dictionary - Cameras (10)","description":"","frontmatter":{},"headers":[],"relativePath":"manual/dictionary/camera.md","filePath":"manual/dictionary/camera.md"}'),p={name:"manual/dictionary/camera.md"};function l(r,a,c,d,i,o){return n(),t("div",null,[...a[0]||(a[0]=[e(`<h1 id="application-dictionary-cameras-10" tabindex="-1">Application dictionary - Cameras (10) <a class="header-anchor" href="#application-dictionary-cameras-10" aria-label="Permalink to &quot;Application dictionary - Cameras (10)&quot;">​</a></h1><h3 id="gameport" tabindex="-1">gameport <a class="header-anchor" href="#gameport" aria-label="Permalink to &quot;gameport&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>gameport</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>game port status</td></tr><tr><td>Usage</td><td><code>gameport</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>gameport</span></span>
<span class="line"><span>game ports: 1</span></span>
<span class="line"><span>  gp0 io=0x201 calibrated=no reads=0 timeouts=0</span></span>
<span class="line"><span>    axis 0: no stick (timeout) rc=0</span></span>
<span class="line"><span>    axis 1: no stick (timeout) rc=0</span></span>
<span class="line"><span>    axis 2: no stick (timeout) rc=0</span></span>
<span class="line"><span>    axis 3: no stick (timeout) rc=0</span></span>
<span class="line"><span>    buttons=0x00</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="gameporttest" tabindex="-1">gameporttest <a class="header-anchor" href="#gameporttest" aria-label="Permalink to &quot;gameporttest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>gameporttest</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>game port test</td></tr><tr><td>Usage</td><td><code>gameporttest</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>gameporttest</span></span>
<span class="line"><span>gameporttest: analog joystick port self test</span></span>
<span class="line"><span>  axis read0: PASS</span></span>
<span class="line"><span>  axis read1: PASS</span></span>
<span class="line"><span>  axis read2: PASS</span></span>
<span class="line"><span>  axis read3: PASS</span></span>
<span class="line"><span>  buttons read: PASS</span></span>
<span class="line"><span>  calibrate runs: PASS</span></span>
<span class="line"><span>  calibration windows: [0,0] [0,0] [0,0] [0,0] </span></span>
<span class="line"><span>gameporttest: 0 failures - PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="ir" tabindex="-1">ir <a class="header-anchor" href="#ir" aria-label="Permalink to &quot;ir&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>ir</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>infrared receiver status</td></tr><tr><td>Usage</td><td><code>ir</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>ir</span></span>
<span class="line"><span>ir devices: 0</span></span>
<span class="line"><span>  no IR device present (QEMU models neither CIR hardware nor an IrDA UART; the SIR probe runs against the secondary 16550 at boot)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="irtest" tabindex="-1">irtest <a class="header-anchor" href="#irtest" aria-label="Permalink to &quot;irtest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>irtest</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>IR receiver test</td></tr><tr><td>Usage</td><td><code>irtest</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>irtest</span></span>
<span class="line"><span>irtest: NEC / RC5 / RC6 / SIRC codec round trips</span></span>
<span class="line"><span>  nec known (1A/3C): PASS</span></span>
<span class="line"><span>  rc5 known (05/15 t1): PASS</span></span>
<span class="line"><span>  rc6 known (12/34 t0): PASS</span></span>
<span class="line"><span>  rt nec 0x12/0x34: PASS</span></span>
<span class="line"><span>  rt nec 0xFF/0x00: PASS</span></span>
<span class="line"><span>  rt nec repeat: PASS</span></span>
<span class="line"><span>  rt rc5 0x0A/0x0C t0: PASS</span></span>
<span class="line"><span>  rt rc5 0x1B/0x2D t1: PASS</span></span>
<span class="line"><span>  rt rc6 0x00/0xFF t0: PASS</span></span>
<span class="line"><span>  rt rc6 0xA5/0x5A t1: PASS</span></span>
<span class="line"><span>  rt sirc 0x15/0x01: PASS</span></span>
<span class="line"><span>  nec corrupted rejected: PASS</span></span>
<span class="line"><span>  live ir devices: 0</span></span>
<span class="line"><span>  (no live device: codec tests above ran on the real code paths)</span></span>
<span class="line"><span>irtest: 0 failures - PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="parallel" tabindex="-1">parallel <a class="header-anchor" href="#parallel" aria-label="Permalink to &quot;parallel&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>parallel</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>parallel port status</td></tr><tr><td>Usage</td><td><code>parallel</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>parallel</span></span>
<span class="line"><span>parallel ports: 1</span></span>
<span class="line"><span>  lpt1 io=0x378 irq=7 mode=SPP ecp=no bidir=no tx=0 rx=0 timeouts=0 status=0xd9</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="paralleltest" tabindex="-1">paralleltest <a class="header-anchor" href="#paralleltest" aria-label="Permalink to &quot;paralleltest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>paralleltest</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>parallel port test</td></tr><tr><td>Usage</td><td><code>paralleltest</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>paralleltest</span></span>
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
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="scsi" tabindex="-1">scsi <a class="header-anchor" href="#scsi" aria-label="Permalink to &quot;scsi&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>scsi</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>SCSI device scan</td></tr><tr><td>Usage</td><td><code>scsi</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>scsi</span></span>
<span class="line"><span>scsi HBAs: 0</span></span>
<span class="line"><span>scsi devices: 0</span></span>
<span class="line"><span>  (no SCSI targets present; QEMU models one via -device lsi)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="scsitest" tabindex="-1">scsitest <a class="header-anchor" href="#scsitest" aria-label="Permalink to &quot;scsitest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>scsitest</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>SCSI device test</td></tr><tr><td>Usage</td><td><code>scsitest</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>scsitest</span></span>
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
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="uvc" tabindex="-1">uvc <a class="header-anchor" href="#uvc" aria-label="Permalink to &quot;uvc&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>uvc</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>UVC camera status</td></tr><tr><td>Usage</td><td><code>uvc</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>uvc</span></span>
<span class="line"><span>camera subsystem: 0 cameras</span></span>
<span class="line"><span>  no camera bound (UVC class driver active; QEMU has no UVC model - attach real hardware or check \`usb\`)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="uvctest" tabindex="-1">uvctest <a class="header-anchor" href="#uvctest" aria-label="Permalink to &quot;uvctest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>uvctest</td></tr><tr><td>Category</td><td>Cameras</td></tr><tr><td>Function</td><td>UVC camera test</td></tr><tr><td>Usage</td><td><code>uvctest</code></td></tr><tr><td>Source</td><td><code>ocos/services/camera/src/svc_camera_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>uvctest</span></span>
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
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,41)])])}const S=s(p,[["render",l]]);export{u as __pageData,S as default};
