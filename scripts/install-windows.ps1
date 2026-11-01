[CmdletBinding()]
param([string]$OBSRoot = "$env:ProgramFiles/obs-studio")
$ErrorActionPreference = 'Stop'
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
