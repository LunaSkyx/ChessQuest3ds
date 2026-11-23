# az_run.ps1 - run the 3DS app unattended in the Azahar emulator and collect the result.
#
#   powershell -ExecutionPolicy Bypass -File tools\az_run.ps1 -Scenario tests\scenarios\demo.txt
#   ... -Binary other.3dsx|other.cia      run a different build (default: Lichess3DS.3dsx)
#   ... -TimeoutSec 120 -Label name       hard stop / output folder name
#   ... -Grep 'regex'                     also print log lines matching the regex
#
# 1. copies the build to the emulated SD card, 2. writes the scenario to sdmc:/lichess3ds/scenario.txt,
# 3. starts azahar.exe and waits for "[scn] done" in sdmc:/lichess3ds/log.txt, 4. kills the emulator and ALWAYS removes
# the scenario file again (the app is inert without it), 5. converts the screenshots to PNG into logs\runs\<label>\.
# Exit: 0 = scenario reached its end, 1 = timeout / emulator died, 2 = "[scn] FAIL".
param(
    [Parameter(Mandatory)][string]$Scenario,
    [string]$Binary = '',
    [int]$TimeoutSec = 120,
    [string]$Label = '',
    [string]$Grep = '',
    [switch]$KeepOpen
)
$ErrorActionPreference = 'Stop'
$repo   = Split-Path $PSScriptRoot
$sd     = "$env:APPDATA\Azahar\sdmc"
$data   = "$sd\lichess3ds"
$azahar = 'C:\Program Files\Azahar\azahar.exe'
if (-not $Binary) { $Binary = "$repo\Lichess3DS.3dsx" }
if (-not (Test-Path $Binary)) { throw "no build at $Binary - run tools\build.ps1" }
$ext = [IO.Path]::GetExtension($Binary)
if (-not $Label) { $Label = (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + [IO.Path]::GetFileNameWithoutExtension($Scenario) }
$out = "$repo\logs\runs\$Label"
New-Item -ItemType Directory -Force $data | Out-Null
Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force

$run = if ($ext -eq '.cia' -or $ext -eq '.cxi') { $Binary } else { Copy-Item $Binary "$sd\Lichess3DS-test.3dsx" -Force; "$sd\Lichess3DS-test.3dsx" }
Copy-Item $Scenario "$data\scenario.txt" -Force
Remove-Item "$data\shots" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$data\log.txt" -Force -ErrorAction SilentlyContinue
Remove-Item "$data\settings.json" -Force -ErrorAction SilentlyContinue   # reproducible defaults (casual, premoves on ...); token.txt stays

$ok = $false; $p = $null
$wall = [Diagnostics.Stopwatch]::StartNew()
try {
    $t0 = Get-Date
    $p = Start-Process $azahar -ArgumentList "`"$run`"" -PassThru
    while (((Get-Date) - $t0).TotalSeconds -lt $TimeoutSec) {
        Start-Sleep -Milliseconds 700
        if ((Test-Path "$data\log.txt") -and (Select-String -Path "$data\log.txt" -Pattern '\[scn\] (done|FAIL)' -Quiet)) { $ok = $true; break }
        if ($p.HasExited) { break }
    }
    if ($ok) { Start-Sleep -Milliseconds 1200 }
}
finally {
    if ($p -and -not $p.HasExited -and -not $KeepOpen) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item "$data\scenario.txt" -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force $out | Out-Null
if (Test-Path "$data\log.txt") { Copy-Item "$data\log.txt" "$out\log.txt" -Force }
Copy-Item $Scenario "$out\scenario.txt" -Force
if (Test-Path "$data\shots") {
    Add-Type -AssemblyName System.Drawing
    foreach ($b in Get-ChildItem "$data\shots\*.bmp") {
        $img = [System.Drawing.Image]::FromFile($b.FullName)
        $img.Save("$out\" + $b.BaseName + '.png', [System.Drawing.Imaging.ImageFormat]::Png)
        $img.Dispose()
    }
}
$failed = (Test-Path "$out\log.txt") -and (Select-String -Path "$out\log.txt" -Pattern '\[scn\] FAIL' -Quiet)
"== az_run: $Label  result=" + $(if ($failed) { 'FAILED (a step failed in the app)' } elseif ($ok) { 'DONE' } else { 'FAILED (timeout / emulator exited)' })
"== output: $out   (wall {0:N1} s)" -f $wall.Elapsed.TotalSeconds
if (Test-Path "$out\log.txt") {
    Select-String -Path "$out\log.txt" -Pattern '\[scn\]' | ForEach-Object { $_.Line }
    if ($Grep) { "== log lines matching '$Grep':"; Select-String -Path "$out\log.txt" -Pattern $Grep | Where-Object { $_.Line -notmatch '\[scn\]' } | ForEach-Object { $_.Line } | Select-Object -First 80 }
    "== pngs: " + (Get-ChildItem "$out\*.png" -ErrorAction SilentlyContinue | Measure-Object).Count
} else { "== no log was written - the app never started" }
if ($failed) { exit 2 }
if (-not $ok) { exit 1 }
