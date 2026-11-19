import{_ as s,o as n,c as t,a2 as p}from"./chunks/framework.BM5YPQmL.js";const b=JSON.parse('{"title":"Application dictionary - Cryptography (2)","description":"","frontmatter":{},"headers":[],"relativePath":"manual/dictionary/crypto.md","filePath":"manual/dictionary/crypto.md"}'),e={name:"manual/dictionary/crypto.md"};function c(d,a,l,o,i,r){return n(),t("div",null,[...a[0]||(a[0]=[p(`<h1 id="application-dictionary-cryptography-2" tabindex="-1">Application dictionary - Cryptography (2) <a class="header-anchor" href="#application-dictionary-cryptography-2" aria-label="Permalink to &quot;Application dictionary - Cryptography (2)&quot;">​</a></h1><h3 id="cryptotest" tabindex="-1">cryptotest <a class="header-anchor" href="#cryptotest" aria-label="Permalink to &quot;cryptotest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>cryptotest</td></tr><tr><td>Category</td><td>Cryptography</td></tr><tr><td>Function</td><td>crypto primitives test</td></tr><tr><td>Usage</td><td><code>cryptotest</code></td></tr><tr><td>Source</td><td><code>ocos/services/crypto/src/svc_crypto_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>cryptotest</span></span>
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
<span class="line"><span>ush&gt;</span></span></code></pre></div><h3 id="dhtest" tabindex="-1">dhtest <a class="header-anchor" href="#dhtest" aria-label="Permalink to &quot;dhtest&quot;">​</a></h3><table tabindex="0"><thead><tr><th>Field</th><th>Value</th></tr></thead><tbody><tr><td>Name</td><td>dhtest</td></tr><tr><td>Category</td><td>Cryptography</td></tr><tr><td>Function</td><td>Diffie-Hellman test</td></tr><tr><td>Usage</td><td><code>dhtest</code></td></tr><tr><td>Source</td><td><code>ocos/services/crypto/src/svc_crypto_l1.c</code></td></tr></tbody></table><p><strong>Executed example</strong>:</p><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>dhtest</span></span>
<span class="line"><span>DH modexp self-test (Oakley Group 1, 1024-bit):</span></span>
<span class="line"><span>  g^0 mod p = 00000001 (expect ...0001)  PASS</span></span>
<span class="line"><span>  1^x mod p = 00000001 (expect ...0001)  PASS</span></span>
<span class="line"><span>  Computing g^x mod p (timing)...</span></span>
<span class="line"><span>  g^x mod p = b0bcb6577676a87392260e4cbb295ab9...</span></span>
<span class="line"><span>  Time: 4090 ms</span></span>
<span class="line"><span>  premaster (first 16): 170e36c20580419bba5eddf270b2432e</span></span>
<span class="line"><span>  TLS KEX (2 modexp) time: 9220 ms (premaster non-zero)</span></span>
<span class="line"><span>DH group14 (2048-bit) fixed-vector truth tests:</span></span>
<span class="line"><span>  g^x mod p14 = e  -&gt; PASS  (got 32a09a91..., expect 32A09A91...)</span></span>
<span class="line"><span>  determinism (3 runs): IDENTICAL</span></span>
<span class="line"><span>  f^x mod p14 = K2 -&gt; PASS  (got 5962870f..., expect 5962870F...)</span></span>
<span class="line"><span>  2048-bit modexp time: 13646 / 0 ms</span></span>
<span class="line"><span>DH modexp scale sweep (truth vectors, python3 pow()):</span></span>
<span class="line"><span>  len=8: PASS  got 1f8c25fe... expect 1f8c25fe...</span></span>
<span class="line"><span>  len=16: PASS  got 5ab910ce... expect 5ab910ce...</span></span>
<span class="line"><span>  len=32: PASS  got 4a2f13c1... expect 4a2f13c1...</span></span>
<span class="line"><span>  len=64: PASS  got 10ad3f1b... expect 10ad3f1b...</span></span>
<span class="line"><span>  len=128: PASS  got 3ca32c36... expect 3ca32c36...</span></span>
<span class="line"><span>  len=256: PASS  got a7f3a565... expect a7f3a565...</span></span>
<span class="line"><span>  5/5 correctness tests passed</span></span>
<span class="line"><span>ush&gt;</span></span></code></pre></div>`,9)])])}const y=s(e,[["render",c]]);export{b as __pageData,y as default};
