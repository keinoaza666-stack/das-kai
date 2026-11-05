# SPDX-License-Identifier: AGPL-3.0-only
# Checks the DAS Kai Windows installer the way a user meets it, on a clean
# Windows (a Windows Server container with no Visual C++ runtime and no
# developer tools): install silently, check every installed file, load the
# installed VST3 with the smoke test (it must be created, report the right
# name and vendor, and its audio must reach the OBS-side reader), then
# uninstall and check that everything is gone. Adapted from Kinu Tune's
# tools/windows/check-installer.ps1 (2026-11-05).
#
#   pwsh tools/windows/check-installer.ps1 -Setup <Setup.exe> -SmokeTest <das_kai_vst3_smoke .exe>
param(
    [Parameter(Mandatory = $true)][string] $Setup,
    [Parameter(Mandatory = $true)][string] $SmokeTest
)

$ErrorActionPreference = "Stop"
$Setup = (Resolve-Path $Setup).Path
$SmokeTest = (Resolve-Path $SmokeTest).Path

$stage = "C:\daskai-installer"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $Setup (Join-Path $stage "setup.exe")
Copy-Item $SmokeTest (Join-Path $stage "vst3_smoke.exe")

# The script that runs inside the container. Written with a BOM: the container
# runs Windows PowerShell 5.1, which reads a UTF-8 file without one as the ANSI
# code page and breaks on the Japanese file name below.
@'
$ErrorActionPreference = "Stop"
$failures = @()
$vst3 = "C:\Program Files\Common Files\VST3\DAS Kai.vst3"
$obs = "C:\ProgramData\obs-studio\plugins\das-kai-obs"
$appDir = "C:\Program Files\Keinoaza\DAS Kai"
$uninstaller = "$appDir\unins000.exe"
$uninstallKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{230D027E-4530-4AC3-900E-DDAEAEC78284}_is1"
Write-Host "Windows $([System.Environment]::OSVersion.Version)"
# The plug-ins link the C++ runtime statically; on a machine without the
# Visual C++ Redistributable a missing runtime shows up as a load failure.
if (Test-Path (Join-Path $env:SystemRoot "System32\msvcp140.dll")) {
    Write-Host "This image already has the Visual C++ runtime; it is not a clean machine."
    exit 1
}
Write-Host "Before: no Visual C++ runtime in System32"

$run = Start-Process C:\daskai\setup.exe -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/LOG=C:\daskai\setup.log" -Wait -PassThru
Write-Host "setup.exe -> exit code $($run.ExitCode)"
if ($run.ExitCode -ne 0) { $failures += "the installer exited with $($run.ExitCode)" }

foreach ($path in "$vst3\Contents\x86_64-win\DAS Kai.vst3", "$obs\bin\64bit\das-kai-obs.dll",
                  "$obs\data\locale\ja-JP.ini", "$obs\data\locale\en-US.ini",
                  "$appDir\README_はじめにお読みください.txt", "$appDir\SOURCE.txt",
                  "$appDir\licenses\LICENSE", "$appDir\licenses\LICENSES\AGPL-3.0-only.txt",
                  "$appDir\licenses\LICENSES\Inno-Setup.txt", "$appDir\licenses\JUCE\LICENSE.md",
                  $uninstaller, $uninstallKey) {
    if (-not (Test-Path -LiteralPath $path)) { $failures += "missing after install: $path" }
}

# Load the installed VST3 like a host: identity, bypass, one sender per user,
# and audio with timestamps reaching the OBS-side reader.
& C:\daskai\vst3_smoke.exe $vst3
if ($LASTEXITCODE -ne 0) { $failures += "the smoke test on the installed VST3 failed (exit code $LASTEXITCODE)" }
else { Write-Host "Installed VST3 loads and sends audio to the OBS-side reader." }

# The uninstaller hands over to a copy of itself; wait for the files to go.
if (Test-Path $uninstaller) {
    $un = Start-Process $uninstaller -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait -PassThru
    Write-Host "unins000.exe -> exit code $($un.ExitCode)"
    if ($un.ExitCode -ne 0) { $failures += "the uninstaller exited with $($un.ExitCode)" }
    $left = { @($vst3, $obs, $appDir, $uninstallKey | Where-Object { Test-Path -LiteralPath $_ }) }
    $deadline = (Get-Date).AddSeconds(120)
    while ((& $left).Count -gt 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
    foreach ($path in & $left) { $failures += "still there after uninstalling: $path" }
    if ((& $left).Count -eq 0) { Write-Host "Uninstall removed the VST3, the OBS plug-in, the app folder and the uninstall entry." }
}

Write-Host "::group::Installer log"
Get-Content C:\daskai\setup.log | Out-Host
Write-Host "::endgroup::"
if ($failures.Count -gt 0) {
    Write-Host "FAILED:"
    $failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Write-Host "Installer check passed."
exit 0
'@ | Set-Content -Encoding utf8BOM (Join-Path $stage "in-container.ps1")

# Process isolation needs the container's Windows build to match the runner's.
# The full "server" image: JUCE plug-ins link Direct2D, which Server Core lacks.
$build = [System.Environment]::OSVersion.Version.Build
$tag = switch ($build) { 20348 { "ltsc2022" } 26100 { "ltsc2025" } default { throw "no container tag for Windows build $build" } }
$image = "mcr.microsoft.com/windows/server:$tag"

# On the same runner image the Docker service was running on one run and not
# on the next (2026-11-05); start it and wait until it answers.
$docker = Get-Service docker -ErrorAction SilentlyContinue
if ($null -eq $docker) { throw "Docker is not installed on this runner" }
if ($docker.Status -ne "Running") {
    Write-Host "Starting the Docker service ($($docker.Status))"
    Start-Service docker
}
$deadline = (Get-Date).AddMinutes(3)
while ($true) {
    docker version --format "{{.Server.Version}}" 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) { break }
    if ((Get-Date) -gt $deadline) { throw "Docker did not start within 3 minutes" }
    Start-Sleep -Seconds 5
}
Write-Host "Docker $(docker version --format '{{.Server.Version}}') is running"

Write-Host "::group::Pull $image"
docker pull $image | Out-Host
Write-Host "::endgroup::"
docker run --rm --isolation=process -v "${stage}:C:\daskai" $image powershell -NoProfile -ExecutionPolicy Bypass -File C:\daskai\in-container.ps1 | Out-Host
$code = $LASTEXITCODE
Write-Host "Clean machine install -> exit code $code"
if ($code -ne 0) { exit 1 }
