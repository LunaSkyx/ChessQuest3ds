# build_cia.ps1 - build ChessQuest3DS.cia  (usage: powershell -ExecutionPolicy Bypass -File tools\build_cia.ps1)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$dk = 'C:\devkitPro\tools\bin'
& "$PSScriptRoot\build.ps1"; if ($LASTEXITCODE -ne 0) { throw 'build failed' }
$cia = "$root\build\cia"; New-Item -ItemType Directory -Force $cia | Out-Null
python "$PSScriptRoot\make_banner.py"; if ($LASTEXITCODE -ne 0) { throw 'banner art failed' }
& "$PSScriptRoot\bin\bannertool.exe" makebanner -i "$cia\banner.png" -a "$cia\banner.wav" -o "$cia\banner.bnr"
if ($LASTEXITCODE -ne 0) { throw 'bannertool failed' }
$out = "$root\ChessQuest3DS.cia"
Remove-Item $out -Force -ErrorAction SilentlyContinue
& "$dk\makerom.exe" -f cia -o $out -rsf "$PSScriptRoot\Lichess3DS.rsf" -target t -exefslogo -elf "$root\Lichess3DS.elf" `
    -icon "$root\Lichess3DS.smdh" -banner "$cia\banner.bnr" "-DAPP_ROMFS=$root\romfs"
if ($LASTEXITCODE -ne 0) { throw 'makerom failed' }
"{0}  {1:N1} MB" -f $out, ((Get-Item $out).Length / 1MB)
