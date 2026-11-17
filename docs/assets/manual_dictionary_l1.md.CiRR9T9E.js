import{_ as a,o as n,c as e,a2 as t}from"./chunks/framework.BM5YPQmL.js";const _=JSON.parse('{"title":"Application dictionary - L1 runtime (2)","description":"","frontmatter":{},"headers":[],"relativePath":"manual/dictionary/l1.md","filePath":"manual/dictionary/l1.md"}'),p={name:"manual/dictionary/l1.md"};function l(d,s,o,i,r,c){return n(),e("div",null,[...s[0]||(s[0]=[t(`<h1 id="application-dictionary-l1-runtime-2" tabindex="-1">Application dictionary - L1 runtime (2) <a class="header-anchor" href="#application-dictionary-l1-runtime-2" aria-label="Permalink to &quot;Application dictionary - L1 runtime (2)&quot;">​</a></h1><h3 id="l1test" tabindex="-1">l1test <a class="header-anchor" href="#l1test" aria-label="Permalink to &quot;l1test&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>l1test</td></tr><tr><td>Category</td><td>L1 runtime</td></tr><tr><td>Function</td><td>L1 job/IPC/dynamic-link self-test</td></tr><tr><td>Usage</td><td><code>l1test (job + wp8b + CPL0 smoke</code></td></tr><tr><td>Hidden parameters</td><td>hidden parameter, not a public interface; compatibility is not guaranteed: <code>hello</code>, <code>loop</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/l1/src/svc_l1_core.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>ixgbe</span></span>
<span class="line"><span>ixgbe: not present</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="ldd" tabindex="-1">ldd <a class="header-anchor" href="#ldd" aria-label="Permalink to &quot;ldd&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>ldd</td></tr><tr><td>Category</td><td>L1 runtime</td></tr><tr><td>Function</td><td>dynamic dependency lister</td></tr><tr><td>Usage</td><td><code>ldd &lt;program&gt;</code></td></tr><tr><td>Parameters</td><td><code>program</code></td></tr><tr><td>Return codes</td><td>0, 1</td></tr><tr><td>Source</td><td><code>ocos/services/l1/src/svc_l1_core.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>l1test</span></span>
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
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,9)])])}const u=a(p,[["render",l]]);export{_ as __pageData,u as default};
