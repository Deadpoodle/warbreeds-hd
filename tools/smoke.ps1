# Launch a game copy, wait, and report whether it is still alive.
# Usage: smoke.ps1 [-Seconds 5] [-Keep] [-Dir testrun]
# Automated tests use testrun (hidden via its ini); testgame is the player's copy.
param([int]$Seconds = 5, [switch]$Keep, [string]$Dir = "testrun")
$t = Join-Path $PSScriptRoot "..\$Dir" | Resolve-Path
$exe = "$t\WARBREED.EXE"
$mine = Get-CimInstance Win32_Process -Filter "Name='WARBREED.EXE'" | Where-Object { $_.ExecutablePath -eq $exe }
if ($mine) { "refusing: $Dir is already running (PID $($mine.ProcessId))"; exit 1 }
$start = Get-Date
$p = Start-Process -FilePath $exe -WorkingDirectory $t -PassThru
Set-Content (Join-Path $PSScriptRoot "..\re\pid.txt") $p.Id
Start-Sleep $Seconds
$alive = -not $p.HasExited
"alive after ${Seconds}s: $alive"
$crash = Get-WinEvent -FilterHashtable @{LogName = 'Application'; Id = 1000; StartTime = $start } -ErrorAction SilentlyContinue |
    Where-Object { $_.Message -match 'WARBREED' } | Select-Object -First 1
if ($crash) { ($crash.Message -split "`n" | Select-String 'Faulting module name|Fault offset|Exception code') -join "`n" }
if ($alive -and -not $Keep) { Stop-Process -Id $p.Id -Force }
