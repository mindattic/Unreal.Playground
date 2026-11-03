[CmdletBinding()]
param(
  [int]$Seconds = 60,
  [string]$BuildsDir
)

# Runs both packaged builds (tools/package.ps1) one at a time with the same unattended benchmark,
# counts gameplay lines of code per language, reads Builds/build-times.json and writes
# docs/COMPARISON.md (UNR-T8).
#   Unreal.Soulslike.Cpp.exe    -> benchmark-cpp.json
#   Unreal.Soulslike.CSharp.exe -> benchmark-csharp.json

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildsDir)) { $BuildsDir = Join-Path $repo 'Builds' }
$reportPath = Join-Path $repo 'docs\COMPARISON.md'
$runArgs = "-DemoBot -Benchmark=$Seconds -windowed -ResX=1280 -ResY=720"

$builds = @(
  @{ Key = 'cpp'; Label = 'C++'; Exe = 'Unreal.Soulslike.Cpp.exe'; Dir = (Join-Path $BuildsDir 'Cpp') },
  @{ Key = 'csharp'; Label = 'C# (UnrealSharp)'; Exe = 'Unreal.Soulslike.CSharp.exe'; Dir = (Join-Path $BuildsDir 'CSharp') }
)

# Development builds keep Saved next to the game; Shipping builds write to %LOCALAPPDATA%\<Project>\Saved.
function Find-BenchmarkFile([string]$ArchiveDir, [string]$Key, [datetime]$Since) {
  $name = "benchmark-$Key.json"
  $hits = @()
  if (Test-Path -LiteralPath $ArchiveDir) {
    $hits += @(Get-ChildItem -LiteralPath $ArchiveDir -Recurse -Filter $name -File -ErrorAction SilentlyContinue)
  }
  $hits += @(Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "*\Saved\Benchmark\$name") -File -ErrorAction SilentlyContinue)
  $fresh = @($hits | Where-Object { $_.LastWriteTime -ge $Since } | Sort-Object LastWriteTime -Descending)
  if ($fresh.Count -gt 0) { return $fresh[0].FullName }
  return $null
}

$results = @{}
foreach ($build in $builds) {
  $exe = @(Get-ChildItem -LiteralPath $build.Dir -Recurse -Filter $build.Exe -File -ErrorAction SilentlyContinue | Select-Object -First 1)
  if ($exe.Count -eq 0) {
    Write-Warning "$($build.Exe) not found under $($build.Dir); run tools/package.ps1 first. Skipping."
    continue
  }

  Write-Output "== Running $($build.Exe) $runArgs"
  $started = Get-Date
  $process = Start-Process -FilePath $exe[0].FullName -ArgumentList $runArgs -PassThru
  $timeoutMs = ($Seconds + 180) * 1000
  if (-not $process.WaitForExit($timeoutMs)) {
    Stop-Process -Id $process.Id -Force
    Write-Warning "$($build.Exe) didn't exit within $($Seconds + 180) s; killed it."
    continue
  }

  $file = Find-BenchmarkFile $build.Dir $build.Key $started
  if (-not $file) {
    Write-Warning "$($build.Exe) exited (code $($process.ExitCode)) without writing benchmark-$($build.Key).json."
    continue
  }
  $results[$build.Key] = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
  Write-Output "   $file"
}

# Gameplay lines of code: non-blank lines, generated files excluded.
function Measure-Lines([string[]]$Files) {
  $total = 0
  foreach ($file in $Files) {
    $total += @(Get-Content -LiteralPath $file | Where-Object { $_.Trim().Length -gt 0 }).Count
  }
  return $total
}

$cppSource = Join-Path $repo 'Source\UnrealSoulslike'
$csSource = Join-Path $repo 'CSharp\Script\ManagedUnrealSoulslikeCSharp'
$cppFiles = @(Get-ChildItem -LiteralPath $cppSource -Recurse -Include '*.h', '*.cpp' -File | Where-Object { $_.Name -notlike '*.generated.h' } | ForEach-Object FullName)
$csFiles = @(Get-ChildItem -LiteralPath $csSource -Recurse -Filter '*.cs' -File | Where-Object { $_.FullName -notmatch '\\(bin|obj)\\' } | ForEach-Object FullName)
$csStubs = @($csFiles | Where-Object { $_ -like '*.TODO.cs' })
$csPorted = @($csFiles | Where-Object { $_ -notlike '*.TODO.cs' })
$nativeShim = @(Get-ChildItem -LiteralPath (Join-Path $repo 'CSharp\Source\UnrealSoulslikeCSharp') -Recurse -Include '*.h', '*.cpp' -File | ForEach-Object FullName)

# Like-for-like: the C++ files that have a finished C# port (see CSharp/PORT_STATUS.md).
$portedStems = @('SoulslikeTypes', 'SoulslikeData', 'VitalityComponent', 'CombatComponent', 'SoulslikeCombatant',
  'SkeletonEnemy', 'SoulslikeGameMode', 'SoulslikePlayerController', 'SoulslikeHUD')
$cppPorted = @($cppFiles | Where-Object { $portedStems -contains [IO.Path]::GetFileNameWithoutExtension($_) })

$loc = [ordered]@{
  cpp_all = Measure-Lines $cppFiles
  cpp_ported_subset = Measure-Lines $cppPorted
  csharp_ported = Measure-Lines $csPorted
  csharp_stubs = Measure-Lines $csStubs
  csharp_native_shim = Measure-Lines $nativeShim
}

$times = $null
$timesPath = Join-Path $BuildsDir 'build-times.json'
if (Test-Path -LiteralPath $timesPath) { $times = Get-Content -LiteralPath $timesPath -Raw | ConvertFrom-Json }

function Format-Value($Object, [string]$Field) {
  if ($null -eq $Object) { return 'n/a' }
  $value = $Object.$Field
  if ($null -eq $value) { return 'n/a' }
  return [string]$value
}

function Format-Delta($Cpp, $CSharp, [string]$Field) {
  if ($null -eq $Cpp -or $null -eq $CSharp) { return '' }
  $a = [double]$Cpp.$Field
  $b = [double]$CSharp.$Field
  if ($a -eq 0) { return '' }
  return ('{0:+0.0;-0.0;0.0}%' -f ((($b - $a) / $a) * 100.0))
}

$cpp = $results['cpp']
$cs = $results['csharp']
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# C++ vs C# (UnrealSharp): measured comparison')
$lines.Add('')
$lines.Add("Generated by ``tools/compare.ps1`` on $((Get-Date).ToString('yyyy-MM-dd HH:mm')). Each build ran unattended with ``$runArgs`` on the same machine, one at a time.")
$lines.Add('')
if ($csStubs.Count -gt 0) {
  $lines.Add("> **The C# port is still incomplete** ($($csStubs.Count) stub file(s) in ``CSharp/Script/.../Todo``, see ``CSharp/PORT_STATUS.md``). Until it's finished, the two builds don't run the same game, so the runtime numbers below aren't a like-for-like comparison yet.")
  $lines.Add('')
}
$lines.Add('## Runtime (-DemoBot benchmark)')
$lines.Add('')
$lines.Add('| Metric | C++ | C# | C# vs C++ |')
$lines.Add('|---|---|---|---|')
foreach ($row in @(
    @('frames', 'Frames sampled'), @('avg_frame_ms', 'Avg frame (ms)'), @('p50_frame_ms', 'Median frame (ms)'),
    @('p99_frame_ms', 'p99 frame (ms)'), @('avg_fps', 'Avg FPS'), @('one_percent_low_fps', '1% low FPS'),
    @('avg_game_thread_ms', 'Avg game thread (ms)'), @('p99_game_thread_ms', 'p99 game thread (ms)'),
    @('peak_used_physical_mb', 'Peak memory (MB)'))) {
  $lines.Add("| $($row[1]) | $(Format-Value $cpp $row[0]) | $(Format-Value $cs $row[0]) | $(Format-Delta $cpp $cs $row[0]) |")
}
$lines.Add('')
$lines.Add('## Build (tools/package.ps1, seconds)')
$lines.Add('')
$lines.Add('| Step | C++ | C# |')
$lines.Add('|---|---|---|')
$cppTimes = $null
$csTimes = $null
if ($times) { $cppTimes = $times.cpp; $csTimes = $times.csharp }
foreach ($step in @('mirror_content', 'build_cook_package', 'package_managed')) {
  $lines.Add("| $step | $(Format-Value $cppTimes $step) | $(Format-Value $csTimes $step) |")
}
if ($times) { $lines.Add(''); $lines.Add("Engine $($times.engine), $($times.config) configuration.") }
$lines.Add('')
$lines.Add('## Code size (non-blank lines)')
$lines.Add('')
$lines.Add('| | Lines |')
$lines.Add('|---|---|')
$lines.Add("| C++ gameplay, all files | $($loc.cpp_all) |")
$lines.Add("| C++, the files already ported to C# | $($loc.cpp_ported_subset) |")
$lines.Add("| C#, ported files | $($loc.csharp_ported) |")
$lines.Add("| C#, TODO stubs (not comparable) | $($loc.csharp_stubs) |")
$lines.Add("| C++ inside the C# build (stub module + reflection-gap library) | $($loc.csharp_native_shim) |")
$lines.Add('')
$lines.Add('## Reading the numbers')
$lines.Add('')
$lines.Add('- **The same in both builds by construction:** rendering, physics, animation, networking, audio, the assets and the map. Both builds run on the same engine binaries. The only difference is the language the gameplay code is written in, plus UnrealSharp''s interop layer and its .NET runtime.')
$lines.Add('- **Frame time and game-thread time** measure the gameplay code and its native calls. In C#, each call into the engine goes through UnrealSharp''s marshalling, and actor queries allocate lists. In this game the AI is the main per-frame cost.')
$lines.Add('- **Memory** includes the .NET runtime that UnrealSharp loads, plus any garbage the C# code leaves between collections.')
$lines.Add('- **Build times** cover a clean package. They don''t measure iteration: C# hot-reloads in the editor, while C++ uses Live Coding or a rebuild. Time a one-line change separately if that matters for the decision.')
if ($cpp -and $cs) {
  $frame = Format-Delta $cpp $cs 'avg_frame_ms'
  $gt = Format-Delta $cpp $cs 'avg_game_thread_ms'
  $mem = Format-Delta $cpp $cs 'peak_used_physical_mb'
  $lines.Add("- **This run:** average frame time $frame, game-thread time $gt, peak memory $mem, for C# relative to C++.")
}
$lines.Add('')

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportPath) | Out-Null
[IO.File]::WriteAllText($reportPath, ($lines -join "`n"), [Text.UTF8Encoding]::new($false))
Write-Output "Wrote $reportPath"
