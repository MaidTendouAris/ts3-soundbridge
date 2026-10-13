param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskBin = Join-Path $taskRoot "build\$Configuration"
$taskProbe = Join-Path $taskBin 'soundbridge_probe.exe'
$taskApp = Join-Path $taskBin 'restart-tone.exe'
$taskLog = Join-Path $taskRoot 'build\restart-test.log'
$taskWatch = $null
$taskTones = @()
function Wait-TaskState([string]$Pattern) {
    $taskDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        if ((Test-Path -LiteralPath $taskLog) -and (Get-Content -LiteralPath $taskLog -Raw) -match $Pattern) { return }
        if ($taskWatch.HasExited) { throw "Watcher exited before $Pattern" }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $taskDeadline)
    throw "Timeout waiting for $Pattern"
}
try {
    Copy-Item -LiteralPath $taskProbe -Destination $taskApp -Force
    $taskWatch = Start-Process -FilePath $taskProbe -ArgumentList @('watch-selected', '12', ('"' + $taskApp + '"')) -WindowStyle Hidden -RedirectStandardOutput $taskLog -PassThru
    Wait-TaskState 'WATCH waiting'
    $taskFirst = Start-Process -FilePath $taskApp -ArgumentList @('tone', '440', '2') -WindowStyle Hidden -PassThru
    $taskTones += $taskFirst
    Wait-TaskState 'WATCH waiting\s+WATCH sharing'
    $taskFirst.WaitForExit()
    Wait-TaskState 'WATCH waiting\s+WATCH sharing\s+WATCH waiting'
    $taskTones += Start-Process -FilePath $taskApp -ArgumentList @('tone', '440', '3') -WindowStyle Hidden -PassThru
    Wait-TaskState 'WATCH waiting\s+WATCH sharing\s+WATCH waiting\s+WATCH sharing'
    $taskWatch.WaitForExit()
    $taskText = Get-Content -LiteralPath $taskLog -Raw
    Write-Output $taskText
    if ($taskWatch.ExitCode -ne 0) { throw 'Restart lifecycle test failed.' }
    Write-Output 'PASS: selected application exit/start detected and capture rebound to its new process.'
} finally {
    foreach ($taskProcess in @($taskWatch) + $taskTones) {
        if ($taskProcess) {
            if (!$taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit() }
            $taskProcess.Dispose()
        }
    }
}
