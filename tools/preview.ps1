[CmdletBinding()]
param(
  # The run's start: only files written since then go into the set.
  [Parameter(Mandatory)][datetime]$Since,
  [Parameter(Mandatory)][string]$Title,
  [string]$Ticket = '',
  # The game's Saved folder; its Screenshots and WeatherShots stand in when the run recorded no stills.
  [Parameter(Mandatory)][string]$SavedDir,
  # The run recorded itself (-capture video): wait up to WaitSeconds for the agent to export its video.
  [switch]$Recorded,
  [int]$WaitSeconds = 120,
  # Make and fill the set but don't open it in MindAttic.Media.Viewer.
  [switch]$NoLaunch
)

# UNR-T124: gathers a run's screenshots and clips into a MindAttic.Media.Capture preview set and opens it in
# MindAttic.Media.Viewer. Every folder comes from the agent: the set's from `capture set new --json`, and the
# recording's is the sibling of the preview root named Capture (R:\MindAttic\Media\{Preview,Capture}\<repo>).

$ErrorActionPreference = 'Stop'
$repoName = 'Unreal.Playground'
$capture = Join-Path $env:LOCALAPPDATA 'MindAttic\Capture\bin\capture.exe'
if (-not (Test-Path -LiteralPath $capture)) { throw "MindAttic.Media.Capture isn't installed ($capture); run D:\Projects\MindAttic\MindAttic.Media.Capture\run.ps1." }

$newArgs = @('set', 'new', '--title', $Title, '--repo', $repoName, '--json')
if ($Ticket) { $newArgs += @('--ticket', $Ticket) }
$set = (& $capture @newArgs | Out-String) | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or -not $set.dir) { throw "capture set new failed (exit $LASTEXITCODE)." }
$setDir = $set.dir

$mediaRoot = Split-Path -Parent (Split-Path -Parent $setDir)
$sessionDir = Join-Path (Join-Path (Split-Path -Parent $mediaRoot) 'Capture') $repoName
$stills = '.png', '.jpg', '.jpeg', '.webp', '.bmp'
$clips = '.mp4', '.gif', '.webm'
function Get-NewMedia([string]$dir, [string[]]$extensions, [switch]$Recurse) {
  if (-not (Test-Path -LiteralPath $dir)) { return @() }
  @(Get-ChildItem -LiteralPath $dir -File -Recurse:$Recurse |
      Where-Object { $_.LastWriteTime -ge $Since -and $extensions -contains $_.Extension.ToLowerInvariant() -and $_.Name -notlike '*.thumb.*' } |
      Sort-Object LastWriteTime)
}

# The host exports the session detached once the game is gone, so its video shows up a little after the quit.
$deadline = (Get-Date).AddSeconds($WaitSeconds)
$video = Get-NewMedia $sessionDir $clips
while ($Recorded -and -not $video -and (Get-Date) -lt $deadline) {
  Start-Sleep -Seconds 2
  $video = Get-NewMedia $sessionDir $clips
}
$files = @(Get-NewMedia $sessionDir $stills)
if (-not $files) { $files = @('Screenshots', 'WeatherShots' | ForEach-Object { Get-NewMedia (Join-Path $SavedDir $_) $stills -Recurse }) }
$files += $video

if (-not $files) { Write-Warning "Nothing was shot or recorded since $Since; the set $setDir is empty." }
foreach ($file in $files) {
  & $capture set add $setDir $file.FullName --caption $file.BaseName --copy | Out-Null
  if ($LASTEXITCODE -ne 0) { Write-Warning "capture set add failed for $($file.FullName) (exit $LASTEXITCODE)." }
}
Write-Output "Preview set ($($files.Count) items): $setDir"

$showArgs = @('set', 'show', $setDir)
if ($NoLaunch) { $showArgs += '--no-launch' }
& $capture @showArgs | Out-Null
$setDir
