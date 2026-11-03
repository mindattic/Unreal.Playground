# Dot-source for Find-UnrealEngine / Get-UnrealEngineVersion, or run directly to print the engine root.
# Resolution: $env:UE_ROOT, else the highest UE_5.* install in the Epic Launcher's LauncherInstalled.dat.

function Find-UnrealEngine {
  if (-not [string]::IsNullOrWhiteSpace($env:UE_ROOT)) {
    $root = $env:UE_ROOT.TrimEnd('\')
    if (-not (Test-Path -LiteralPath (Join-Path $root 'Engine\Binaries\Win64\UnrealEditor.exe'))) {
      throw "UE_ROOT is set to '$root' but Engine\Binaries\Win64\UnrealEditor.exe isn't there."
    }
    return $root
  }

  $manifest = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
  if (-not (Test-Path -LiteralPath $manifest)) {
    throw "Unreal Engine not found: $manifest doesn't exist. Install UE 5.5+ through the Epic Games Launcher, or set UE_ROOT."
  }

  $installs = @((Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList |
    Where-Object { $_.AppName -match '^UE_5\.(\d+)$' } |
    Sort-Object { [int]($_.AppName -replace '^UE_5\.', '') } -Descending)

  foreach ($install in $installs) {
    $root = ([string]$install.InstallLocation).TrimEnd('\')
    if (Test-Path -LiteralPath (Join-Path $root 'Engine\Binaries\Win64\UnrealEditor.exe')) { return $root }
  }
  throw "Unreal Engine not found: no UE_5.* entry with an UnrealEditor.exe in $manifest. Install UE 5.5+ through the Epic Games Launcher, or set UE_ROOT."
}

function Get-UnrealEngineVersion([string]$Root) {
  $buildVersion = Join-Path $Root 'Engine\Build\Build.version'
  if (Test-Path -LiteralPath $buildVersion) {
    $v = Get-Content -LiteralPath $buildVersion -Raw | ConvertFrom-Json
    return "$($v.MajorVersion).$($v.MinorVersion)"
  }
  if ((Split-Path -Leaf $Root) -match '^UE_(\d+\.\d+)$') { return $Matches[1] }
  throw "Can't tell the engine version of '$Root' (no Engine\Build\Build.version)."
}

if ($MyInvocation.InvocationName -ne '.') { Find-UnrealEngine }
