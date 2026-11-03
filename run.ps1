[CmdletBinding()]
param(
  [ValidateRange(1, 4)][int]$Players = 1,
  [switch]$SkipContent,
  [string]$Extra = ''
)

# Build (compile + content import) then launch the demo. -Players 2..4 runs local co-op over a listen server.

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'tools\build.ps1') -SkipContent:$SkipContent
& (Join-Path $PSScriptRoot 'tools\run.ps1') -Players $Players -Extra $Extra
