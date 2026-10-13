param([string]$Destination = (Join-Path $PSScriptRoot '..\.deps\ts3-sdk'))
$ErrorActionPreference = 'Stop'
# API 26. Never track a moving branch for ABI-sensitive function-table headers.
$taskSdkCommit = '4aa90a53aa150cbf81e13bc97e68c0431b26499f'
$taskTree = Invoke-RestMethod -Uri "https://api.github.com/repos/teamspeak/ts3client-pluginsdk/git/trees/${taskSdkCommit}?recursive=1"
foreach ($taskEntry in $taskTree.tree) {
    if ($taskEntry.path.StartsWith('include/') -and $taskEntry.type -eq 'blob') {
        $taskOutput = Join-Path $Destination $taskEntry.path
        New-Item -ItemType Directory -Path (Split-Path -Parent $taskOutput) -Force | Out-Null
        Invoke-WebRequest -Uri ("https://raw.githubusercontent.com/teamspeak/ts3client-pluginsdk/${taskSdkCommit}/" + $taskEntry.path) -OutFile $taskOutput
    }
}
Set-Content -LiteralPath (Join-Path $Destination 'SDK-COMMIT.txt') -Value $taskSdkCommit -Encoding ascii
Write-Output "TeamSpeak SDK API 26 ready: $Destination"
