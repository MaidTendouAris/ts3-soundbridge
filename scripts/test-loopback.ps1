param([string]$Probe = (Join-Path $PSScriptRoot '..\build\Release\soundbridge_probe.exe'))
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskProbe = [IO.Path]::GetFullPath($Probe)
$taskLog = Join-Path $taskRoot 'build\loopback-test.log'
$taskErrorLog = Join-Path $taskRoot 'build\loopback-test-error.log'
# Both tones have 3% PCM amplitude. The excluded process plays 440 Hz;
# a sibling process plays 997 Hz. Neither records or saves any audio.
$taskCaptureProcess = Start-Process -FilePath $taskProbe -ArgumentList @('probe','5','0','440') -PassThru -WindowStyle Hidden -RedirectStandardOutput $taskLog -RedirectStandardError $taskErrorLog
try {
    $taskReady = $false
    for ($taskAttempt = 0; $taskAttempt -lt 70; $taskAttempt++) {
        if ((Test-Path -LiteralPath $taskLog) -and ((Get-Content -LiteralPath $taskLog -Raw) -match 'READY')) { $taskReady = $true; break }
        if ($taskCaptureProcess.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if (!$taskReady) { throw "Probe did not start. See $taskErrorLog" }
    & $taskProbe tone 997 3
    if ($LASTEXITCODE -ne 0) { throw 'Tone playback failed.' }
    $taskCaptureProcess.WaitForExit()
    $taskResult = Get-Content -LiteralPath $taskLog -Raw
    Write-Output $taskResult
    if ($taskCaptureProcess.ExitCode -ne 0) { throw 'Capture failed.' }
    $taskIncluded = [double]([regex]::Match($taskResult, 'amplitude997=([0-9.eE+-]+)').Groups[1].Value)
    $taskExcluded = [double]([regex]::Match($taskResult, 'amplitude440=([0-9.eE+-]+)').Groups[1].Value)
    if ($taskIncluded -lt 0.001) { throw 'External 997 Hz tone not detected; check default playback device/volume.' }
    if ($taskExcluded -gt $taskIncluded * 0.1) { throw '440 Hz exclusion check inconclusive/failed; other apps might be playing the same frequency.' }
    Write-Output 'PASS: external tone captured; target-process tone suppressed by at least 20 dB.'
} finally {
    # Only stop the exact helper process created by this script if still running.
    if (!$taskCaptureProcess.HasExited) { $taskCaptureProcess.Kill(); $taskCaptureProcess.WaitForExit() }
}
