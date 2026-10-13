$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!(Test-Path -LiteralPath (Join-Path $taskRoot '.deps\ts3-sdk\include\ts3_functions.h'))) { & (Join-Path $PSScriptRoot 'fetch-sdk.ps1') }
$taskHeader = Join-Path $taskRoot '.deps\json\nlohmann\json.hpp'
$taskHash = 'aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63'
if (!(Test-Path -LiteralPath $taskHeader)) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskHeader) -Force | Out-Null
    Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp' -OutFile $taskHeader
}
if ((Get-FileHash -LiteralPath $taskHeader -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskHash) { throw 'nlohmann JSON 3.12.0 checksum mismatch.' }
