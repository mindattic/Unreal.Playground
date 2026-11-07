[CmdletBinding()]
param(
  [ValidateRange(1, 4)][int]$Players = 1,
  [string]$Extra = '',
  [int]$ClientDelaySeconds = 10,
  # Play the packaged Shipping build (Builds\Cpp\Windows) instead of the editor's -game.
  [switch]$Packaged,
  # Skip the MindAttic.Capture recording.
  [switch]$NoCapture,
  [int]$ResX = 1920,
  [int]$ResY = 1080,
  # UNR-T87: no window, no sound, for scripted/automated testing, through the shared launcher
  # (..\Unreal.Shared\tools\Start-Headless.ps1, MindAtticShared's --headless). Waits for the
  # MINDATTIC_HEADLESS_READY line and leaves the game running (its process is returned);
  # -StopAfterReady stops it once ready (a start-up smoke test).
  [switch]$Headless,
  [switch]$StopAfterReady,
  [int]$TimeoutSeconds = 300
)

# Launches the game windowed at 1920x1080. Players > 1: instance 1 is a listen server, the rest
# join it on 127.0.0.1.
#
# Every run is filmed by MindAttic.Capture (capture.exe, installed by that repo's run.ps1): the
# game's window is recorded by its handle from launch to exit (its 1920x1080 picture plus the title
# bar, at the hq tape quality) and the session lands in
# R:\MindAttic\Capture\Unreal.Playground\. With several players, player 1's window is the one filmed.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'find-engine.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $repo 'UnrealSoulslike.uproject'
$map = '/Game/Souls/Maps/Lvl_Isle'

if ($Headless) {
  # The editor's -game on Lvl_Isle, which the game boots straight into (there is no menu to wait at).
  if ($Players -gt 1 -or $Packaged) { throw '-Headless runs one player from the editor binaries (a Shipping build writes no log for the ready line).' }
  $launcher = Join-Path $repo '..\Unreal.Shared\tools\Start-Headless.ps1'
  if (-not (Test-Path -LiteralPath $launcher)) { throw "-Headless needs Unreal.Shared cloned next to this repo ($launcher)." }
  $gameArgs = @("-ResX=$ResX", "-ResY=$ResY") + @($Extra -split '\s+' | Where-Object { $_ })
  # -PassThru by name, so the game's own -flags never reach PowerShell's parameter binding.
  return & $launcher -Project $uproject -EngineRoot (Find-UnrealEngine) -Map $map -Wait -TimeoutSeconds $TimeoutSeconds `
    -StopAfterReady:$StopAfterReady -PassThru $gameArgs
}

$capture = Join-Path $env:LOCALAPPDATA 'MindAttic\Capture\bin\capture.exe'
$useCapture = -not $NoCapture
if ($useCapture -and -not (Test-Path -LiteralPath $capture)) {
  Write-Warning "MindAttic.Capture isn't installed ($capture); run D:\Projects\MindAttic\MindAttic.Capture\run.ps1. Playing without recording."
  $useCapture = $false
}

if ($Packaged) {
  $exe = Join-Path $repo 'Builds\Cpp\Windows\Unreal.Playground.Cpp.exe'
  if (-not (Test-Path -LiteralPath $exe)) { throw "No packaged build at $exe; run tools\package.ps1 -Only Cpp." }
  # The packaged exe is a launcher stub: the real window belongs to the Shipping process.
  $realProcess = 'UnrealSoulslike-Win64-Shipping'
}
else {
  $exe = Join-Path (Find-UnrealEngine) 'Engine\Binaries\Win64\UnrealEditor.exe'
  $realProcess = $null
}

for ($i = 0; $i -lt $Players; $i++) {
  if ($Players -eq 1) { $target = $map }
  elseif ($i -eq 0) { $target = "$map?listen" }
  else { $target = '127.0.0.1' }

  $winX = 40 + 80 * $i
  $winY = 40 + 60 * $i
  $gameArgs = @()
  if (-not $Packaged) { $gameArgs += @("`"$uproject`"", $target, '-game') }
  elseif ($Players -gt 1) { $gameArgs += $target }
  $gameArgs += @('-windowed', "-ResX=$ResX", "-ResY=$ResY", "-WinX=$winX", "-WinY=$winY", '-nosplash')
  if (-not [string]::IsNullOrWhiteSpace($Extra)) { $gameArgs += $Extra }

  if ($i -eq 1) {
    Write-Output "Waiting $ClientDelaySeconds s for the listen server to come up..."
    Start-Sleep -Seconds $ClientDelaySeconds
  }
  Write-Output "Player $($i + 1): $target ($ResX x $ResY, windowed)"
  $launched = Get-Date
  $game = Start-Process -FilePath $exe -ArgumentList $gameArgs -PassThru
  if ($useCapture -and $i -eq 0) {
    # Film the game's own window once it is up and drawing (a recorder attached any earlier finds a
    # window that isn't ready and can't start), and stop the recording when the game exits.
    $find = if ($realProcess) {
      "Get-Process -Name '$realProcess' -ErrorAction SilentlyContinue | Where-Object { `$_.StartTime -ge ([datetime]'$($launched.ToString('o'))').AddSeconds(-2) }"
    } else {
      "Get-Process -Id $($game.Id) -ErrorAction SilentlyContinue"
    }
    $watch = @"
`$deadline = (Get-Date).AddSeconds(180)
`$p = `$null
while ((Get-Date) -lt `$deadline) {
  `$p = $find | Where-Object { `$_.MainWindowHandle -ne 0 -and `$_.MainWindowTitle } | Select-Object -First 1
  if (`$p) { break }
  Start-Sleep -Milliseconds 500
}
if (-not `$p) { exit }
Start-Sleep -Seconds 4
# Film the game's window itself (Windows.Graphics.Capture by handle), not a rectangle of the desktop:
# a desktop region films whatever is on top, so a terminal over the game replaced the game in the
# recording. The window capture includes the title bar.
`$hwnd = '0x{0:x}' -f `$p.MainWindowHandle.ToInt64()
# The editor running -game leaves well under the default 2 GB of free RAM (559 and 669 MB were seen),
# which stopped recordings after 5 s; keep a smaller floor for play sessions.
`$cfg = Join-Path `$env:TEMP 'unreal-soulslike-capture.json'
Set-Content -LiteralPath `$cfg -Value '{ "limits": { "minFreeMemoryMB": 384 } }' -Encoding ASCII
Start-Process -FilePath '$capture' -ArgumentList @('record', '--hwnd', `$hwnd, '--app', 'Unreal.Playground', '--hq', '--config', "``"`$cfg``"") -WindowStyle Minimized | Out-Null
`$p.WaitForExit()
& '$capture' stop | Out-Null
"@
    Write-Output "Recording with MindAttic.Capture -> R:\MindAttic\Capture\Unreal.Playground"
    $watchFile = Join-Path $env:TEMP 'unreal-soulslike-capture-watch.ps1'
    Set-Content -LiteralPath $watchFile -Value $watch -Encoding UTF8
    Start-Process -FilePath 'powershell' -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$watchFile`"") -WindowStyle Hidden | Out-Null
  }
}
