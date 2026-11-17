import{_ as a,o as t,c as n,a2 as e}from"./chunks/framework.BM5YPQmL.js";const g=JSON.parse('{"title":"应用字典 - 内核时代测试程序（散装源码） (33)","description":"","frontmatter":{},"headers":[],"relativePath":"zh/manual/dictionary/flat.md","filePath":"zh/manual/dictionary/flat.md"}'),p={name:"zh/manual/dictionary/flat.md"};function l(d,s,i,r,o,c){return t(),n("div",null,[...s[0]||(s[0]=[e(`<h1 id="应用字典-内核时代测试程序-散装源码-33" tabindex="-1">应用字典 - 内核时代测试程序（散装源码） (33) <a class="header-anchor" href="#应用字典-内核时代测试程序-散装源码-33" aria-label="Permalink to &quot;应用字典 - 内核时代测试程序（散装源码） (33)&quot;">​</a></h1><h3 id="badapp" tabindex="-1">badapp <a class="header-anchor" href="#badapp" aria-label="Permalink to &quot;badapp&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>badapp</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>badapp</span></span>
<span class="line"><span>badapp: trying kernel access</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000005  rip=0x000000000040001D  cs=0x0000000000000033  rflags=0x0000000000010202</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x00000000FD000000  (faulting address)</span></span>
<span class="line"><span>*** KILLING user process (pid=18) due to exception 14, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=14), kernel continues</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="dlsym-test" tabindex="-1">dlsym_test <a class="header-anchor" href="#dlsym-test" aria-label="Permalink to &quot;dlsym_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>dlsym_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>SPDX-License-Identifier: Apache-2</td></tr><tr><td>返回码</td><td>1</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>dlsym_test</span></span>
<span class="line"><span>dlsym_test: foo_add(7,8)=15</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="dyn-hello" tabindex="-1">dyn_hello <a class="header-anchor" href="#dyn-hello" aria-label="Permalink to &quot;dyn_hello&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>dyn_hello</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>SPDX-License-Identifier: Apache-2</td></tr><tr><td>返回码</td><td>1</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>dyn_hello</span></span>
<span class="line"><span>hello from dynamic program</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="exec-test" tabindex="-1">exec_test <a class="header-anchor" href="#exec-test" aria-label="Permalink to &quot;exec_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>exec_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-08a exec test: fork, child execs hello, parent waits</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>exec_test</span></span>
<span class="line"><span>exec_test: start</span></span>
<span class="line"><span>exec_test: parent waiting</span></span>
<span class="line"><span>exexec_test: child execing hellohello from userspaceexec_test: PASS</span></span>
<span class="line"><span>eush&gt;</span></span></code></pre></div><p>(备注: fork/exec 竞态的原样串口转录：子进程的 exec 交错于父进程的输出之间、提示符紧贴 PASS 行输出——与发货 ISO 串口实际捕获逐字一致)</p><h3 id="fdref-test" tabindex="-1">fdref_test <a class="header-anchor" href="#fdref-test" aria-label="Permalink to &quot;fdref_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>fdref_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>fdref_test</span></span>
<span class="line"><span>FDREF PASS</span></span>
<span class="line"><span>[user] process 7 exited (code=0)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="fork-test" tabindex="-1">fork_test <a class="header-anchor" href="#fork-test" aria-label="Permalink to &quot;fork_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>fork_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-08a fork test: TRUE POSIX fork</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>fork_test</span></span>
<span class="line"><span>fork_test: start</span></span>
<span class="line"><span>parent: pid=17 ppid=1 child=18child: pid=18 ppid=17fork_test: PASS</span></span>
<span class="line"><span> ush&gt;</span></span></code></pre></div><h3 id="g6-exec-a" tabindex="-1">g6_exec_a <a class="header-anchor" href="#g6-exec-a" aria-label="Permalink to &quot;g6_exec_a&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>g6_exec_a</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>g6_exec_a</span></span>
<span class="line"><span>g6_exec_a: start</span></span>
<span class="line"><span>g6_exec_a: child execing g6_exec_b</span></span>
<span class="line"><span>g6_exec_b: alive, raising SIGUSR1 with no handler</span></span>
<span class="line"><span>g6_exec_a: child status = 138g6_exec_a: PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="g6-exec-b" tabindex="-1">g6_exec_b <a class="header-anchor" href="#g6-exec-b" aria-label="Permalink to &quot;g6_exec_b&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>g6_exec_b</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>g6_exec_b</span></span>
<span class="line"><span>g6_exec_b: alive, raising SIGUSR1 with no handler</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="g6-oom" tabindex="-1">g6_oom <a class="header-anchor" href="#g6-oom" aria-label="Permalink to &quot;g6_oom&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>g6_oom</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-10-AUDIT_P2-fix3 G6 regression (BUG-0279b): fork under address-space</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>g6_oom</span></span>
<span class="line"><span>g6_oom: start</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000007  rip=0x00000000004000D4  cs=0x0000000000000033  rflags=0x0000000000010246</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x000000003C000000  (faulting address)</span></span>
<span class="line"><span>*** KILLING user process (pid=35) due to exception 14, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=14), kernel continues</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="g6-test" tabindex="-1">g6_test <a class="header-anchor" href="#g6-test" aria-label="Permalink to &quot;g6_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>g6_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-10-AUDIT_P2-fix3 G6 regression test (BUG-0274/0275/0276/0277/0278/0280).</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>g6_test</span></span>
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
<span class="line"><span>[user] process 41 exited (code=42)</span></span>
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
<span class="line"><span>g6_test: T6 dlopen count = 3839g6_test: =PASS=</span></span>
<span class="line"><span>g6_test: failed tests = 0g6_test: PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="g7mmap" tabindex="-1">g7mmap <a class="header-anchor" href="#g7mmap" aria-label="Permalink to &quot;g7mmap&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>g7mmap</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-10-AUDIT_P2-fix3 G7 mmap regression (BUG-0281 / A2-9).</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>g7mmap</span></span>
<span class="line"><span>G7MMAP: start</span></span>
<span class="line"><span>G7MMAP PASS T1 (bump RW map + round-trip)</span></span>
<span class="line"><span>G7MMAP PASS T2 (addr hint honoured)</span></span>
<span class="line"><span>G7MMAP PASS T3 (kernel-addr hint rejected)</span></span>
<span class="line"><span>G7MMAP PASS T4 (prot 0x8 rejected)</span></span>
<span class="line"><span>G7MMAP PASS T5 (over-window length rejected)</span></span>
<span class="line"><span>G7MMAP PASS T6 (munmap kernel addr rejected)</span></span>
<span class="line"><span>G7MMAP PASS T7 (munmap legal range)</span></span>
<span class="line"><span>G7MMAP PASS T8 (mprotect RW toggle + write)</span></span>
<span class="line"><span>G7MMAP PASS T9 (mprotect prot 0x9 rejected)</span></span>
<span class="line"><span>G7MMAP PASS T10 (munmap beyond window rejected)</span></span>
<span class="line"><span>G7MMAP T11: PF probe armed (write to R-only page)</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000007  rip=0x000000000040031C  cs=0x0000000000000033  rflags=0x0000000000010206</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x000000003C012000  (faulting address)</span></span>
<span class="line"><span>*** KILLING user process (pid=47) due to exception 14, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=14), kernel continues</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="hello" tabindex="-1">hello <a class="header-anchor" href="#hello" aria-label="Permalink to &quot;hello&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>hello</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>Open Cube OS WP-08a - hello world</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>hello</span></span>
<span class="line"><span>hello from userspaceush&gt;</span></span></code></pre></div><h3 id="int3-user" tabindex="-1">int3_user <a class="header-anchor" href="#int3-user" aria-label="Permalink to &quot;int3_user&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>int3_user</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>int3_user</span></span>
<span class="line"><span>int3_user: about to int3</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 3: #BP Breakpoint ***</span></span>
<span class="line"><span>  error_code=0x0000000000000000  rip=0x0000000000400019  cs=0x0000000000000033  rflags=0x0000000000010202</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>*** KILLING user process (pid=97) due to exception 3, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=3), kernel continues</span></span>
<span class="line"><span>*** IST-exit: vector=3 exit-stack rsp=0x00000000003E1FD0 [off-IST2 OK]</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="ld-so" tabindex="-1">ld_so <a class="header-anchor" href="#ld-so" aria-label="Permalink to &quot;ld_so&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>ld_so</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>SPDX-License-Identifier: Apache-2</td></tr><tr><td>返回码</td><td>-1, 0, 1</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>ld_so</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000005  rip=0x000000001000192D  cs=0x0000000000000033  rflags=0x0000000000010212</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEA8  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x0000000008000018  (faulting address)</span></span>
<span class="line"><span>*** KILLING user process (pid=119) due to exception 14, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=14), kernel continues</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="loop" tabindex="-1">loop <a class="header-anchor" href="#loop" aria-label="Permalink to &quot;loop&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>loop</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>run loop</span></span>
<span class="line"><span>started pid=3</span></span>
<span class="line"><span>loop: running</span></span></code></pre></div><p>(备注: run loop (async) + kill - the by-design spinner)</p><h3 id="main-dyn" tabindex="-1">main_dyn <a class="header-anchor" href="#main-dyn" aria-label="Permalink to &quot;main_dyn&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>main_dyn</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>SPDX-License-Identifier: Apache-2</td></tr><tr><td>返回码</td><td>1</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>main_dyn</span></span>
<span class="line"><span>main: foo_add(2,3)=5</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="mmap-multi" tabindex="-1">mmap_multi <a class="header-anchor" href="#mmap-multi" aria-label="Permalink to &quot;mmap_multi&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>mmap_multi</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>mmap_multi</span></span>
<span class="line"><span>parent: mmap=1006632960child: mmap=1006637056mmap_multi: BAD child aparent: readback=PAAAmmap_multi: PASS</span></span>
<span class="line"><span>mmush&gt;</span></span></code></pre></div><h3 id="mmap-test" tabindex="-1">mmap_test <a class="header-anchor" href="#mmap-test" aria-label="Permalink to &quot;mmap_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>mmap_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-08a mmap test</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>mmap_test</span></span>
<span class="line"><span>mmap_test: startmmap_test: wrote to mmaMMAP_OK!</span></span>
<span class="line"><span>mmap_test: mprotect done</span></span>
<span class="line"><span></span></span>
<span class="line"><span>mmap_test: PASSush&gt;</span></span></code></pre></div><h3 id="mprotect-test" tabindex="-1">mprotect_test <a class="header-anchor" href="#mprotect-test" aria-label="Permalink to &quot;mprotect_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>mprotect_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>mprotect_test</span></span>
<span class="line"><span>MPROTECT_KERNEL_REJECTED</span></span>
<span class="line"><span>MMPROTECT_USER_OKPASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="p0leak" tabindex="-1">p0leak <a class="header-anchor" href="#p0leak" aria-label="Permalink to &quot;p0leak&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>p0leak</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>p0leak</span></span>
<span class="line"><span>p0leak: BUG-AUD-2a-001 user-pointer fence test</span></span>
<span class="line"><span>p0leak: brk grown to 0x700000 (2MiB split forced)</span></span>
<span class="line"><span>p0leak: kernel-image write REJECTED (-1) OK</span></span>
<span class="line"><span>p0leak: split-sibling write REJECTED (-1) OK</span></span>
<span class="line"><span>p0leak: kernel-addr read REJECTED (-1) OK</span></span>
<span class="line"><span>p0leak: PASS - all kernel-address probes fenced</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="p3-test" tabindex="-1">p3_test <a class="header-anchor" href="#p3-test" aria-label="Permalink to &quot;p3_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>p3_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-10-AUDIT_P2-fix3 G7 (BUG-0281): prot is now honoured, so the</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>p3_test</span></span>
<span class="line"><span>p3_test: startp3_test: PASS_P3123</span></span>
<span class="line"><span>push&gt;</span></span></code></pre></div><h3 id="pf-test" tabindex="-1">pf_test <a class="header-anchor" href="#pf-test" aria-label="Permalink to &quot;pf_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>pf_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pf_test</span></span>
<span class="line"><span>PF-A LEAKED (P=1 fault grew stack: data lost + frames leaked)</span></span>
<span class="line"><span>[user] process 11 exited (code=0)</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000007  rip=0x00000000004000A7  cs=0x0000000000000033  rflags=0x0000000000010202</span></span>
<span class="line"><span>  rsp=0x000000003FFEFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x000000003FFEFEE0  (faulting address)</span></span>
<span class="line"><span>*** KILLING user process (pid=12) due to exception 14, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=14), kernel continues</span></span>
<span class="line"><span>PF-T DONE</span></span>
<span class="line"><span>[user] process 10 exited (code=0)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="pie-test" tabindex="-1">pie_test <a class="header-anchor" href="#pie-test" aria-label="Permalink to &quot;pie_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>pie_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>SPDX-License-Identifier: Apache-2</td></tr><tr><td>返回码</td><td>1</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pie_test</span></span>
<span class="line"><span>pie_test: loaded at 0x20000000</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="pipe-test" tabindex="-1">pipe_test <a class="header-anchor" href="#pipe-test" aria-label="Permalink to &quot;pipe_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>pipe_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-08a pipe test: fork, child writes pipe, parent reads</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>pipe_test</span></span>
<span class="line"><span>pipe_test: startpipe_test: parent readinpipe_test: got &#39;HHELLO from child&#39;</span></span>
<span class="line"><span>ppipe_test: PASSush&gt;</span></span></code></pre></div><h3 id="priv-test" tabindex="-1">priv_test <a class="header-anchor" href="#priv-test" aria-label="Permalink to &quot;priv_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>priv_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>priv_test</span></span>
<span class="line"><span>priv_test: attempting out 0x70 from ring 3</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 13: #GP General Protection ***</span></span>
<span class="line"><span>  error_code=0x0000000000000000  rip=0x000000000040001A  cs=0x0000000000000033  rflags=0x0000000000010202</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>*** KILLING user process (pid=2) due to exception 13, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=13), kernel continues</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="reloc-test" tabindex="-1">reloc_test <a class="header-anchor" href="#reloc-test" aria-label="Permalink to &quot;reloc_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>reloc_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>SPDX-License-Identifier: Apache-2</td></tr><tr><td>返回码</td><td>1</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>reloc_test</span></span>
<span class="line"><span>reloc_test: foo_add=30 foo_global=42 fp=300 msg=r</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="select-test" tabindex="-1">select_test <a class="header-anchor" href="#select-test" aria-label="Permalink to &quot;select_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>select_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-08a select test: fork, child writes pipe, parent selects</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>select_test</span></span>
<span class="line"><span>select_test: startselect_test: parent selectingselect_test: pipe ready (PASS)</span></span>
<span class="line"><span>selselect_test: PASSush&gt;</span></span></code></pre></div><h3 id="select-zero-test" tabindex="-1">select_zero_test <a class="header-anchor" href="#select-zero-test" aria-label="Permalink to &quot;select_zero_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>select_zero_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>select_zero_test</span></span>
<span class="line"><span>SZ1</span></span>
<span class="line"><span>S1 select(fd0,timeout=0)...</span></span>
<span class="line"><span>S1B returned (rax in r15)</span></span>
<span class="line"><span>S2B select(pipe,timeout=0) returned 0</span></span>
<span class="line"><span>SZ-PASS</span></span>
<span class="line"><span>[user] process 32 exited (code=0)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="signal-test" tabindex="-1">signal_test <a class="header-anchor" href="#signal-test" aria-label="Permalink to &quot;signal_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>signal_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-08a signal test: fork, child sends SIGUSR1 to parent</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>signal_test</span></span>
<span class="line"><span>signal_test: startsignal_test: handler registered</span></span>
<span class="line"><span>signal_test: parent waiting</span></span>
<span class="line"><span>ssignal_test: child sending SIGUSR1</span></span>
<span class="line"><span>signal_test: SIGNAL CAUGHT</span></span>
<span class="line"><span>ssignal_test: PASSush&gt;</span></span></code></pre></div><h3 id="sigsegv-test" tabindex="-1">sigsegv_test <a class="header-anchor" href="#sigsegv-test" aria-label="Permalink to &quot;sigsegv_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>sigsegv_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-11-crash_recovery 4.5: SIGSEGV-on-fault test.</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>sigsegv_test</span></span>
<span class="line"><span>sigsegv_test: SIGSEGV handler armed</span></span>
<span class="line"><span>sigsegv_test: touching 0xFD000000 (kernel page)...</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000005  rip=0x000000000040004D  cs=0x0000000000000033  rflags=0x0000000000010202</span></span>
<span class="line"><span>  rsp=0x000000003FFFFEE0  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x00000000FD000000  (faulting address)</span></span>
<span class="line"><span>*** SIGSEGV delivered to pid=36 (handler will run, delivery #1) - kernel continues ***</span></span>
<span class="line"><span>SIGSEGV-TEST CAUGHT (signal 11 delivered, exiting cleanly)</span></span>
<span class="line"><span>[user] process 36 exited (code=0)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="so-test" tabindex="-1">so_test <a class="header-anchor" href="#so-test" aria-label="Permalink to &quot;so_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>so_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>SPDX-License-Identifier: Apache-2</td></tr><tr><td>返回码</td><td>1</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>so_test</span></span>
<span class="line"><span>main: foo_add(2,3)=5</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="sse-test" tabindex="-1">sse_test <a class="header-anchor" href="#sse-test" aria-label="Permalink to &quot;sse_test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>sse_test</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>sse_test</span></span>
<span class="line"><span>SSE parent OK</span></span>
<span class="line"><span>SSE child  OK</span></span>
<span class="line"><span>[user] process 9 exited (code=0)</span></span>
<span class="line"><span>[user] process 8 exited (code=0)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="stackovf" tabindex="-1">stackovf <a class="header-anchor" href="#stackovf" aria-label="Permalink to &quot;stackovf&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>stackovf</td></tr><tr><td>分类</td><td>内核时代测试程序（散装源码）</td></tr><tr><td>功能</td><td>WP-11-crash_recovery 4.4: ring-3 stack-overflow interception test.</td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>stackovf</span></span>
<span class="line"><span>stackovf: fork+recursion (4 KiB frames, no handler)</span></span>
<span class="line"><span>stackovf: parent waiting for child</span></span>
<span class="line"><span>stackovf: child recursing - expect kill at the boundary</span></span>
<span class="line"><span></span></span>
<span class="line"><span>*** EXCEPTION 14: #PF Page Fault ***</span></span>
<span class="line"><span>  error_code=0x0000000000000007  rip=0x000000000040001A  cs=0x0000000000000033  rflags=0x0000000000010206</span></span>
<span class="line"><span>  rsp=0x000000003FFEFE60  ss=0x000000000000003B</span></span>
<span class="line"><span>  cr2=0x000000003FFEFE58  (faulting address)</span></span>
<span class="line"><span>*** KILLING user process (pid=3) due to exception 14, kernel continues ***</span></span>
<span class="line"><span>[user] process killed by exception (vector=14), kernel continues</span></span>
<span class="line"><span>STACKOVF-TEST DONE (child killed, kernel alive)</span></span>
<span class="line"><span>[user] process 2 exited (code=0)</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,135)])])}const b=a(p,[["render",l]]);export{g as __pageData,b as default};
