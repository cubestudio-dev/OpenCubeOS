# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-13-build_system
# File: build.ps1
# Purpose: One-command build entry for Windows hosts.  Open Cube OS is a
#          freestanding ELF kernel built with the GNU toolchain, so the
#          actual build always runs in a POSIX environment.  This script
#          finds one and forwards to tools/build.sh:
#
#            1. WSL2 (recommended): wsl.exe -> bash tools/build.sh
#               - the working directory is translated with wslpath
#               - OC_TOOLS is autodetected inside WSL by build.sh
#            2. MSYS2 / Git-Bash: bash.exe on PATH -> same
#            3. otherwise: print actionable setup instructions
#
# Usage (PowerShell):
#   .\build.ps1              # full pipeline (kernel + ISO + SRC + OTA)
#   .\build.ps1 iso          # kernel + bootable ISO only
#   .\build.ps1 -Target check# reproducibility check
#   .\build.ps1 clean
#   .\build.ps1 -Install     # apt-install missing deps inside WSL first
#
# Requirements inside WSL2 (Debian/Ubuntu):
#   gcc nasm xorriso mtools grub-common grub-pc-bin grub-efi-amd64-bin ovmf
#   qemu-system-x86  (only for `make run-bios` / E2E tests)
#   -> .\build.ps1 -Install installs them via build.sh --install

param(
    [Parameter(Position=0)]
    [string]$Target = "all",
    [switch]$Install
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildSh  = Join-Path $RepoRoot "tools\build.sh"

function Write-Intro($mode) {
    Write-Host "build.ps1: Open Cube OS one-command build ($mode)" -ForegroundColor Green
    Write-Host "  repo : $RepoRoot"
    Write-Host "  goal : $Target"
}

# ---- 1. WSL2 (recommended) -------------------------------------------
$wslOk = $false
try {
    $null = & wsl.exe --status 2>$null
    if ($LASTEXITCODE -eq 0) { $wslOk = $true }
} catch { $wslOk = $false }

if ($wslOk) {
    Write-Intro "WSL2"
    $wslPath = & wsl.exe wslpath -a ($BuildSh -replace '\\', '\\') 2>$null
    if (-not $wslPath) {
        # wslpath needs the Windows path in its raw form
        $wslPath = & wsl.exe wslpath -a $BuildSh 2>$null
    }
    if (-not $wslPath) {
        Write-Host "build.ps1: WSL2 present but wslpath failed - is the repo on a Windows drive?" -ForegroundColor Red
        exit 1
    }
    $wslDir = Split-Path $wslPath
    $extra = if ($Install) { "--install" } else { "" }
    & wsl.exe -e bash -lc "cd '$wslDir' && bash ./tools/build.sh $extra $Target"
    exit $LASTEXITCODE
}

# ---- 2. MSYS2 / Git-Bash ---------------------------------------------
$bash = Get-Command bash.exe -ErrorAction SilentlyContinue
if ($bash) {
    Write-Intro "MSYS2/Git-Bash"
    $posixRepo = (& bash.exe -c "cygpath -u '$RepoRoot'").Trim()
    $extra = if ($Install) { "--install" } else { "" }
    & bash.exe -c "cd '$posixRepo' && ./tools/build.sh $extra $Target"
    exit $LASTEXITCODE
}

# ---- 3. no POSIX environment found ------------------------------------
Write-Host "build.ps1: no WSL2 and no MSYS2/Git-Bash found." -ForegroundColor Red
Write-Host ""
Write-Host "Open Cube OS builds with the GNU toolchain (gcc/nasm/xorriso/grub)."
Write-Host "Pick ONE of these setups, then re-run this script:"
Write-Host ""
Write-Host "  Option A (recommended): WSL2"
Write-Host "    1. Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux"
Write-Host "    2. wsl --install -d Debian      (or Ubuntu)"
Write-Host "    3. inside WSL:  sudo apt-get install gcc nasm xorriso mtools \\"
Write-Host "         grub-common grub-pc-bin grub-efi-amd64-bin ovmf zip python3"
Write-Host "    4. re-run:      .\build.ps1"
Write-Host ""
Write-Host "  Option B: MSYS2"
Write-Host "    1. install MSYS2 from https://www.msys2.org"
Write-Host "    2. pacman -S mingw-w64-ucrt-x86_64-gcc nasm mtools make zip"
Write-Host "       (xorriso/grub are not in MSYS2 - WSL2 is the reliable route)"
Write-Host ""
Write-Host "Full guide: docs/BUILD.md"
exit 1
