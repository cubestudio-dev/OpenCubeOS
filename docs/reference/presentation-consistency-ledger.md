<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- GENERATED BASIS: tools/audit/build_fact_source.sh (fact source) +
     tools/audit/scan_presentation.sh (presentation scan).  The ledger
     rows below are machine-reconciled by tools/audit/verify_presentation.sh:
     every row carries its fact key, the claim surface and the status. -->
# Presentation consistency ledger (WP-presentation-consistency-audit)

The single authority is the code enumeration
(`tools/audit/build_fact_source.sh`).  Every row: what the fact is,
where it appears, what it said before, what it says now, and the
status.  Machine reconciliation: `tools/audit/verify_presentation.sh`
re-derives the fact column from the tree and checks the fixed
surfaces; the EXEMPT rows carry their reason and stay human-audited.

## A. Numbers

| # | Fact (key) | Value | Surfaces fixed | Before | After | Status |
|---|------------|-------|----------------|--------|-------|--------|
| A1 | commands_total | 311 | README(+zh mirror), ARCHITECTURE(+mirror), FAQ(+mirror), MANIFEST(+mirror), website/index.html, i18n.js, site-data.js stats, docs/index.md(+zh), function-dictionary(+zh), ush-reference(+zh), about.html, download.html | 296 / 285 / 251 | 311 | FIXED (verified) |
| A2 | ring3 programs | 300 | same as A1 + tree-map(+zh) | 284 / 274 / 275 | 300 | FIXED (verified) |
| A3 | ush built-ins | 11 (registry caliber; help -a shows 12 rows - help dual-homes) | README, ARCHITECTURE, i18n.js, docs/index.md(+zh) | 12 / 19 | 11 (12-row caliber noted in ush-reference) | FIXED (verified) |
| A4 | services | 20 | INTERFACES.md tables, architecture.html table, index.md | 17 / 24 | 20 | FIXED (verified) |
| A5 | l1svc_ops | 221 | website (meta, stat card, feature), architecture.html (per-service table + caliber), INTERFACES.md (per-service tables), tree-map(+zh), MANIFEST, docs/index.md(+zh), gen_api_docs parser | 190 / 208 / 158 | 221 (per-service sum = 221) | FIXED (verified; parser defect PA-02 fixed) |
| A6 | syscalls | 54 | website meta/stat, INTERFACES.md, docs/index.md | 53 (historical quote only) | 54 | VERIFIED (no drift) |
| A7 | drivers | 38 legacy (OC_DRIVER_DECL) + 7 framework (OC_DRIVER_INFO_DECL) | website meta/features, architecture.html ASCII diagram + table, README, docs/index.md(+zh) | 16 / 15 "families" | 38 drivers in 7 declared families + framework | FIXED (verified) |
| A8 | driver families (declared) | 7 (block 6, nic 9, snd 6, usb 8, wireless 7, sdio 1, placeholder 1) | architecture.html, README | 15 / 16 | 7 | FIXED (verified) |
| A9 | LOC (stats.json caliber) | 163,935 / 807 files | README, website hero/stat/about, MANIFEST (find caliber 158,934 / 780), i18n.js | 158,474 / 128,890 / 130,572 | 163,935 (stats) + 158,934 (MANIFEST find caliber, labeled) | FIXED (verified) |
| A10 | dictionary entries | 311 = enum = registry = EN = zh | docs/manual/dictionary(+zh), verify_docs_counts gate | 308 | 311 | FIXED (gate PASS) |
| A11 | command reference pages | 311 (+1 index) | docs/manual/commands(+zh) | 292 (+1) | 311 (+1) | FIXED (regenerated) |
| A12 | category pages | 25 + index = 26 | dictionary tree | 25 + index | unchanged (verified) | VERIFIED |
| A13 | mega apps / ops | 8 / 154 | README, INTERFACES | 8 / 154 | unchanged (verified) | VERIFIED |
| A14 | per-service ops | time4 block23 nic20 snd17 power9 ota16 fs6 net14 health3 klog3 wdt1 crypto2 l1 3 disp17 inp2 cam10 usb9 ocire2 diag51 tui9 (=221) | INTERFACES.md per-service tables, architecture.html table | diag 9, l1 2, power 8, display 12 | all 20 rows = header truth | FIXED (verified) |
| A15 | regression floor | 300 ring-3 programs | tools/run_fix4_regression.py, README(+zh) | 276 | 300 | FIXED |
| A16 | v1 baseline quotes | 233 (v1.0.0), 251 (v2.x ledger), 19 built-ins (v1 ledger) | INTERFACES.md Part 1, FAQ, KNOWN_ISSUES history | historical record | unchanged | EXEMPT (frozen history: v1/v2 baseline ledgers) |
| A17 | hidden entries (registered, unlisted) | 8 (tui-test/tui-hw/tui-net/tui-sysinfo/tui-time/tui-drv/tui-sys/tui-ext) - fact source: k_hidden[] in ocos/services/l1/src/l1_tui_entry_filter.c | l1 service listings (help/help -a/help -w), desktop tree ext node, README, dictionary tui page(+zh), website (hero/stat/features/architecture, EN+zh), verify_presentation A1b | tui-* fully listed | 8 hidden, directly executable | FIXED (WP-tui-input-encoding-hardening 4.1) |
| A18 | ring3_listed / surface_listed | 292 / 303 (= 292 + 11 builtins) | help -a title, ush short help, README, website hero + stat card, i18n.js(EN+zh) | 300 / 311 (no hidden caliber) | 292 / 303 listed + 311/300 registered calibers kept | FIXED (machine-checked: build_fact_source help -a cross-check) |

## B. Images

| # | Asset | Fact | Status |
|---|-------|------|--------|
| B1 | website/assets/shot-bios.png | regenerated from the RELEASED ISO (SHA d559b8ac... verified before boot); VGA screendump at the ush prompt; banner 3.0.0-WP08-driver-framework (VLM-readback verified) | FIXED (regenerated) |
| B2 | website/assets/shot-uefi.png | same, OVMF UEFI path | FIXED (regenerated) |
| B3 | architecture.html ASCII layer diagram | updated: ocos/ layout, 38 drivers / 7 families, the driver-framework layer (device tree, power hooks, hotplug queue, failure isolation, init order), 221 ops, 300 programs | FIXED |
| B4 | index.html feature grid | added "The driver framework" card (metadata / device tree / power hooks / hotplug / failure isolation / init order) | FIXED |

## C. Documents

| # | Item | Fact | Status |
|---|------|------|--------|
| C1 | README/ARCHITECTURE/FAQ/MANIFEST/INTERFACES + docs/root/ mirrors + docs/zh/root/ mirrors | all numbers per section A; stale paths src/L0, src/L1 -> ocos/ layout; current-release line -> 3.0.0-WP08-driver-framework | FIXED |
| C2 | dictionary (25 category pages x EN/zh) | regenerated with 311 entries incl. jobs/fg/bg; every entry carries an executed example (17 flat/test entries restored from the f5af2ab6 capture, commands unchanged - noted per-entry) | FIXED (gate PASS) |
| C3 | command reference pages (311 x 2) | regenerated (jobs/fg/bg pages new) | FIXED |
| C4 | KNOWN_ISSUES.md | K-01/02/03 open rows intact; PA-01..04 audit findings recorded; history sections untouched | FIXED |
| C5 | ush-reference "12 built-ins" caliber | kept as the shell-surface caliber (12 rows incl. dual-homed help) with the registry caliber (11) stated alongside A3 surfaces | VERIFIED (documented) |

## D. Claims

| # | Claim | Fact | Status |
|---|-------|------|--------|
| D1 | version (published line) | update.json = Release tag = website = 3.0.0-WP08-driver-framework, build 477 | VERIFIED (four-way) |
| D2 | "development preview" qualifiers | the 3.0.0 line is a release line (WP08 shipped); preview qualifiers removed from website/README/download | FIXED |
| D3 | Makefile default version | 3.0.0-NAK (the development-trunk identity; generated doc titles track it by design) | EXEMPT (documented convention; not a user-facing version claim) |
| D4 | OCSE medium | last cut at the WP-l1apps-merge milestone; download card now says so | FIXED (labeled) |
| D5 | License/copyright | Apache 2.0, cubestudio-dev <cubestudio@qq.com>, SPDX on every file | VERIFIED |
| D6 | "disaster recovery & push channels" release/ description | no such public doc exists (stale description); private-mirror checklist items removed from ci-gates.md (EN+zh) | FIXED |

## E. Privacy

| # | Item | Action | Status |
|---|------|--------|--------|
| E1 | bug-list.md | archived to the private mirror (internal/wp-presentation-consistency-audit/), removed from the public tree, all 16 references rewritten (EN+zh) | FIXED |
| E2 | ci-gates.md mirror/push items | private-mirror checklist entries removed (EN+zh) | FIXED |
| E3 | word-list scan | tools/audit/scan_privacy.sh (extended word list: private-repo references, audit-report structure, user-conversation traces; BUG-AUD-2a-001 technical references are public issue identifiers and pattern-narrowed) | zero non-tool hits (re-verified) |

## Exemption register (why these rows stay)

| Surface | Reason |
|---------|--------|
| docs/release/*.md (frozen release notes, both languages) | they describe the tree AT THAT release; rewriting their numbers would falsify history |
| docs/archive/** | frozen historical archive |
| site-data.js history[] + past-asset URLs / download links | those strings ARE the real published assets of past releases |
| KNOWN_ISSUES.md history sections + problem tables | frozen per-problem record (the numbers inside quote the era they describe) |
| INTERFACES.md Part 1 v1 ledger / FAQ v1 baseline quotes | v1.0.0/v2.x compatibility baselines are historical fact |
| ARCHIVE-README.md | describes the v3.0.0 pre-NAK mirror (historical) |
| Makefile 3.0.0-NAK default | development-trunk identity, by-design doc-title convention |
| tree-map "55 entries" / "40 commands" / "16 entries" style rows | per-directory / per-table counts, not command-surface claims (scan-pattern context misses) |
