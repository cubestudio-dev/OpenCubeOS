== v2.0.0 release verification record ==

1. Reproducible build: PASS - ELF byte-identical across two clean builds
   sha256 ae0b80b1ce4ce36ca91bb61df007dd892378a0759af153fdce36b4afaef65a59

2. host tests: 17 passed, 2 skipped (openssl fixtures) - host_tests.log
3. wp12 watchdog/logging/crash e2e (BIOS): 14/14 - wp12_e2e.log
4. wp10_tui suite (BIOS): 37/37 - wp10_tui_bios.log
5. chinese e2e (BIOS): 21/21 - chinese_bios.log (uname: Open Cube OS 2.0.0 x86_64)
6. tuiqa desktop apps: BIOS 32/32 + UEFI 32/32
7. native TUI e2e: BIOS 36 PASS / 0 FAIL + UEFI 36 PASS / 0 FAIL - native_e2e.log

Assets (build 302):
efb650e25481a7b0ce9961c21c9747942d79e7b4417eb80ec41719a7bdec3854  build/opencube.iso
327c57766785d48347de780713f93b0f63d5f3786cda0d78849ede544772d691  dist/OpenCubeOS-2.0.0-update.tar.gz
92b415a0bb476cdd2539681cd5a3fd5d4b02620115006007e03af26e61a3d672  dist/OpenCubeOS-src-2.0.0.zip
