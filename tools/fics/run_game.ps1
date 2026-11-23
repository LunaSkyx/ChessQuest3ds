# Plays a scripted FICS game between the 3DS app (in Azahar) and a guest bot, with a direct "match" challenge (never a public seek).
param([string]$Scenario = "tests/scenarios/fics1.txt", [string]$Label = "fics", [string]$Tc = "13 7", [string]$BotEnd = "resign", [string]$Delay = "1.8", [string]$Wmoves = "e2e4,g1f3,f1c4,d2d3,h2h3")
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$blog = "logs/fics_bot.log"
Remove-Item $blog -ErrorAction SilentlyContinue
$env:BOT_DELAY = $Delay
$bot = Start-Process python -ArgumentList @("-I", "tools/fics/bot.py", $blog, "match:", $Wmoves, "e7e5,b8c6,g8f6", $BotEnd, "150") -PassThru -WindowStyle Hidden
$name = $null
for ($i = 0; $i -lt 40 -and -not $name; $i++) { Start-Sleep -Milliseconds 500; if (Test-Path $blog) { $m = Select-String -Path $blog -Pattern 'Logging you in as "(Guest\w+)"' | Select-Object -First 1; if ($m) { $name = $m.Matches[0].Groups[1].Value } } }
if (-not $name) { "bot did not log in"; $bot | Stop-Process -Force; exit 1 }
"bot is $name"
$txt = (Get-Content $Scenario -Raw).Replace("BOTNAME", $name).Replace("TC", $Tc)
$tmp = "tests/scenarios/_fics_run.txt"; Set-Content -Path $tmp -Value $txt -Encoding ascii
& powershell -ExecutionPolicy Bypass -File tools/az_run.ps1 -Scenario $tmp -Label $Label -TimeoutSec 160 -Grep "FAIL|fics: (game|result|event)|game:|premove|opp move"
if (-not $bot.HasExited) { $bot | Stop-Process -Force }
Remove-Item $tmp -ErrorAction SilentlyContinue
