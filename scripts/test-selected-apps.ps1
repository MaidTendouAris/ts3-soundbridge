param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskBin = Join-Path $taskRoot "build\$Configuration"
$taskProbe = Join-Path $taskBin 'soundbridge_probe.exe'
if (!(Test-Path -LiteralPath $taskProbe)) { throw 'Build first.' }
$taskHelpers = @()
try {
    $taskFrequencies = @(440, 997, 1499)
    $taskPaths = @()
    for ($taskIndex = 0; $taskIndex -lt 3; ++$taskIndex) {
        $taskPath = Join-Path $taskBin "selected-tone-$taskIndex.exe"
        Copy-Item -LiteralPath $taskProbe -Destination $taskPath -Force
        $taskPaths += $taskPath
        $taskHelpers += Start-Process -FilePath $taskPath -ArgumentList @('tone', $taskFrequencies[$taskIndex], '10') -WindowStyle Hidden -PassThru
    }
    $taskOutput = & $taskProbe selected 6 $taskPaths[0] $taskPaths[1]
    if ($LASTEXITCODE -ne 0) { throw "Selected capture failed: $taskOutput" }
    $taskText = $taskOutput -join "`n"
    Write-Output $taskText
    $taskAmplitudes = @()
    foreach ($taskFrequency in $taskFrequencies) {
        $taskMatch = [regex]::Match($taskText, "amplitude$taskFrequency=([0-9.eE+-]+)")
        if (!$taskMatch.Success) { throw "Missing amplitude$taskFrequency" }
        $taskAmplitudes += [double]::Parse($taskMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    }
    if ($taskAmplitudes[0] -lt 0.003 -or $taskAmplitudes[1] -lt 0.003 -or $taskAmplitudes[2] -gt [Math]::Min($taskAmplitudes[0], $taskAmplitudes[1]) * 0.1) {
        throw 'Selected tones must both pass, and the unselected tone must be below 10% of either selected tone.'
    }
    Write-Output 'PASS: two selected applications captured together; the third application excluded.'
} finally {
    foreach ($taskHelper in $taskHelpers) {
        if (!$taskHelper.HasExited) { $taskHelper.Kill(); $taskHelper.WaitForExit() }
        $taskHelper.Dispose()
    }
}
