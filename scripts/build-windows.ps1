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
    # OpenCV also installs SDK files. Package only the plugin and runtime resources.
    $stage = Join-Path $root 'dist/stage'
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    Run 'cmake' @('--install', 'build_x64', '--config', $Configuration, '--prefix', $stage)
    Copy-Item 'scripts/install-windows.ps1' $stage
    Copy-Item 'scripts/INSTALL.cmd' $stage
    Copy-Item 'QUICKSTART.txt' $stage
    Copy-Item 'README.md' $stage
    Copy-Item 'docs/TESTING.md' $stage
    Copy-Item 'docs' $stage -Recurse -Force
    $zip = Join-Path $root 'dist/face-png-filter-0.1.0-windows-x64.zip'
    $packagePaths = @("$stage/face-png-filter", "$stage/install-windows.ps1", "$stage/README.md", "$stage/TESTING.md", "$stage/docs", "$stage/INSTALL.cmd", "$stage/QUICKSTART.txt")
    Compress-Archive -Path $packagePaths -DestinationPath $zip -Force
    Run 'python' @('scripts/check-package.py', $zip)
    Get-FileHash -Algorithm SHA256 $zip
    Write-Host "Created $zip"
} finally { Pop-Location }
