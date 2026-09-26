#!/usr/bin/env bash
# Open Cube OS — release helper (codifies the 4-step WP production workflow).
#
# Usage:
#   bash tools/release.sh          # full: build + upload + verify + print constants
#   bash tools/release.sh build    # step 1+2: make + iso + sha256
#   bash tools/release.sh upload   # step 2: upload ISO+src to server (chunked if slow)
#   bash tools/release.sh verify   # step 3+4: compare local/server/website SHA256+size
#
# Every step prints REAL output (no text-only descriptions).

set -e
OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$OC_ROOT"

export PATH="/home/z/opt/extract/usr/bin:$PATH"
export OC_TOOLS="/home/z/opt/extract"
export LD_LIBRARY_PATH="/tmp/qlibs/extracted/usr/lib/x86_64-linux-gnu:/home/z/opt/extract/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"

WEB_DOWNLOADS="/home/z/my-project/public/downloads"
PAGE_TSX="/home/z/my-project/src/app/page.tsx"
SSH_PY="/tmp/ssh_exec.py"

# ---- helpers ----
log() { printf '\n\033[1;36m[%s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*"; }

sha256_local() { sha256sum "$1" | cut -d' ' -f1; }
size_local()   { stat -c%s "$1"; }

# Step 1+2: build
do_build() {
  log "STEP 1-2: build kernel + ISO"
  make 2>&1 | tail -3
  make iso 2>&1 | tail -3
  log "local artifacts"
  ls -la build/opencube.iso
  log "local SHA256 + size"
  printf '  ISO:  %s  %s bytes\n' "$(sha256_local build/opencube.iso)" "$(size_local build/opencube.iso)"
}

# Step 2: upload (chunked gzip for the slow frp tunnel)
do_upload() {
  log "STEP 2: build source zip"
  rm -f /tmp/oc-src.zip
  zip -qr /tmp/oc-src.zip . -x "build/*" "dist/*" ".git/*" "releases/*" "iso/*" "*.bak" "OpenCubeOS-src-*.zip"
  log "src zip: $(size_local /tmp/oc-src.zip) bytes  sha256=$(sha256_local /tmp/oc-src.zip)"

  log "STEP 2: gzip ISO for tunnel-friendly upload"
  gzip -k -9 -f build/opencube.iso
  log "iso.gz: $(size_local build/opencube.iso.gz) bytes (ratio $(python3 -c "print(f'{$(size_local build/opencube.iso.gz)/$(size_local build/opencube.iso)*100:.0f}%')"))"

  log "STEP 2: upload src zip (small, direct SFTP)"
  python3 - <<'PYEOF'
import paramiko, time
key = paramiko.Ed25519Key.from_private_key_file('/home/z/.ssh/id_ed25519')
t = paramiko.Transport(('sj.frp.one', 18600)); t.connect(username='administrator', pkey=key)
sftp = paramiko.SFTPClient.from_transport(t)
t0=time.time()
sftp.put('/tmp/oc-src.zip', 'J:/Open_Cube_OS_server/OpenCubeOS-src-WP06-latest.zip')
print(f'  src zip uploaded in {time.time()-t0:.1f}s')
sftp.close(); t.close()
PYEOF

  log "STEP 2: upload iso.gz in 200KB chunks (with retry)"
  python3 - <<'PYEOF'
import paramiko, os, io, time, sys
CHUNK=200*1024
local_gz='build/opencube.iso.gz'
remote_dir='J:/Open_Cube_OS_server/_chunks'
key = paramiko.Ed25519Key.from_private_key_file('/home/z/.ssh/id_ed25519')
def new():
    t=paramiko.Transport(('sj.frp.one',18600)); t.connect(username='administrator',pkey=key)
    return paramiko.SFTPClient.from_transport(t), t
data=open(local_gz,'rb').read()
n=(len(data)+CHUNK-1)//CHUNK
print(f'  gz={len(data)} bytes -> {n} chunks')
s,t=new()
try: s.mkdir(remote_dir)
except: pass
s.close(); t.close()
for i in range(n):
    chunk=data[i*CHUNK:(i+1)*CHUNK]; rp=f'{remote_dir}/chunk_{i:03d}'
    for att in range(6):
        try:
            s,t=new()
            try:
                if s.stat(rp).st_size==len(chunk): break
            except: pass
            s.putfo(io.BytesIO(chunk), rp); s.close(); t.close(); break
        except Exception as e:
            try: s.close(); t.close()
            except: pass
            print(f'  chunk {i} att {att} fail: {e}'); time.sleep(2)
    else:
        print(f'  chunk {i} FAILED'); sys.exit(1)
    if (i+1)%5==0 or i==n-1: print(f'  {i+1}/{n} uploaded')
print('  all chunks uploaded; reassembling on server...')
PYEOF

  log "STEP 2: reassemble + decompress on server"
  cat > /tmp/_reassemble.ps1 <<'PSEOF'
$root='J:\Open_Cube_OS_server'
$cs=Get-ChildItem "$root\_chunks" -Filter 'chunk_*' | Sort Name
$o=[System.IO.File]::Create("$root\_iso.gz")
foreach($c in $cs){ $b=[System.IO.File]::ReadAllBytes($c.FullName); $o.Write($b,0,$b.Length) }
$o.Close()
$i=[System.IO.File]::OpenRead("$root\_iso.gz")
$iso=[System.IO.File]::Create("$root\OpenCubeOS-WP06-latest.iso")
$gz=New-Object System.IO.Compression.GzipStream($i,[System.IO.Compression.CompressionMode]::Decompress)
$gz.CopyTo($iso); $gz.Close(); $iso.Close(); $i.Close()
Copy-Item "$root\OpenCubeOS-WP06-latest.iso" "$root\build\opencube.iso" -Force
Remove-Item "$root\_chunks" -Recurse -Force; Remove-Item "$root\_iso.gz" -Force
Write-Output ("iso: " + (Get-Item "$root\OpenCubeOS-WP06-latest.iso").Length + " bytes")
PSEOF
  python3 - <<'PYEOF'
import paramiko
key=paramiko.Ed25519Key.from_private_key_file('/home/z/.ssh/id_ed25519')
t=paramiko.Transport(('sj.frp.one',18600)); t.connect(username='administrator',pkey=key)
s=paramiko.SFTPClient.from_transport(t); s.put('/tmp/_reassemble.ps1','J:/Open_Cube_OS_server/_r.ps1'); s.close(); t.close()
PYEOF
  python3 $SSH_PY "powershell -ExecutionPolicy Bypass -File J:\\Open_Cube_OS_server\\_r.ps1" 2>&1 | grep -i "iso:"
  rm -f build/opencube.iso.gz

  log "STEP 2: copy to public/downloads (website source)"
  mkdir -p "$WEB_DOWNLOADS"
  cp build/opencube.iso "$WEB_DOWNLOADS/opencube-wp06.iso"
  cp /tmp/oc-src.zip "$WEB_DOWNLOADS/opencube-wp06-src.zip"
  ls -la "$WEB_DOWNLOADS"/opencube-wp06.*
}

# Step 3+4: verify consistency across local / server / website
do_verify() {
  log "STEP 3: read SHA256 + size from all three locations"
  LOCAL_ISO=$(sha256_local "$WEB_DOWNLOADS/opencube-wp06.iso")
  LOCAL_ISO_SZ=$(size_local "$WEB_DOWNLOADS/opencube-wp06.iso")
  LOCAL_SRC=$(sha256_local "$WEB_DOWNLOADS/opencube-wp06-src.zip")
  LOCAL_SRC_SZ=$(size_local "$WEB_DOWNLOADS/opencube-wp06-src.zip")

  SRV=$(python3 $SSH_PY "certutil -hashfile J:\\Open_Cube_OS_server\\OpenCubeOS-WP06-latest.iso SHA256 2>nul & echo --- & certutil -hashfile J:\\Open_Cube_OS_server\\OpenCubeOS-src-WP06-latest.zip SHA256 2>nul" 2>&1 | tr -d '\r' | grep -iE "^[0-9a-f]{64}$")
  SRV_ISO=$(echo "$SRV" | sed -n '1p' | tr -d '[:space:]')
  SRV_SRC=$(echo "$SRV" | sed -n '2p' | tr -d '[:space:]')

  WEB_ISO=$(grep "const ISO_SHA256" "$PAGE_TSX" | sed -E "s/.*'([0-9a-f]+)'.*/\1/")
  WEB_SRC=$(grep "const SRC_ZIP_SHA256" "$PAGE_TSX" | sed -E "s/.*'([0-9a-f]+)'.*/\1/")
  WEB_ISO_SZ=$(grep "const ISO_SIZE_BYTES" "$PAGE_TSX" | grep -oE '[0-9]+')
  WEB_SRC_SZ=$(grep "const SRC_ZIP_SIZE_BYTES" "$PAGE_TSX" | grep -oE '[0-9]+')

  printf '\n%-12s %-18s %-18s %s\n' "location" "ISO sha256" "SRC sha256" "ISO size / SRC size"
  printf '%-12s %s  %s  %s / %s\n' "local"  "${LOCAL_ISO:0:16}…" "${LOCAL_SRC:0:16}…" "$LOCAL_ISO_SZ" "$LOCAL_SRC_SZ"
  printf '%-12s %s  %s  %s\n' "server" "${SRV_ISO:0:16}…" "${SRV_SRC:0:16}…" "(server)"
  printf '%-12s %s  %s  %s / %s\n' "website" "${WEB_ISO:0:16}…" "${WEB_SRC:0:16}…" "$WEB_ISO_SZ" "$WEB_SRC_SZ"

  log "STEP 3: consistency check"
  if [ "$LOCAL_ISO" = "$SRV_ISO" ] && [ "$LOCAL_ISO" = "$WEB_ISO" ] && \
     [ "$LOCAL_SRC" = "$SRV_SRC" ] && [ "$LOCAL_SRC" = "$WEB_SRC" ] && \
     [ "$LOCAL_ISO_SZ" = "$WEB_ISO_SZ" ] && [ "$LOCAL_SRC_SZ" = "$WEB_SRC_SZ" ]; then
    printf '  \033[1;32mALL MATCH\033[0m  ISO=%s  SRC=%s\n' "$LOCAL_ISO" "$LOCAL_SRC"
  else
    printf '  \033[1;31mMISMATCH\033[0m — fix page.tsx / re-upload\n'
    return 1
  fi

  log "STEP 4: lint + dev server check"
  (cd /home/z/my-project && bun run lint 2>&1 | tail -2)
  printf '  dev.log tail:\n'; tail -3 /home/z/my-project/dev.log

  log "STEP 4: website download check (curl -I)"
  curl -sI http://localhost:3000/downloads/opencube-wp06.iso     | grep -iE "HTTP|Content-Length"
  curl -sI http://localhost:3000/downloads/opencube-wp06-src.zip | grep -iE "HTTP|Content-Length"

  log "DONE. Paste these constants into src/app/page.tsx if changed:"
  printf "const ISO_SHA256 = '%s';\n" "$LOCAL_ISO"
  printf "const ISO_SIZE_BYTES = %s;\n" "$LOCAL_ISO_SZ"
  printf "const SRC_ZIP_SHA256 = '%s';\n" "$LOCAL_SRC"
  printf "const SRC_ZIP_SIZE_BYTES = %s;\n" "$LOCAL_SRC_SZ"
}

case "${1:-all}" in
  build)  do_build ;;
  upload) do_upload ;;
  verify) do_verify ;;
  all)    do_build; do_upload; do_verify ;;
  *) echo "usage: $0 [build|upload|verify|all]"; exit 1 ;;
esac
