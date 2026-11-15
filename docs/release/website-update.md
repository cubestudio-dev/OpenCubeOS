<!-- SPDX-License-Identifier: Apache-2.0 -->
# Website update mechanics

The public site is the **gh-pages branch** of this repository
(https://ocos.cubestudio.cn/, GitHub Pages). The facade is static:
`assets/site-data.js` carries the version data, `download.html` /
`index.html` render it, `downloads/` holds the asset copies.

## The release-time update (per release)

1. Read the assets from `dist/`, compute SHA256 + size.
2. **Facade**: `assets/site-data.js` - update the `dev` channel block
   (version / build / date / the asset four-tuples: name, url, sha,
   size) and insert the release into the `history` array head.
3. **Pages**: `download.html` + `index.html` - replace the previous
   version strings in place (the gh-pages HTML and the archived
   release HTML have structural differences - do not overwrite the
   whole file, only the strings).
4. **OTA manifest**: `update.json` at the branch root (version,
   build, time, changes text, package URL/sha/size) - what
   `checkupdate` inside the system parses.
5. **downloads/**: copy the four release assets in (the lowercase
   `opencube-<ver>.*` canonical names).
6. Two commits (facade + downloads) and push the branch.

## Verification after it runs

- `https://ocos.cubestudio.cn/update.json` shows the new
  version/build/sha.
- The download page's four hashes equal the Release's four.
- `curl -sL <asset-url> | sha256sum` on each asset equals its
  declared SHA256.
- The OTA E2E (`tools/run_fix4_regression.py` update section + the
  checkupdate flow) sees "New version available." on a machine
  built with a lower build id.

## Where the numbers come from

`tools/update_website.sh <version> <build> [date-iso] [changes]`
computes the SHA256/size set from `dist/` and writes the data files
(no hand-editing of versions anywhere) - the same numbers the
GitHub Release body and the private-mirror audit record carry, so
website / Release / mirror hold byte-identical copies.
