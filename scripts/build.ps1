param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [string]$WindowsSdkVersion = '')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'fetch-dependencies.ps1')
$taskCmake = Get-Command cmake -ErrorAction SilentlyContinue
if (!$taskCmake) {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $taskVsRoot = & $taskVswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $taskCmakePath = Join-Path $taskVsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
} else { $taskCmakePath = $taskCmake.Source }
$taskConfigureArgs = @('-S', $taskRoot, '-B', (Join-Path $taskRoot 'build'), '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($WindowsSdkVersion) { $taskConfigureArgs += "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion" }
& $taskCmakePath @taskConfigureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $taskCmakePath --build (Join-Path $taskRoot 'build') --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$taskCtest = Join-Path (Split-Path -Parent $taskCmakePath) 'ctest.exe'
& $taskCtest --test-dir (Join-Path $taskRoot 'build') -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
