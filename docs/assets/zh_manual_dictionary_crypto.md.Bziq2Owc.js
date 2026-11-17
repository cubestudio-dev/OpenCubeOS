import{_ as a,o as n,c as t,a2 as p}from"./chunks/framework.BM5YPQmL.js";const b=JSON.parse('{"title":"应用字典 - 密码学 (2)","description":"","frontmatter":{},"headers":[],"relativePath":"zh/manual/dictionary/crypto.md","filePath":"zh/manual/dictionary/crypto.md"}'),e={name:"zh/manual/dictionary/crypto.md"};function c(d,s,l,o,r,i){return n(),t("div",null,[...s[0]||(s[0]=[p(`<h1 id="应用字典-密码学-2" tabindex="-1">应用字典 - 密码学 (2) <a class="header-anchor" href="#应用字典-密码学-2" aria-label="Permalink to &quot;应用字典 - 密码学 (2)&quot;">​</a></h1><h3 id="cryptotest" tabindex="-1">cryptotest <a class="header-anchor" href="#cryptotest" aria-label="Permalink to &quot;cryptotest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>cryptotest</td></tr><tr><td>分类</td><td>密码学</td></tr><tr><td>功能</td><td>密码学原语测试</td></tr><tr><td>用法</td><td><code>cryptotest</code></td></tr><tr><td>来源</td><td><code>ocos/services/crypto/src/svc_crypto_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>cryptotest</span></span>
<span class="line"><span>Crypto self-test (NIST vectors):</span></span>
<span class="line"><span>  SHA-256(&quot;abc&quot;) = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad</span></span>
<span class="line"><span>    SHA-256: PASS</span></span>
<span class="line"><span>  AES-128(plain) = 69c4e0d86a7b0430d8cdb78070b4c55a</span></span>
<span class="line"><span>    AES-128: PASS</span></span>
<span class="line"><span>  HMAC-SHA256 = b0344c61d8db3853...</span></span>
<span class="line"><span>    HMAC-SHA256: PASS</span></span>
<span class="line"><span>  HMAC-SHA384 = afd03944d84895626b0825f4ab46907f...</span></span>
<span class="line"><span>    HMAC-SHA384: PASS</span></span>
<span class="line"><span>  4/4 tests passed</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="dhtest" tabindex="-1">dhtest <a class="header-anchor" href="#dhtest" aria-label="Permalink to &quot;dhtest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>字段</th><th>值</th></tr></thead><tbody><tr><td>名称</td><td>dhtest</td></tr><tr><td>分类</td><td>密码学</td></tr><tr><td>功能</td><td>迪菲-赫尔曼测试</td></tr><tr><td>用法</td><td><code>dhtest</code></td></tr><tr><td>来源</td><td><code>ocos/services/crypto/src/svc_crypto_l1.c</code></td></tr></tbody></table><p><strong>实际执行示例</strong>：</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>dhtest</span></span>
<span class="line"><span>DH modexp self-test (Oakley Group 1, 1024-bit):</span></span>
<span class="line"><span>  g^0 mod p = 00000001 (expect ...0001)  PASS</span></span>
<span class="line"><span>  1^x mod p = 00000001 (expect ...0001)  PASS</span></span>
<span class="line"><span>  Computing g^x mod p (timing)...</span></span>
<span class="line"><span>  g^x mod p = 302acf9630dfa1974816159842048fea...</span></span>
<span class="line"><span>  Time: 4140 ms</span></span>
<span class="line"><span>  premaster (first 16): fb31b9092534f649c19ee64f813b22ca</span></span>
<span class="line"><span>  TLS KEX (2 modexp) time: 9180 ms (premaster non-zero)</span></span>
<span class="line"><span>DH group14 (2048-bit) fixed-vector truth tests:</span></span>
<span class="line"><span>  g^x mod p14 = e  -&gt; PASS  (got 32a09a91..., expect 32A09A91...)</span></span>
<span class="line"><span>  determinism (3 runs): IDENTICAL</span></span>
<span class="line"><span>  f^x mod p14 = K2 -&gt; PASS  (got 5962870f..., expect 5962870F...)</span></span>
<span class="line"><span>  2048-bit modexp time: 13683 / 0 ms</span></span>
<span class="line"><span>DH modexp scale sweep (truth vectors, python3 pow()):</span></span>
<span class="line"><span>  len=8: PASS  got 1f8c25fe... expect 1f8c25fe...</span></span>
<span class="line"><span>  len=16: PASS  got 5ab910ce... expect 5ab910ce...</span></span>
<span class="line"><span>  len=32: PASS  got 4a2f13c1... expect 4a2f13c1...</span></span>
<span class="line"><span>  len=64: PASS  got 10ad3f1b... expect 10ad3f1b...</span></span>
<span class="line"><span>  len=128: PASS  got 3ca32c36... expect 3ca32c36...</span></span>
<span class="line"><span>  len=256: PASS  got a7f3a565... expect a7f3a565...</span></span>
<span class="line"><span>  5/5 correctness tests passed</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,9)])])}const S=a(e,[["render",c]]);export{b as __pageData,S as default};
