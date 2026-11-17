import{_ as a,o as n,c as t,a2 as e}from"./chunks/framework.BM5YPQmL.js";const _=JSON.parse('{"title":"应用字典 - L1 运行时 (2)","description":"","frontmatter":{},"headers":[],"relativePath":"zh/manual/dictionary/l1.md","filePath":"zh/manual/dictionary/l1.md"}'),p={name:"zh/manual/dictionary/l1.md"};function l(d,s,o,r,c,i){return n(),t("div",null,[...s[0]||(s[0]=[e(`<h1 id="应用字典-l1-运行时-2" tabindex="-1">应用字典 - L1 运行时 (2) <a class="header-anchor" href="#应用字典-l1-运行时-2" aria-label="Permalink to &quot;应用字典 - L1 运行时 (2)&quot;">​</a></h1><h3 id="l1test" tabindex="-1">l1test <a class="header-anchor" href="#l1test" aria-label="Permalink to &quot;l1test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>l1test</td></tr><tr><td>分类</td><td>L1 运行时</td></tr><tr><td>功能</td><td>L1 作业/IPC/动态链接自检</td></tr><tr><td>用法</td><td><code>l1test (job + wp8b + CPL0 smoke</code></td></tr><tr><td>隐藏参数</td><td>隐藏参数，非公开接口，兼容性不保证: <code>hello</code>, <code>loop</code></td></tr><tr><td>返回码</td><td>0, 1</td></tr><tr><td>来源</td><td><code>ocos/services/l1/src/svc_l1_core.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>ixgbe</span></span>
<span class="line"><span>ixgbe: not present</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="ldd" tabindex="-1">ldd <a class="header-anchor" href="#ldd" aria-label="Permalink to &quot;ldd&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>ldd</td></tr><tr><td>分类</td><td>L1 运行时</td></tr><tr><td>功能</td><td>动态依赖列表</td></tr><tr><td>用法</td><td><code>ldd &lt;program&gt;</code></td></tr><tr><td>参数</td><td><code>program</code></td></tr><tr><td>返回码</td><td>0, 1</td></tr><tr><td>来源</td><td><code>ocos/services/l1/src/svc_l1_core.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>l1test</span></span>
<span class="line"><span>loop: running</span></span>
<span class="line"><span>hello from userspace  ext_wp8b: elf_get_needed(so_test) = 1 (libfoo.so) - PASS</span></span>
<span class="line"><span>  ext_wp8b: elf_get_needed(hello) = 0 (static, correct) - PASS</span></span>
<span class="line"><span>  ext_wp8b: so_unload(0) = -1 (NULL handle rejected) - PASS</span></span>
<span class="line"><span>  ext_wp8b: ldso_run = 0 (kernel context, correct) - PASS</span></span>
<span class="line"><span>  ext_wp8b: elf_get_needed(truncated 48B) = 0 (rejected) - PASS</span></span>
<span class="line"><span>  ext_wp8b: elf_get_needed(vaddr!=off) = 1 (libx.so) - PASS</span></span>
<span class="line"><span>  ext_wp8b: elf_get_needed(wild strtab) = 0 (rejected) - PASS</span></span>
<span class="line"><span>  ext_wp8b: so_bss_zero boundary page (0xA7 tail -&gt; 0) - PASS</span></span>
<span class="line"><span>  ext_wp8b: symbol_resolve(libfoo, foo_add) - PASS</span></span>
<span class="line"><span>  ext_wp8b: symbol_resolve(libfoo, foo_global) - PASS</span></span>
<span class="line"><span>  ext_wp8b: symbol_resolve(wild symtab) = 0 (rejected) - PASS</span></span>
<span class="line"><span>  ext_wp8b: reloc GLOB_DAT cross-lib resolve - PASS</span></span>
<span class="line"><span>  ext_wp8b: reloc RELATIVE base+addend - PASS</span></span>
<span class="line"><span>  ext_wp8b: reloc hostile sym_idx bounded - PASS</span></span>
<span class="line"><span>  ext_wp8b: 14/14 self-tests passed</span></span>
<span class="line"><span>L1 job interface test:</span></span>
<span class="line"><span>  step 1: job_create(&quot;loop&quot;) (real background spawn)</span></span>
<span class="line"><span>  job_create -&gt; job_id=0 spawned loop pid=76 - PASS</span></span>
<span class="line"><span>  step 3: job_list()</span></span>
<span class="line"><span>  job_list: 1 active job:</span></span>
<span class="line"><span>[0] pid=76 loop</span></span>
<span class="line"><span>  step 4: job_control(job, bg=1)</span></span>
<span class="line"><span>  job_control(bg) = 0 - PASS</span></span>
<span class="line"><span>  step 5: job_create(&quot;hello&quot;) + job_control(fg) waits for exit</span></span>
<span class="line"><span>  fg returned after target exit (pid=77 alive=0, job retired) - PASS</span></span>
<span class="line"><span>  job_list after fg: 1 active (loop) - PASS</span></span>
<span class="line"><span>  step 6: job_control(job, kill=2)</span></span>
<span class="line"><span>  kill (process reaped) + job_list=0 active - PASS</span></span>
<span class="line"><span>  step 7: ext_wp8b_selftest()</span></span>
<span class="line"><span>  ext_wp8b selftest 14/14 - PASS</span></span>
<span class="line"><span>  L1 job interface test: PASS</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,9)])])}const h=a(p,[["render",l]]);export{_ as __pageData,h as default};
