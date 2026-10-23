<!-- SPDX-License-Identifier: Apache-2.0 -->
# L1 guide: OTA and A/B

## The two headers

- `kernel/ota/ota_ab.h` (21 declarations): slot discovery, flags,
  slot state, install-onto-slot, rollback - the numbered +7.
- `kernel/ota/ota_update.h` (12 declarations): the chain - check,
  download, verify (SHA256 + signature), install, set_boot,
  get_status, rollback.

## The L1 mirror

`l1/l1_ext.h` mirrors the chain for module code:
`l1_ext_check_update(_async)`, `l1_ext_update_check_pkg`,
`l1_ext_update_download`, `l1_ext_update_verify`,
`l1_ext_update_install`, `l1_ext_update_rollback`,
`l1_ext_update_set_boot`, `l1_ext_update_get_status`.

## Using it from a module

The canonical flow (what the `update` command runs):

```
l1_ext_check_update(&info);            // manifest from /etc config
l1_ext_update_download(url, path);
l1_ext_update_verify(path, sha256);
l1_ext_update_install(path, "B");      // inactive slot
// next boot flips; l1_ext_update_rollback() flips back
```

The `l1_ext_update_info_t.version[64]`/`changes[256]` mirror
`ota_update_info_t` exactly (the 32->64 widening was a real bug:
32 truncated the 44-char official version string).

## In-guest tests

`ab_partition_test`, the `update_*_test` battery, `checkupdate_test`.
