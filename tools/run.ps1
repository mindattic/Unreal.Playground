[CmdletBinding()]
param(
  [ValidateRange(1, 4)][int]$Players = 1,
  [string]$Extra = '',
  [int]$ClientDelaySeconds = 10,
  # Play the packaged Shipping build (Builds\Cpp\Windows) instead of the editor's -game.
  [switch]$Packaged,
  # Skip the MindAttic.Media.Capture recording.
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
# Every run records itself: player 1's game gets -capture video hq, and the
# MindAttic.Media.Capture.Unreal plugin inside it drives capture.exe (installed by that repo's
# run.ps1) from launch to exit, so the session lands in R:\MindAttic\Capture\Unreal.Playground\.
# With several players, only player 1 records.

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
  Write-Warning "MindAttic.Media.Capture isn't installed ($capture), so the game's capture plugin will find no agent; run D:\Projects\MindAttic\MindAttic.Media.Capture\run.ps1. Playing without recording."
  $useCapture = $false
}

if ($Packaged) {
  $exe = Join-Path $repo 'Builds\Cpp\Windows\Unreal.Playground.Cpp.exe'
  if (-not (Test-Path -LiteralPath $exe)) { throw "No packaged build at $exe; run tools\package.ps1 -Only Cpp." }
}
else {
  $exe = Join-Path (Find-UnrealEngine) 'Engine\Binaries\Win64\UnrealEditor.exe'
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
  if ($useCapture -and $i -eq 0) { $gameArgs += @('-capture', 'video', 'hq') }
  if (-not [string]::IsNullOrWhiteSpace($Extra)) { $gameArgs += $Extra }

  if ($i -eq 1) {
    Write-Output "Waiting $ClientDelaySeconds s for the listen server to come up..."
    Start-Sleep -Seconds $ClientDelaySeconds
  }
  Write-Output "Player $($i + 1): $target ($ResX x $ResY, windowed)"
  if ($useCapture -and $i -eq 0) { Write-Output "Recording in-game with MindAttic.Media.Capture -> R:\MindAttic\Capture\Unreal.Playground" }
  Start-Process -FilePath $exe -ArgumentList $gameArgs | Out-Null
}
