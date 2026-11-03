[CmdletBinding()]
param(
  [ValidateRange(1, 4)][int]$Players = 1,
  [string]$Extra = '',
  [int]$ClientDelaySeconds = 10
)

# Launches the demo with -game. Players > 1: instance 1 is a listen server, the rest join it on 127.0.0.1.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'find-engine.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $repo 'Unreal.Soulslike.uproject'
$editor = Join-Path (Find-UnrealEngine) 'Engine\Binaries\Win64\UnrealEditor.exe'
$map = '/Game/Variant_Combat/Lvl_Combat'

for ($i = 0; $i -lt $Players; $i++) {
  if ($Players -eq 1) { $target = $map }
  elseif ($i -eq 0) { $target = "$map?listen" }
  else { $target = '127.0.0.1' }

  $winX = 40 + 80 * $i
  $winY = 40 + 60 * $i
  $argLine = "`"$uproject`" $target -game -windowed -ResX=1280 -ResY=720 -WinX=$winX -WinY=$winY -log"
  if (-not [string]::IsNullOrWhiteSpace($Extra)) { $argLine = "$argLine $Extra" }

  if ($i -eq 1) {
    Write-Output "Waiting $ClientDelaySeconds s for the listen server to come up..."
    Start-Sleep -Seconds $ClientDelaySeconds
  }
  Write-Output "Player $($i + 1): $target"
  Start-Process -FilePath $editor -ArgumentList $argLine | Out-Null
}
