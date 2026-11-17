<!-- SPDX-License-Identifier: Apache-2.0 -->
# Dictionary example-recapture evidence (the alignment repair)

Live session proving the two entries that could not be repaired from
existing captures were re-executed against the SHIPPING ISO
(`OpenCubeOS-v3.0.0-WP-07.iso`, SHA-256
`7f305232f9a0924c6189063fea6709c8443a7f558125301e75c9d0b608e62eb8`,
byte-identical with the release asset):

| Entry | Provenance |
|---|---|
| `exec_test` | recaptured live: the fork/exec race transcript (child execs `hello`, parent waits) - interleaving, the stray echo fragments and the prompt glued after the PASS line are verbatim serial output |
| `env` | recaptured live: the complete output including the final prompt (the earlier window was cut at the `PS1=ush> ` line) |

- exec_test-env-shipping-iso.jsonl: the capture records (raw serial
  output with `\r\r\n` line endings, as captured; `env`'s record also
  carries the tail of the preceding `exec_test` window - the `env`
  transcript starts at its own `env` echo line).

The other 69 repaired entries were restored from the FIRST capture
pass (commit a1461f09: every window still opened with its own command
echo) and from re-assigning misaligned windows of the second pass
(each misaligned window verbatim contains another command's complete
transcript; ownership was re-derived from the window's opening echo
line).  No example was rewritten or synthesized: every block in the
dictionary is a byte-for-byte captured serial transcript.
