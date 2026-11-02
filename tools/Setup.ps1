# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Prepare pinned dependencies. Downloads are verified by SHA-256; nothing is
# flashed to any device. Existing local copies (for example from the previous
# RIDDIM ROOTS checkout next to this folder) are reused when their hashes match.
[CmdletBinding()]
param([string]$Sibling = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'Riddim-roots-fm1'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
function Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function Download([string]$Url, [string]$Path, [string]$Sha) {
    if (!(Test-Path -LiteralPath $Path)) { & curl.exe -L --fail --output $Path $Url; if ($LASTEXITCODE) { throw "Download failed: $Url" } }
    if ((Hash $Path) -cne $Sha) { throw "Hash mismatch for $Path - review the upstream change; do not bypass verification." }
}
function Checkout([string]$Url, [string]$Path, [string]$Revision) {
    if (!(Test-Path -LiteralPath $Path)) { & git clone -q $Url $Path; if ($LASTEXITCODE) { throw "clone failed: $Url" } }
    $safe = [IO.Path]::GetFullPath((Join-Path $root $Path)).Replace('\', '/')
    & git -c "safe.directory=$safe" -C $Path checkout -q --detach $Revision
    if ($LASTEXITCODE) { throw "pinned checkout failed: $Path" }
}
try {
    New-Item -ItemType Directory -Force .tools, build, dist, research, vendor | Out-Null
    foreach ($p in 'git.exe', 'curl.exe', 'python.exe', 'node.exe') { if (!(Get-Command $p -ErrorAction SilentlyContinue)) { throw "Missing prerequisite: $p" } }
    Checkout 'https://github.com/m-vave/fm1_open.git' 'vendor/fm1_open' '4c7496daac04a95ff10d1afdcc8f9ead2dbabc41'
    Checkout 'https://github.com/joakimeriksson/fm1-emu.git' 'research/fm1-emu' 'eb0d897cf6e0ebf4c412e2389a113d878ec28dd0'
    $clang = '.tools/jieli/C$/JL/pi32/bin/clang.exe'
    if (!$env:FM1_PI32_TOOLCHAIN -and !(Test-Path -LiteralPath $clang)) {
        if (Test-Path -LiteralPath (Join-Path $Sibling '.tools/jieli/C$/JL/pi32/bin/clang.exe')) {
            Copy-Item -Recurse (Join-Path $Sibling '.tools/jieli') .tools/
        } else {
            Download 'https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe' '.tools/jieli-2.5.3.exe' '399bd518a28e47a7a0394506cebd5bf658748a58cd6b7c02512e24b4b88c5a5e'
            Download 'https://constexpr.org/innoextract/files/innoextract-1.9-windows.zip' '.tools/innoextract.zip' '6989342c9b026a00a72a38f23b62a8e6a22cc5de69805cf47d68ac2fec993065'
            Expand-Archive .tools/innoextract.zip .tools/innoextract -Force
            & .tools/innoextract/innoextract.exe -q -d .tools/jieli .tools/jieli-2.5.3.exe
            if ($LASTEXITCODE) { throw 'Toolchain extraction failed' }
        }
    }
    if ((Hash $clang) -cne 'd4bc97cf421c1ccc373dd8924ce88720ff7457b3f1478d15df4aeda6ae923bce') { throw 'Unexpected PI32 clang.exe' }
    if (!(Test-Path .tools/tcc/tcc/tcc.exe)) {
        Download 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip' '.tools/tcc.zip' '34a721949a2583fdff725312da092fa0f5f1f284b702e6f811c6954714faabb2'
        Expand-Archive .tools/tcc.zip .tools/tcc -Force
    }
    Download 'https://joakimeriksson.github.io/fm1-emu/fm1_web.wasm' '.tools/fm1_web.wasm' 'aa074eb964d27c16b6a5c3ab4aa95cf76b57a31a45186f053706b16885c3f954'
    Write-Host 'Dependencies ready. Next: tools/Build.ps1, tools/Test.ps1'
} finally { Pop-Location }
