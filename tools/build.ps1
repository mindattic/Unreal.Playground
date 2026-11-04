[CmdletBinding()]
param(
  [switch]$SkipContent,
  [switch]$LegacyContent
)

# Compiles the UnrealSoulslikeEditor target, installs Epic's Third Person template content if it's
# missing, then builds the sword knight (retargeted animations, weapons, blueprints) in the editor.
# -LegacyContent also runs the old KayKit/island import (tools/build_content.py).

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'find-engine.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $repo 'Unreal.Soulslike.uproject'
$engine = Find-UnrealEngine
$version = Get-UnrealEngineVersion $engine
Write-Output "Engine: $engine (UE $version)"

# Point the project at the installed engine so it doesn't prompt to switch versions.
$text = [IO.File]::ReadAllText($uproject)
$pattern = '("EngineAssociation"\s*:\s*")([^"]*)(")'
$current = [regex]::Match($text, $pattern).Groups[2].Value
if ($current -ne $version) {
  $text = [regex]::Replace($text, $pattern, "`${1}$version`${3}")
  [IO.File]::WriteAllText($uproject, $text, [Text.UTF8Encoding]::new($false))
  Write-Output "EngineAssociation: $current -> $version"
}

$buildBat = Join-Path $engine 'Engine\Build\BatchFiles\Build.bat'
Write-Output "Compiling UnrealSoulslikeEditor Win64 Development..."
& $buildBat UnrealSoulslikeEditor Win64 Development "-Project=$uproject" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "Build.bat failed (exit $LASTEXITCODE)." }

if ($SkipContent) {
  Write-Output 'Skipping content (-SkipContent).'
  return
}

$editorCmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (-not (Test-Path -LiteralPath (Join-Path $repo 'Content\Variant_Combat\Lvl_Combat.umap'))) {
  Write-Output 'Installing the Third Person template content...'
  & python -I (Join-Path $PSScriptRoot 'install_thirdperson_template.py') $engine
  if ($LASTEXITCODE -ne 0) { throw "install_thirdperson_template.py failed (exit $LASTEXITCODE)." }
}

function Invoke-EditorScript([string]$Script, [string]$LogFile, [string]$OkMarker) {
  Write-Output "Running $([IO.Path]::GetFileName($Script)) in the editor..."
  & $editorCmd $uproject -run=pythonscript "-script=$Script" -unattended -nop4 -nosplash | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "$Script failed (exit $LASTEXITCODE)." }
  $log = Get-Content -LiteralPath $LogFile -Raw
  if ($log -notmatch $OkMarker) { throw "$Script did not finish ($OkMarker missing); see $LogFile" }
  ($log -split "`n") | Where-Object { $_ -match 'WARNING' } | ForEach-Object { Write-Warning $_ }
}

Invoke-EditorScript (Join-Path $PSScriptRoot 'build_souls_character.py') (Join-Path $repo 'Saved\souls_character.log') 'SOULS_CHARACTER_OK'
# The Shade of the Fen (second boss): needs the knight's montages, weapons, audio and game mode above.
Invoke-EditorScript (Join-Path $PSScriptRoot 'build_shadow_boss.py') (Join-Path $repo 'Saved\shadow_boss.log') 'SHADOW_BOSS_OK'

if ($LegacyContent) {
  $script = Join-Path $PSScriptRoot 'build_content.py'
  Write-Output 'Importing legacy KayKit content and the island map...'
  & $editorCmd $uproject -run=pythonscript "-script=$script" -unattended -nop4 -nosplash | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "build_content.py failed (exit $LASTEXITCODE)." }
}
Write-Output 'Build complete.'
