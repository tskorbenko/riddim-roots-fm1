# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Reproducible native build of RIDDIM ROOTS v0.3.0 on the pinned FM1 Open SDK.
# The SDK tree is reset to its pinned revision, project sources are copied into
# app/riddim, two one-line hooks are inserted, then the unchanged official
# SDK build (toolchain manifest checks, image/SRAM/package checks) runs.
[CmdletBinding()]
param([string]$Toolchain = $env:FM1_PI32_TOOLCHAIN)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = '0.3.0'
Push-Location -LiteralPath $root
try {
    if (!$Toolchain) { $Toolchain = Join-Path $root '.tools/jieli/C$/JL/pi32' }
    $sdk = Join-Path $root 'vendor/fm1_open'
    if (!(Test-Path -LiteralPath (Join-Path $sdk 'sdk/Build.ps1'))) { throw 'Run tools/Setup.ps1 first.' }
    $safe = $sdk.Replace('\', '/')
    & git -c "safe.directory=$safe" -C $sdk checkout -q -- app
    if ($LASTEXITCODE) { throw 'Cannot reset SDK app sources' }
    $target = Join-Path $sdk 'app/riddim'
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    New-Item -ItemType Directory -Force $target, (Join-Path $root 'dist') | Out-Null
    Copy-Item src/core/*.c, src/core/*.h, src/fw/*.c, src/fw/*.h, src/ui/*.c, src/ui/*.h $target -Force
    Copy-Item src/fw/screen.c.in (Join-Path $sdk 'app/screen.c') -Force
    Copy-Item src/fw/fm_factory_min.c.in (Join-Path $sdk 'app/fm_factory.c') -Force

    $router = Join-Path $sdk 'app/midi_router.c'
    $text = [IO.File]::ReadAllText($router)
    $anchor = 'if (source >= 3 || !m || !n || n > 3) return;'
    if (!$text.Contains($anchor)) { throw 'MIDI router hook point changed' }
    $text = $text.Replace('#include "instrument.h"', "#include `"instrument.h`"`n#include `"riddim/rr_app.h`"")
    $text = $text.Replace($anchor, "$anchor`n    if (rr_midi_receive(source, m, n)) return;")
    [IO.File]::WriteAllText($router, $text)

    $audio = Join-Path $sdk 'app/audio_output.c'
    $text = [IO.File]::ReadAllText($audio)
    $anchor = 'if (!output) return;'
    if (!$text.Contains($anchor)) { throw 'Audio output hook point changed' }
    $text = $text.Replace('#include "audio_output.h"', "#include `"audio_output.h`"`n#include `"riddim/rr_fx.h`"")
    $text = $text.Replace($anchor, "$anchor`n    rr_fx_process(output);")
    [IO.File]::WriteAllText($audio, $text)

    & (Join-Path $sdk 'sdk/Build.ps1') -FirmwareOnly -Toolchain $Toolchain
    if (!$?) { throw 'Native SDK build failed.' }
    $build = Join-Path $sdk 'build/dx7'
    $report = Get-Content "$build/firmware-report.json" -Raw | ConvertFrom-Json
    $package = Get-Content "$build/package-report.json" -Raw | ConvertFrom-Json
    if (!$report.passed -or !$package.fits) { throw 'SDK validation did not pass.' }
    $symbols = & (Join-Path $Toolchain 'bin/llvm-nm.exe') --print-size "$build/sdk.elf"
    $fx = @($symbols | Where-Object { $_ -cmatch '^01c[0-7][0-9a-f]{4}\s+[0-9a-f]+\s+[NnTt]\s+rr_fx_process$' })
    if ($fx.Count -ne 1) { throw 'rr_fx_process must be linked into internal SRAM.' }
    $out = "dist/RIDDIM_ROOTS_v$version.fwsc"
    Copy-Item "$build/generated/FM1_OFFICIAL_OPEN.fwsc" $out -Force
    Copy-Item "$build/firmware-report.json", "$build/package-report.json", "$build/build-identity.json" dist -Force
    $hash = (Get-FileHash $out -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  RIDDIM_ROOTS_v$version.fwsc" | Set-Content -Encoding ascii dist/SHA256SUMS.txt
    Write-Host "Firmware: $out"
    Write-Host "SHA-256:  $hash"
    Write-Host ("App image {0} bytes, runtime heap {1} bytes (minimum {2})" -f $report.app_image_bytes, $report.runtime_heap_bytes, $report.minimum_runtime_heap_bytes)
} finally { Pop-Location }
