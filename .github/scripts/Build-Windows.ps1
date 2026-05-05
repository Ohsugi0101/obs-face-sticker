[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Build-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    $CmakeArgs = @('--preset', "windows-ci-${Target}")

    if ( $env:CMAKE_TOOLCHAIN_FILE ) {
        $CmakeArgs += @(
            "-DCMAKE_TOOLCHAIN_FILE=$env:CMAKE_TOOLCHAIN_FILE"
        )
        if ( $env:VCPKG_TARGET_TRIPLET ) {
            $CmakeArgs += @("-DVCPKG_TARGET_TRIPLET=$env:VCPKG_TARGET_TRIPLET")
        }
    }
    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CmakeInstallArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        '--preset', "windows-${Target}"
        '--config', $Configuration
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CmakeInstallArgs += @(
        '--install', "build_${Target}"
        '--prefix', "${ProjectRoot}/release/${Configuration}"
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    # Copy required vcpkg DLLs (OpenCV and its deps) into the install folder
    # so the plugin can be packaged as a stand-alone zip.
    if ( $env:VCPKG_INSTALLATION_ROOT ) {
        $VcpkgBin = "$env:VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin"
        if ( Test-Path $VcpkgBin ) {
            $InstallDir = "${ProjectRoot}/release/${Configuration}"
            $TargetBin = Get-ChildItem -Path $InstallDir -Recurse -Filter '64bit' -Directory | Select-Object -First 1
            if ( $TargetBin -and (Test-Path $TargetBin.FullName) ) {
                Log-Group "Copying vcpkg runtime DLLs..."
                $patterns = @('opencv_*.dll', 'jpeg62.dll', 'libpng16.dll', 'tiff.dll', 'liblzma.dll', 'zlib*.dll', 'tbb*.dll', 'libwebp*.dll', 'libsharpyuv*.dll')
                foreach ($pat in $patterns) {
                    Get-ChildItem -Path $VcpkgBin -Filter $pat -ErrorAction SilentlyContinue | ForEach-Object {
                        Copy-Item -Path $_.FullName -Destination $TargetBin.FullName -Force
                    }
                }
            }
        }
    }

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
