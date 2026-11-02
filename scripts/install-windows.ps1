[CmdletBinding()]
param([string]$OBSRoot = "$env:ProgramFiles/obs-studio")
$ErrorActionPreference = 'Stop'
# Request Windows UAC elevation automatically for the standard Program Files install.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $launchArgs = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -OBSRoot "{1}"' -f $PSCommandPath, $OBSRoot
    try {
        $child = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') -Verb RunAs -ArgumentList $launchArgs -Wait -PassThru
        exit $child.ExitCode
    } catch {
        throw 'Administrator approval was cancelled or failed. Right-click INSTALL.cmd and choose Run as administrator.'
    }
}

if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'Close OBS before installing.' }
# This script lives next to face-png-filter/ in the generated ZIP.
$source = Join-Path $PSScriptRoot 'face-png-filter'
if (!(Test-Path "$source/bin/64bit/face-png-filter.dll")) { throw 'Plugin DLL missing. Run this script from the extracted Windows binary ZIP.' }
if (!(Test-Path "$OBSRoot/bin/64bit/obs64.exe")) { throw "OBS not found at $OBSRoot. Specify -OBSRoot." }
$version = (Get-Item "$OBSRoot/bin/64bit/obs64.exe").VersionInfo.ProductVersion
Write-Host "Installed OBS: $version"
if ([version]($version -replace '[^0-9.].*$','') -lt [version]'31.1.1') { throw 'OBS 31.1.1 or newer is required.' }
# Conventional OBS root layout also supports portable installations. Program Files needs Administrator.
New-Item -ItemType Directory -Force "$OBSRoot/obs-plugins/64bit", "$OBSRoot/data/obs-plugins/face-png-filter" | Out-Null
Copy-Item "$source/bin/64bit/face-png-filter.dll" "$OBSRoot/obs-plugins/64bit/" -Force
Copy-Item "$source/data/*" "$OBSRoot/data/obs-plugins/face-png-filter/" -Recurse -Force
Write-Host 'Installed. Start OBS and add 顔追従PNGオーバーレイ as an effect filter.'
