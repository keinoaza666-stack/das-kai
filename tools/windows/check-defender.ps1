# SPDX-License-Identifier: AGPL-3.0-only
# Checks that Microsoft Defender lets a user download the given files, the way
# Chrome hands a finished download to Windows (see AttachmentScan.ps1), with
# the settings of a user's Windows 10/11: real-time and cloud protection on and
# samples sent to Microsoft. Run on a GitHub Windows runner, last in the job
# (it turns Defender's protection on for the rest of the job).
#
# Why: on 2026-11-11 the unsigned 0.2.0 installer was refused as
# "Trojan:Win32/Wacatac.C!ml" (cloud machine-learning verdict) on first sight
# and accepted a few minutes after the sample had been sent. So a file passes
# when it is accepted in several rounds in a row; a refusal starts the count
# again until the deadline.
#
#   pwsh tools/windows/check-defender.ps1 -Files a.exe,b.zip [-SourceBase <url>]
param(
    [Parameter(Mandatory = $true)][string[]] $Files,
    [string] $SourceBase = "https://github.com/keinoaza666-stack/das-kai/releases/download/check",
    [int] $CleanRoundsNeeded = 4,
    [int] $RoundSeconds = 40,
    [int] $DeadlineMinutes = 25
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "AttachmentScan.ps1")

Start-Service WinDefend -ErrorAction SilentlyContinue
try { Update-MpSignature -ErrorAction Stop } catch { Write-Host "Update-MpSignature: $_" }
# The runner image excludes whole drives; a scan there proves nothing.
$pref = Get-MpPreference
foreach ($path in @($pref.ExclusionPath)) { if ($path) { Remove-MpPreference -ExclusionPath $path } }
foreach ($ext in @($pref.ExclusionExtension)) { if ($ext) { Remove-MpPreference -ExclusionExtension $ext } }
foreach ($proc in @($pref.ExclusionProcess)) { if ($proc) { Remove-MpPreference -ExclusionProcess $proc } }
Set-MpPreference -MAPSReporting Advanced -SubmitSamplesConsent SendSafeSamples -DisableBlockAtFirstSeen $false -DisableIOAVProtection $false
Get-MpComputerStatus | Format-List AntivirusSignatureVersion, AntivirusSignatureLastUpdated

$dir = "C:\Users\Public\Downloads\defender-check"
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
New-Item -ItemType Directory -Path $dir | Out-Null

# Turning protection on takes a while to take effect (the first time on a
# runner, long enough that the control file below was accepted); wait until
# Defender reports it.
function Enable-Protection {
    Set-MpPreference -DisableRealtimeMonitoring $false
    $until = (Get-Date).AddMinutes(3)
    while ($true) {
        $status = Get-MpComputerStatus
        if ($status.RealTimeProtectionEnabled -and $status.IoavProtectionEnabled -and $status.OnAccessProtectionEnabled) { return }
        if ((Get-Date) -gt $until) { throw "Defender's real-time protection did not turn on within 3 minutes" }
        Start-Sleep -Seconds 2
    }
}

# Copies with protection off, so the file is first seen by the Save() call.
function Copy-Unseen([string] $source, [string] $target) {
    Set-MpPreference -DisableRealtimeMonitoring $true
    Copy-Item -Force $source $target
    Enable-Protection
}

# Control: the same path must refuse the standard harmless EICAR test file.
Set-MpPreference -DisableRealtimeMonitoring $true
[System.IO.File]::WriteAllText("$dir\control.com", 'X5O!P%@AP[4\PZX54(P^)7CC)7}$EICAR-STANDARD-' + 'ANTIVIRUS-TEST-FILE!$H+H*')
Enable-Protection
# A few tries: right after protection turns on, the first call can still pass.
$control = "0x00000000"
foreach ($try in 1..6) {
    if (-not (Test-Path "$dir\control.com")) { $control = "removed"; break }
    $control = [AttachmentScan]::Save("$dir\control.com", "https://example.com/control.com")
    if ($control -ne "0x00000000") { break }
    Start-Sleep -Seconds 10
}
Write-Host "control (EICAR test file): $control"
if ($control -eq "0x00000000") { throw "Defender accepted the control file; this check is not working" }

$state = @{}
foreach ($file in $Files) { $state[(Resolve-Path $file).Path] = 0 }
$deadline = (Get-Date).AddMinutes($DeadlineMinutes)
$round = 0
while ($true) {
    $round++
    foreach ($source in @($state.Keys)) {
        if ($state[$source] -ge $CleanRoundsNeeded) { continue }
        $name = Split-Path $source -Leaf
        $target = Join-Path $dir $name
        if (-not (Test-Path $target)) { Copy-Unseen $source $target }
        $result = [AttachmentScan]::Save($target, "$SourceBase/$name")
        if ($result -eq "0x00000000" -and (Test-Path $target)) { $state[$source]++ } else { $state[$source] = 0 }
        Write-Host "round $round $name -> $result, clean rounds in a row: $($state[$source])"
    }
    if (-not ($state.Values | Where-Object { $_ -lt $CleanRoundsNeeded })) { break }
    if ((Get-Date) -gt $deadline) { break }
    Start-Sleep -Seconds $RoundSeconds
}

$threats = Get-MpThreat | Where-Object { $_.ThreatName -notmatch "EICAR" }
if ($threats) { Write-Host "Defender reported:"; $threats | ForEach-Object { Write-Host "  $($_.ThreatName)" } }
$failed = @($state.Keys | Where-Object { $state[$_] -lt $CleanRoundsNeeded })
if ($failed.Count -gt 0) {
    Write-Host "FAILED: Defender still refuses, after $DeadlineMinutes minutes:"
    $failed | ForEach-Object { Write-Host "  - $_" }
    Write-Host "Do not publish these files. Run the check again later; see verify-map.md."
    exit 1
}
Write-Host "Defender check passed: every file was accepted $CleanRoundsNeeded rounds in a row."
exit 0
