# Build the 3DS app inside the devkitPro msys2 shell.  usage: tools\build.ps1 [-Clean] [-Cia]
param([switch]$Clean, [string]$Extra = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$drive = $root.Substring(0,1).ToLower(); $unix = "/$drive" + $root.Substring(2).Replace('\','/')
$target = if ($Clean) { 'clean all' } else { 'all' }
$cmd = "export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM; export PATH=`$DEVKITARM/bin:`$DEVKITPRO/tools/bin:`$PATH; cd '$unix' && make -j8 $target EXTRA_CFLAGS='$Extra' 2>&1"
$env:MSYSTEM = 'MSYS'
& 'C:\devkitPro\msys2\usr\bin\bash.exe' -lc $cmd
exit $LASTEXITCODE
