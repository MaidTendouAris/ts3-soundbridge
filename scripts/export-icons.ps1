param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskTool = Join-Path $taskRoot "build\$Configuration\soundbridge_icon_tool.exe"
if (!(Test-Path -LiteralPath $taskTool)) { throw 'Run scripts/build.ps1 first.' }
& $taskTool (Join-Path $taskRoot 'assets\icon.svg') (Join-Path $taskRoot 'build\icons')
if ($LASTEXITCODE -ne 0) { throw 'SVG rendering failed.' }
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\icons\icon-256.png') -Destination (Join-Path $taskRoot 'assets\icon-256.png') -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\icons\soundbridge.ico') -Destination (Join-Path $taskRoot 'assets\soundbridge.ico') -Force
