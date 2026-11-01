# Requires VS 2022 Desktop development with C++, Windows SDK >= 10.0.22621,
# CMake >= 3.30, Git and Python 3. Network is used only during setup/build.
[CmdletBinding()]
param([string]$Configuration = 'RelWithDebInfo', [int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
function Run([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed with exit code $LASTEXITCODE" }
}
Push-Location $root
try {
    Run 'python' @('scripts/setup-model.py')
    Run 'cmake' @('--preset', 'windows-x64', '-DBUILD_TESTING=ON')
    Run 'cmake' @('--build', 'build_x64', '--config', $Configuration, '--parallel', "$Jobs")
    Run 'ctest' @('--test-dir', 'build_x64', '-C', $Configuration, '--output-on-failure')
    # OpenCV has no install rules; the template installs the plugin and data only.
    $stage = Join-Path $root 'dist/stage'
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    Run 'cmake' @('--install', 'build_x64', '--config', $Configuration, '--prefix', $stage)
    Copy-Item 'scripts/install-windows.ps1' $stage
    Copy-Item 'README.md' $stage
    Copy-Item 'docs/TESTING.md' $stage
    $zip = Join-Path $root 'dist/face-png-filter-0.1.0-windows-x64.zip'
    Compress-Archive -Path "$stage/*" -DestinationPath $zip -Force
    Get-FileHash -Algorithm SHA256 $zip
    Write-Host "Created $zip"
} finally { Pop-Location }
