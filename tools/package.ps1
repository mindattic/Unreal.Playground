[CmdletBinding()]
param(
  [ValidateSet('Both', 'Cpp', 'CSharp')]
  [string]$Only = 'Both',
  [ValidateSet('Shipping', 'Development')]
  [string]$Config = 'Shipping'
)

# Packages both builds of the game for a side-by-side comparison (UNR-T8):
#   Builds\Cpp\Windows\Unreal.Playground.Cpp.exe       - the C++ project (UnrealSoulslike.uproject)
#   Builds\CSharp\Windows\Unreal.Playground.CSharp.exe - the UnrealSharp twin (CSharp\UnrealSoulslikeCSharp.uproject)
# and records how long every step took in Builds\build-times.json (merged, so -Only keeps the other side).
#
# UE target names can't contain dots, so each build's root launcher exe is renamed after packaging.
# The C# build needs UnrealSharp's PackageProject step after BuildCookRun to publish the managed code;
# the rename waits until that step is done (it looks the exe up by its original name).

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'find-engine.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$engine = Find-UnrealEngine
$version = Get-UnrealEngineVersion $engine
$runUat = Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat'
$buildsDir = Join-Path $repo 'Builds'
$timesPath = Join-Path $buildsDir 'build-times.json'
New-Item -ItemType Directory -Force -Path $buildsDir | Out-Null
Write-Output "Engine: $engine (UE $version), config $Config"

$times = [ordered]@{}
if (Test-Path -LiteralPath $timesPath) {
  $previous = Get-Content -LiteralPath $timesPath -Raw | ConvertFrom-Json
  foreach ($property in $previous.PSObject.Properties) {
    if ($property.Value -is [System.Management.Automation.PSCustomObject]) {
      $steps = [ordered]@{}
      foreach ($step in $property.Value.PSObject.Properties) { $steps[$step.Name] = $step.Value }
      $times[$property.Name] = $steps
    } else {
      $times[$property.Name] = $property.Value
    }
  }
}

function Invoke-TimedStep([string]$Language, [string]$Step, [scriptblock]$Body) {
  Write-Output "== [$Language] $Step"
  $watch = [Diagnostics.Stopwatch]::StartNew()
  & $Body
  $watch.Stop()
  $seconds = [math]::Round($watch.Elapsed.TotalSeconds, 1)
  if (-not $times.Contains($Language)) { $times[$Language] = [ordered]@{} }
  $times[$Language][$Step] = $seconds
  Write-Output "   $Step took $seconds s"
}

function Set-EngineAssociation([string]$Uproject) {
  $text = [IO.File]::ReadAllText($Uproject)
  $pattern = '("EngineAssociation"\s*:\s*")([^"]*)(")'
  $current = [regex]::Match($text, $pattern).Groups[2].Value
  if ($current -ne $version) {
    $text = [regex]::Replace($text, $pattern, "`${1}$version`${3}")
    [IO.File]::WriteAllText($Uproject, $text, [Text.UTF8Encoding]::new($false))
    Write-Output "EngineAssociation in $(Split-Path -Leaf $Uproject): $current -> $version"
  }
}

function Invoke-BuildCookRun([string]$Uproject, [string]$ArchiveDir) {
  & $runUat BuildCookRun "-project=$Uproject" -noP4 -utf8output -platform=Win64 "-clientconfig=$Config" -build -cook -stage -pak -archive "-archivedirectory=$ArchiveDir"
  if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed for $(Split-Path -Leaf $Uproject) (exit $LASTEXITCODE)." }
}

# BuildCookRun archives into <dir>\Windows on UE5.
function Get-ArchiveRoot([string]$ArchiveDir) {
  foreach ($leaf in @('Windows', 'Win64', 'WindowsNoEditor')) {
    $candidate = Join-Path $ArchiveDir $leaf
    if (Test-Path -LiteralPath $candidate) { return $candidate }
  }
  return $ArchiveDir
}

function Rename-LauncherExe([string]$ArchiveRoot, [string]$NewName) {
  $target = Join-Path $ArchiveRoot $NewName
  $launchers = @(Get-ChildItem -LiteralPath $ArchiveRoot -Filter '*.exe' -File | Where-Object { $_.Name -ne $NewName })
  if ($launchers.Count -eq 0) {
    if (Test-Path -LiteralPath $target) { Write-Host "   $NewName already in place"; return $target }
    throw "No launcher exe found in $ArchiveRoot."
  }
  if ($launchers.Count -gt 1) { throw "Expected one launcher exe in $ArchiveRoot, found: $($launchers.Name -join ', ')." }
  if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
  Rename-Item -LiteralPath $launchers[0].FullName -NewName $NewName
  Write-Host "   $($launchers[0].Name) -> $NewName"
  return $target
}

if ($Only -ne 'CSharp') {
  $uproject = Join-Path $repo 'UnrealSoulslike.uproject'
  $archiveDir = Join-Path $buildsDir 'Cpp'
  Set-EngineAssociation $uproject
  Invoke-TimedStep 'cpp' 'build_cook_package' { Invoke-BuildCookRun $uproject $archiveDir }
  $exe = Rename-LauncherExe (Get-ArchiveRoot $archiveDir) 'Unreal.Playground.Cpp.exe'
  Write-Output "C++ build: $exe"
}

if ($Only -ne 'Cpp') {
  $csharpDir = Join-Path $repo 'CSharp'
  $uproject = Join-Path $csharpDir 'UnrealSoulslikeCSharp.uproject'
  $pluginDir = Join-Path $csharpDir 'Plugins\UnrealSharp'
  $archiveDir = Join-Path $buildsDir 'CSharp'
  if (-not (Test-Path -LiteralPath (Join-Path $pluginDir 'UnrealSharp.uplugin'))) {
    throw "UnrealSharp submodule missing at $pluginDir. Run: git submodule update --init CSharp/Plugins/UnrealSharp"
  }
  Set-EngineAssociation $uproject

  # One source of truth for assets and data files: the C++ project's Content.
  Invoke-TimedStep 'csharp' 'mirror_content' {
    robocopy (Join-Path $repo 'Content') (Join-Path $csharpDir 'Content') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy Content mirror failed (exit $LASTEXITCODE)." }
    $global:LASTEXITCODE = 0
  }

  Invoke-TimedStep 'csharp' 'build_cook_package' { Invoke-BuildCookRun $uproject $archiveDir }

  $archiveRoot = Get-ArchiveRoot $archiveDir
  Invoke-TimedStep 'csharp' 'package_managed' {
    & $runUat PackageProject "-ScriptDir=$csharpDir" "-ScriptsForProject=$uproject" "-Project=$uproject" "-ArchiveDirectory=$archiveRoot" -UETargetType=Game "-UEBuildConfig=$Config"
    if ($LASTEXITCODE -ne 0) { throw "UnrealSharp PackageProject failed (exit $LASTEXITCODE)." }
  }

  $exe = Rename-LauncherExe $archiveRoot 'Unreal.Playground.CSharp.exe'
  Write-Output "C# build: $exe"
}

$times['engine'] = "UE $version"
$times['config'] = $Config
$times['updated'] = (Get-Date).ToString('s')
[IO.File]::WriteAllText($timesPath, ($times | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
Write-Output "Build times: $timesPath"
