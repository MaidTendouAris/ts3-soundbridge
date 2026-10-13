param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskDll = Join-Path $taskRoot "build\$Configuration\ts3_soundbridge_win64.dll"
if (!(Test-Path -LiteralPath $taskDll)) { throw 'Build the plugin first with scripts/build.ps1.' }
$taskDist = Join-Path $taskRoot 'dist'
New-Item -ItemType Directory -Path $taskDist -Force | Out-Null
$taskMetadata = Get-Content -LiteralPath (Join-Path $taskRoot 'packaging\package.ini') -Raw
$taskVersion = [regex]::Match($taskMetadata, '(?m)^Version\s*=\s*([0-9.]+)').Groups[1].Value
if (!$taskVersion) { throw 'Missing package version.' }
$taskDllVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($taskDll).ProductVersion
if ($taskDllVersion -ne $taskVersion) { throw "DLL version $taskDllVersion differs from package version $taskVersion." }
$taskPackage = Join-Path $taskDist "TS3-SoundBridge-$taskVersion-win64.ts3_plugin"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskStream = [IO.File]::Open($taskPackage, [IO.FileMode]::Create)
$taskArchive = [IO.Compression.ZipArchive]::new($taskStream, [IO.Compression.ZipArchiveMode]::Create)
try {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskArchive, (Join-Path $taskRoot 'packaging\package.ini'), 'package.ini', [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskArchive, $taskDll, 'plugins/ts3_soundbridge_win64.dll', [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    $taskResources = @{
        'build\icons\icon-32.png' = 'icon-32.png'
        'build\icons\icon-256.png' = 'icon-256.png'
        'assets\icon.svg' = 'icon.svg'
        'LICENSE' = 'LICENSE.txt'
        'THIRD_PARTY_NOTICES.txt' = 'THIRD_PARTY_NOTICES.txt'
    }
    foreach ($taskSource in $taskResources.Keys | Sort-Object) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskArchive, (Join-Path $taskRoot $taskSource), ('plugins/ts3_soundbridge_win64/' + $taskResources[$taskSource]), [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $taskArchive.Dispose(); $taskStream.Dispose() }
$taskCheck = [IO.Compression.ZipFile]::OpenRead($taskPackage)
try {
    if ($taskCheck.Entries.Count -ne 7 -or !$taskCheck.GetEntry('package.ini') -or !$taskCheck.GetEntry('plugins/ts3_soundbridge_win64.dll') -or !$taskCheck.GetEntry('plugins/ts3_soundbridge_win64/icon-32.png') -or !$taskCheck.GetEntry('plugins/ts3_soundbridge_win64/LICENSE.txt')) { throw 'Unexpected package structure.' }
} finally { $taskCheck.Dispose() }
$taskHash = (Get-FileHash -LiteralPath $taskPackage -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $taskDist 'SHA256SUMS.txt') -Value "$taskHash  $([IO.Path]::GetFileName($taskPackage))" -Encoding ascii
Write-Output "Package ready: $taskPackage"
