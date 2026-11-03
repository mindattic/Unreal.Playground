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

  $candidates = @()
  $manifest = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
  if (Test-Path -LiteralPath $manifest) {
    $candidates += @((Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList |
      Where-Object { $_.AppName -match '^UE_5\.\d+$' } | ForEach-Object { [string]$_.InstallLocation })
  }
  # Engines registered as custom builds (e.g. installed outside the Launcher's list).
  $builds = 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
  if (Test-Path $builds) {
    $item = Get-ItemProperty $builds
    $candidates += @($item.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | ForEach-Object { [string]$_.Value })
  }

  $found = @($candidates | ForEach-Object { $_.Replace('/', '\').TrimEnd('\') } |
    Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Engine\Binaries\Win64\UnrealEditor.exe')) } |
    Select-Object -Unique |
    Sort-Object { [version]((Get-UnrealEngineVersion $_) + '.0') } -Descending)
  if ($found.Count -gt 0) { return $found[0] }
  throw "Unreal Engine not found in the Epic Launcher's install list or the registered custom builds. Install UE 5.6-5.8, or set UE_ROOT."
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
