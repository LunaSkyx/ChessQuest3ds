# promo_record.ps1 - run a scenario in Azahar and record the emulator window with ffmpeg (for the promo video).
#
#   powershell -ExecutionPolicy Bypass -File tools\promo_record.ps1 -Scenario tests\scenarios\promo.txt -OutDir build\promo
#
# Output: <OutDir>\raw.mkv (window capture, 60 fps), marks.txt ("name seconds" for every "log MARK:name" line the scenario
# writes, seconds measured on the recording clock), log.txt. The emulator window is moved to the top-left of the primary
# monitor and recorded with Windows Graphics Capture (the window itself, so nothing else can end up in the picture). The scenario file is ALWAYS removed again.
param(
    [Parameter(Mandatory)][string]$Scenario,
    [Parameter(Mandatory)][string]$OutDir,
    [int]$TimeoutSec = 300,
    [int]$WinW = 1280, [int]$WinH = 1240, [int]$Fps = 60
)
$ErrorActionPreference = 'Stop'
$repo   = Split-Path $PSScriptRoot
$sd     = "$env:APPDATA\Azahar\sdmc"
$data   = "$sd\lichess3ds"
$azahar = 'C:\Program Files\Azahar\azahar.exe'
$ffmpeg = (& python -c "import imageio_ffmpeg;print(imageio_ffmpeg.get_ffmpeg_exe())").Trim()
$build  = "$repo\Lichess3DS.3dsx"
if (-not (Test-Path $build)) { throw "no build - run tools\build.ps1" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

Add-Type @"
using System; using System.Runtime.InteropServices;
public class PW {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  public struct RECT { public int L, T, R, B; }
  public struct POINT { public int X, Y; }
}
"@
[void][PW]::SetProcessDPIAware()

Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item $build "$sd\Lichess3DS-test.3dsx" -Force
New-Item -ItemType Directory -Force $data | Out-Null
Remove-Item "$data\shots" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$data\log.txt" -Force -ErrorAction SilentlyContinue
Remove-Item "$data\settings.json" -Force -ErrorAction SilentlyContinue
Copy-Item $Scenario "$data\scenario.txt" -Force

$p = $null; $ff = $null; $ok = $false
try {
    $p = Start-Process $azahar -ArgumentList "`"$sd\Lichess3DS-test.3dsx`"" -PassThru
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 20) { $p.Refresh(); if ($p.MainWindowHandle -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 200 }
    # wait until the app itself has started (it writes log.txt); Azahar restores its saved window geometry until then
    while (((Get-Date) - $t0).TotalSeconds -lt 40 -and -not (Test-Path "$data\log.txt")) { Start-Sleep -Milliseconds 100 }
    Start-Sleep -Milliseconds 2500
    $p.Refresh(); $hwnd = $p.MainWindowHandle
    [void][PW]::ShowWindow($hwnd, 9)
    for ($try = 0; $try -lt 4; $try++) {                                  # Azahar may re-apply its own geometry: retry until it sticks
        [void][PW]::SetWindowPos($hwnd, [IntPtr](-1), 20, 20, $WinW, $WinH, 0x40)     # topmost, shown
        [void][PW]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 1200
        $r = New-Object PW+RECT; [void][PW]::GetWindowRect($hwnd, [ref]$r)
        if ([math]::Abs(($r.R - $r.L) - $WinW) -le 4 -and [math]::Abs(($r.B - $r.T) - $WinH) -le 4) { break }
    }
    $cx = $r.L; $cy = $r.T; $cw = ($r.R - $r.L) -band -2; $ch = ($r.B - $r.T) -band -2
    "window: $cx,$cy ${cw}x$ch" | Tee-Object "$OutDir\window.txt"
    if ([math]::Abs($cw - $WinW) -gt 8) { throw "emulator window did not resize ($cw x $ch)" }

    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $ffmpeg
    # Windows Graphics Capture of the emulator window itself: nothing else on the desktop can end up in the picture
    $q = [char]34
    $flt = "gfxcapture=hwnd=$($hwnd.ToInt64()):capture_cursor=0:max_framerate=$Fps,hwdownload,format=bgra,crop=trunc(iw/2)*2:trunc(ih/2)*2"
    $psi.Arguments = "-y -loglevel error -f lavfi -i $q$flt$q -c:v libx264 -preset ultrafast -crf 10 -pix_fmt yuv420p $q$OutDir\raw.mkv$q"
    $psi.RedirectStandardInput = $true; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $ff = [Diagnostics.Process]::Start($psi)
    $clock = [Diagnostics.Stopwatch]::StartNew()

    $marks = New-Object Collections.Generic.List[string]
    $pos = 0L; $buf = ''
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSec) {
        Start-Sleep -Milliseconds 30
        if ($p.HasExited) { break }
        $lp = "$data\log.txt"
        if (-not (Test-Path $lp)) { continue }
        $fs = [IO.File]::Open($lp, 'Open', 'Read', 'ReadWrite')
        try {
            if ($fs.Length -gt $pos) {
                [void]$fs.Seek($pos, 'Begin')
                $bytes = New-Object byte[] ($fs.Length - $pos); [void]$fs.Read($bytes, 0, $bytes.Length); $pos = $fs.Length
                $buf += [Text.Encoding]::UTF8.GetString($bytes)
                $now = $clock.Elapsed.TotalSeconds
                while (($i = $buf.IndexOf("`n")) -ge 0) {
                    $line = $buf.Substring(0, $i); $buf = $buf.Substring($i + 1)
                    if ($line -match 'MARK:(\S+)') { $marks.Add(("{0} {1}" -f $Matches[1], $now.ToString("F2", [Globalization.CultureInfo]::InvariantCulture))); "mark $($Matches[1]) @ {0:N1}s" -f $now }
                    if ($line -match '\[scn\] (done|FAIL)') { $ok = $true }
                }
            }
        } finally { $fs.Close() }
        if ($ok) { break }
    }
    Start-Sleep -Milliseconds 800
    $marks | Set-Content -Encoding ascii "$OutDir\marks.txt"
}
finally {
    if ($ff -and -not $ff.HasExited) { try { $ff.StandardInput.Write('q'); $ff.StandardInput.Flush() } catch {}; if (-not $ff.WaitForExit(15000)) { $ff.Kill() } }
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item "$data\scenario.txt" -Force -ErrorAction SilentlyContinue
    if (Test-Path "$data\log.txt") { Copy-Item "$data\log.txt" "$OutDir\log.txt" -Force }
}
"== promo_record: " + $(if ($ok) { 'DONE' } else { 'FAILED (timeout / emulator exited)' }) + "  -> $OutDir"
if (-not $ok) { exit 1 }
