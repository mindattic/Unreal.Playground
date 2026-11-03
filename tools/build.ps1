[CmdletBinding()]
param(
  [switch]$SkipContent
)

# Compiles the UnrealSoulslikeEditor target, then imports SourceArt and builds the map via tools/build_content.py.

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
  Write-Output 'Skipping content import (-SkipContent).'
  return
}

$script = Join-Path $PSScriptRoot 'build_content.py'
if ($script -match '\s') { throw "build_content.py path contains whitespace; the commandlet's -script argument can't take it: $script" }
$editorCmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
Write-Output 'Importing content and building the map (pythonscript commandlet)...'
& $editorCmd $uproject -run=pythonscript "-script=$script" -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput | Tee-Object -Variable captured
$exit = $LASTEXITCODE
$output = ($captured | Out-String)
if ($exit -ne 0) { throw "Content commandlet failed (exit $exit)." }
if ($output -notmatch 'BUILD_CONTENT_OK') {
  throw 'Content commandlet finished without BUILD_CONTENT_OK; see the LogPython errors above.'
}
$warnings = @($captured | Where-Object { $_ -match '\[build_content\]' -and $_ -match 'Warning' })
if ($warnings.Count -gt 0) { Write-Warning "$($warnings.Count) content warning(s); see [build_content] lines above." }
Write-Output 'Build complete.'
