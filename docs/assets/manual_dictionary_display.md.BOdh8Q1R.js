import{_ as a,o as n,c as t,a2 as e}from"./chunks/framework.BM5YPQmL.js";const u=JSON.parse('{"title":"Application dictionary - Display (13)","description":"","frontmatter":{},"headers":[],"relativePath":"manual/dictionary/display.md","filePath":"manual/dictionary/display.md"}'),p={name:"manual/dictionary/display.md"};function d(i,s,l,c,o,r){return n(),t("div",null,[...s[0]||(s[0]=[e(`<h1 id="application-dictionary-display-13" tabindex="-1">Application dictionary - Display (13) <a class="header-anchor" href="#application-dictionary-display-13" aria-label="Permalink to &quot;Application dictionary - Display (13)&quot;">​</a></h1><h3 id="chinese" tabindex="-1">chinese <a class="header-anchor" href="#chinese" aria-label="Permalink to &quot;chinese&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>chinese</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>CJK console demo</td></tr><tr><td>Usage</td><td><code>chinese</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>chinese</span></span>
<span class="line"><span>chinese support status:</span></span>
<span class="line"><span>  font     : embedded-unifont-16x16</span></span>
<span class="line"><span>  coverage : 6763 hanzi + 682 symbols, 16x16, 2 bytes/row</span></span>
<span class="line"><span>  data     : 216416 + 21824 bytes embedded</span></span>
<span class="line"><span>  slots    : 1 of 4 used</span></span>
<span class="line"><span>  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 5904 | encode 0 | convert 0 | detect 0 | miss 0</span></span>
<span class="line"><span>  console encoding: utf-8</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><p>(Note: full-screen page; the quit key was not honored in this session - output as captured, session reset)</p><h3 id="chinesetest" tabindex="-1">chinesetest <a class="header-anchor" href="#chinesetest" aria-label="Permalink to &quot;chinesetest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>chinesetest</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>CJK font/encoding test battery</td></tr><tr><td>Usage</td><td><code>chinesetest</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>chinesetest</span></span>
<span class="line"><span>chinesetest: full Chinese support suite</span></span>
<span class="line"><span>[1] font data</span></span>
<span class="line"><span>  PASS status() returns 0</span></span>
<span class="line"><span>  PASS hanzi count == 6763</span></span>
<span class="line"><span>  PASS symbol count == 682</span></span>
<span class="line"><span>  PASS glyph bytes == 32 (16x16, 2 B/row)</span></span>
<span class="line"><span>  PASS hanzi size == 216416 B</span></span>
<span class="line"><span>  PASS symbol size == 21824 B</span></span>
<span class="line"><span>  PASS embedded slot loaded</span></span>
<span class="line"><span>  PASS zone 16 wei 1 -&gt; compact index 0</span></span>
<span class="line"><span>  PASS zone 87 wei 94 -&gt; compact index 6762</span></span>
<span class="line"><span>  PASS zone 55 wei 90 is a GB2312 gap slot</span></span>
<span class="line"><span>  PASS all 6763 hanzi bitmaps non-empty</span></span>
<span class="line"><span>  PASS symbol table reachable via bit15</span></span>
<span class="line"><span>[2] font lookup</span></span>
<span class="line"><span>  PASS U+4E2D found in unicode index</span></span>
<span class="line"><span>  PASS zone/wei lookup agrees with unicode lookup</span></span>
<span class="line"><span>  PASS index -&gt; unicode round-trips to U+4E2D</span></span>
<span class="line"><span>  PASS U+3002 in symbol table (bit15 set)</span></span>
<span class="line"><span>  PASS unmapped/mapped U+4E00 handled without crash</span></span>
<span class="line"><span>  PASS zone 95 rejected</span></span>
<span class="line"><span>  PASS wei 0 rejected</span></span>
<span class="line"><span>  PASS metrics 16x16 stride 2</span></span>
<span class="line"><span>[3] font management</span></span>
<span class="line"><span>  PASS embedded load idempotent</span></span>
<span class="line"><span>  PASS runtime slot allocated (&gt;=1)</span></span>
<span class="line"><span>  PASS runtime font resolves its glyph</span></span>
<span class="line"><span>  PASS runtime unload ok</span></span>
<span class="line"><span>  PASS embedded slot unload rejected</span></span>
<span class="line"><span>  PASS unknown slot rejected</span></span>
<span class="line"><span>  PASS shutdown ok</span></span>
<span class="line"><span>[4] GB2312 codec</span></span>
<span class="line"><span>  PASS decode D6D0 -&gt; U+4E2D</span></span>
<span class="line"><span>  PASS decode B0A2 -&gt; U+963F (a, &gt;= U+8000)</span></span>
<span class="line"><span>  PASS decode &#39;A&#39; -&gt; 0x41</span></span>
<span class="line"><span>  PASS lead 0x88 rejected</span></span>
<span class="line"><span>  PASS truncated pair rejected</span></span>
<span class="line"><span>  PASS encode U+4E2D -&gt; D6 D0</span></span>
<span class="line"><span>  PASS encode U+963F -&gt; B0 A2 (&gt;= U+8000)</span></span>
<span class="line"><span>  PASS encode non-GB2312 cp rejected</span></span>
<span class="line"><span>[5] UTF-8 codec</span></span>
<span class="line"><span>  PASS decode E4 B8 AD -&gt; U+4E2D</span></span>
<span class="line"><span>  PASS decode &#39;z&#39; pass-through</span></span>
<span class="line"><span>  PASS overlong C1 81 rejected</span></span>
<span class="line"><span>  PASS surrogate ED A0 80 rejected</span></span>
<span class="line"><span>  PASS truncated 3-byte rejected</span></span>
<span class="line"><span>  PASS bad continuation byte rejected</span></span>
<span class="line"><span>  PASS decode 4-byte U+1F600</span></span>
<span class="line"><span>  PASS encode U+4E2D -&gt; E4 B8 AD</span></span>
<span class="line"><span>  PASS encode surrogate rejected</span></span>
<span class="line"><span>  PASS encode &gt; U+10FFFF rejected</span></span>
<span class="line"><span>[6] encoding conversion</span></span>
<span class="line"><span>  PASS GB2312 zhongwen -&gt; UTF-8</span></span>
<span class="line"><span>  PASS UTF-8 -&gt; GB2312 round trip</span></span>
<span class="line"><span>  PASS UTF-8 -&gt; Unicode cp U+4E2D</span></span>
<span class="line"><span>[7] encoding detection</span></span>
<span class="line"><span>  PASS detect ascii</span></span>
<span class="line"><span>  PASS detect utf-8</span></span>
<span class="line"><span>  PASS detect gb2312</span></span>
<span class="line"><span>  PASS detect empty -&gt; ascii</span></span>
<span class="line"><span>[8] renderer extension</span></span>
<span class="line"><span>  PASS render_chinese_init</span></span>
<span class="line"><span>  PASS CJK width 16 px</span></span>
<span class="line"><span>  PASS ASCII width 8 px</span></span>
<span class="line"><span>  PASS CJK cols 2</span></span>
<span class="line"><span>  PASS ASCII cols 1</span></span>
<span class="line"><span>  PASS font_select finds CJK font</span></span>
<span class="line"><span>  PASS font_get_bitmap CJK</span></span>
<span class="line"><span>  PASS ascii + gb2312 fonts registered</span></span>
<span class="line"><span>  PASS active renderer keeps 8x16 cell</span></span>
<span class="line"><span>  PASS draw CJK/ASCII/missing-box without fault</span></span>
<span class="line"><span>[9] console extension</span></span>
<span class="line"><span>  PASS console_cn_init</span></span>
<span class="line"><span>  PASS default encoding utf-8</span></span>
<span class="line"><span>  PASS encoding switch</span></span>
<span class="line"><span>  PASS mixed rejected</span></span>
<span class="line"><span>  PASS cp cols CJK 2</span></span>
<span class="line"><span>  PASS display cols a[zhong]b = 4</span></span>
<span class="line"><span>ä¸­x  PASS putcw cursor geometry SKIP (output captured)</span></span>
<span class="line"><span>   PASS cn backspace ok</span></span>
<span class="line"><span>[12] font engine registry</span></span>
<span class="line"><span>  PASS font_register accepts</span></span>
<span class="line"><span>  PASS count increments</span></span>
<span class="line"><span>  PASS registered font resolves cp</span></span>
<span class="line"><span>  PASS font_unregister</span></span>
<span class="line"><span>  PASS count restored</span></span>
<span class="line"><span>  PASS unregistered cp unresolved</span></span>
<span class="line"><span>  PASS unknown name rejected</span></span>
<span class="line"><span>[10] VFS UTF-8 file</span></span>
<span class="line"><span>  PASS create /tmp/cn.txt</span></span>
<span class="line"><span>  PASS wrote 12 UTF-8 bytes</span></span>
<span class="line"><span>[13] GB2312 full-library round trip</span></span>
<span class="line"><span>  PASS 6763 hanzi zone/wei &lt;-&gt; cp round trip</span></span>
<span class="line"><span>  PASS 682 symbols zone/wei &lt;-&gt; cp round trip</span></span>
<span class="line"><span>  full trip: 6763 hanzi (2366 at &gt;= U+8000) + 682 symbols</span></span>
<span class="line"><span>chinesetest: 81/81 PASS</span></span>
<span class="line"><span>chinesetest: ALL PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><p>(Note: full-screen page; the quit key was not honored in this session - output as captured, session reset)</p><h3 id="clear" tabindex="-1">clear <a class="header-anchor" href="#clear" aria-label="Permalink to &quot;clear&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>clear</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>clear the screen</td></tr><tr><td>Usage</td><td><code>clear</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>clear</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="encoding" tabindex="-1">encoding <a class="header-anchor" href="#encoding" aria-label="Permalink to &quot;encoding&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>encoding</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>console encoding status/set</td></tr><tr><td>Usage</td><td><code>encoding</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>encoding</span></span>
<span class="line"><span>encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 93 | encode 0 | convert 0 | detect 0 | miss 0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="encoding-test" tabindex="-1">encoding-test <a class="header-anchor" href="#encoding-test" aria-label="Permalink to &quot;encoding-test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>encoding-test</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>encoding codec test</td></tr><tr><td>Usage</td><td><code>encoding-test</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>encoding-test</span></span>
<span class="line"><span>encoding-test: PASS (utf8+gb2312 spot checks)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="font" tabindex="-1">font <a class="header-anchor" href="#font" aria-label="Permalink to &quot;font&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>font</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>font engine status</td></tr><tr><td>Usage</td><td><code>font</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>font</span></span>
<span class="line"><span>registered fonts:</span></span>
<span class="line"><span>  ascii-8x16     built-in screen_font8x16, 8x16</span></span>
<span class="line"><span>  gb2312-16x16   embedded GB2312, 6763+682 glyphs</span></span>
<span class="line"><span>  total live: 2</span></span>
<span class="line"><span>  active renderer: cn-8x16-16x16</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="font-test" tabindex="-1">font-test <a class="header-anchor" href="#font-test" aria-label="Permalink to &quot;font-test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>font-test</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>font renderer test</td></tr><tr><td>Usage</td><td><code>font-test</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>font-test</span></span>
<span class="line"><span>font-test: rendering sample row (16x16 CJK):</span></span>
<span class="line"><span></span></span>
<span class="line"><span>font-test: 16px row drawn above (zhongwen OpenCube shi)</span></span>
<span class="line"><span></span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="ime" tabindex="-1">ime <a class="header-anchor" href="#ime" aria-label="Permalink to &quot;ime&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>ime</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>the pinyin IME (-i interactive)</td></tr><tr><td>Usage</td><td><code>ime [-i]</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>ime</span></span>
<span class="line"><span>pinyin IME status:</span></span>
<span class="line"><span>  syllables : 396</span></span>
<span class="line"><span>  level-1   : 3755 chars (GB2312 zones 16-55)</span></span>
<span class="line"><span>  phrases   : 622</span></span>
<span class="line"><span>  usage     : &quot;ime -i&quot; opens the interactive compose line (the SAME</span></span>
<span class="line"><span>              engine the nano/vi editors use - F2 inside an editor);</span></span>
<span class="line"><span>              type pinyin; digits 1-9 pick; space = first; PageUp/PageDown</span></span>
<span class="line"><span>              (or - / =) page; Backspace drops a letter; Enter commits the</span></span>
<span class="line"><span>  encoding: gb2312+utf8+ascii | font: embedded-unifont-16x16 | hanzi 6763 | symbols 682 | decode 3016 | encode 0 | convert 0 | detect 0 | miss 0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="ime-test" tabindex="-1">ime-test <a class="header-anchor" href="#ime-test" aria-label="Permalink to &quot;ime-test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>ime-test</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>IME engine + session battery</td></tr><tr><td>Usage</td><td><code>ime-test</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>ime-test</span></span>
<span class="line"><span>ime-test: PASS (engine ni/hao/qqq + session: phrase, compose, page, digit, space, enter, backspace, esc, passthrough)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="lang" tabindex="-1">lang <a class="header-anchor" href="#lang" aria-label="Permalink to &quot;lang&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>lang</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>show/set the console language (en/zh)</td></tr><tr><td>Usage</td><td><code>lang [en|zh]</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>lang</span></span>
<span class="line"><span>Console output language: en</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="serial-encoding" tabindex="-1">serial_encoding <a class="header-anchor" href="#serial-encoding" aria-label="Permalink to &quot;serial_encoding&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>serial_encoding</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>show/set the serial line encoding (utf8/gbk)</td></tr><tr><td>Usage</td><td><code>serial_encoding [utf8|gbk] (WP06g3</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>serial_encoding</span></span>
<span class="line"><span>serial encoding: utf8</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="serial-probe" tabindex="-1">serial-probe <a class="header-anchor" href="#serial-probe" aria-label="Permalink to &quot;serial-probe&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>serial-probe</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>serial encoding self-check wizard (pick the line that reads correctly)</td></tr><tr><td>Usage</td><td><code>serial-probe</code></td></tr><tr><td>Source</td><td><code>ocos/l1apps/serial-probe/main.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>serial-probe</span></span>
<span class="line"><span>serial-probe: pick the line that reads correctly</span></span>
<span class="line"><span>serial-probe [1] UTF-8: &lt;sample&gt;</span></span>
<span class="line"><span>serial-probe [2] GBK:   &lt;sample&gt;</span></span>
<span class="line"><span>serial-probe: enter 1 (UTF-8) or 2 (GBK)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="stats" tabindex="-1">stats <a class="header-anchor" href="#stats" aria-label="Permalink to &quot;stats&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>stats</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>display pipeline statistics</td></tr><tr><td>Usage</td><td><code>stats</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>stats</span></span>
<span class="line"><span>ticks=291  ms=2910  IRQ0=292  IRQ1=0  soft=0  one=0</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="uname" tabindex="-1">uname <a class="header-anchor" href="#uname" aria-label="Permalink to &quot;uname&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>uname</td></tr><tr><td>Category</td><td>Display</td></tr><tr><td>Function</td><td>system identification</td></tr><tr><td>Usage</td><td><code>uname [-a|-s|-r|-m]</code></td></tr><tr><td>Source</td><td><code>ocos/services/display/src/svc_display_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>uname</span></span>
<span class="line"><span>Open Cube OS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,59)])])}const g=a(p,[["render",d]]);export{u as __pageData,g as default};
