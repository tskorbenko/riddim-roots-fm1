# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Host tests (portable core, TinyCC) and native acceptance on the emulated FM-1.
[CmdletBinding()]
param([switch]$HostOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
try {
    New-Item -ItemType Directory -Force build | Out-Null
    & .tools/tcc/tcc/tcc.exe -Isrc/core tests/host_core.c -o build/host_core.exe
    if ($LASTEXITCODE) { throw 'host test compile failed' }
    & ./build/host_core.exe | Tee-Object build/host-test.log
    if ($LASTEXITCODE) { throw 'host tests failed' }
    if ($HostOnly) { return }
    & node tests/emulator.mjs | Tee-Object build/emulator.log
    if ($LASTEXITCODE) { throw 'native acceptance failed' }
} finally { Pop-Location }
