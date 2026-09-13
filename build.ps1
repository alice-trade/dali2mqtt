<#
// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later
#>

[CmdletBinding()]
param (
    [Parameter(Position = 0)]
    [ValidateSet("app", "flash", "monitor", "menuconfig", "clean", "test-flash", "unit-test", "integration", "")]
    [string]$Command = "",

    [Alias("t")]
    [string]$Target = "",

    [Alias("b")]
    [string]$BuildType = "",

    [Alias("offline")]
    [string]$OfflineDir = "",

    [Alias("h")]
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Print-Help {
    Write-Host "DaliMQTT Build Helper" -ForegroundColor Blue
    Write-Host "Usage: .\build.ps1 [COMMAND] [OPTIONS]`n"
    Write-Host "Commands:"
    Write-Host "  app           Configure and build the firmware (default)"
    Write-Host "  flash         Build and flash the firmware to the device"
    Write-Host "  monitor       Open the ESP-IDF serial monitor"
    Write-Host "  menuconfig    Open the Kconfig menu"
    Write-Host "  test-flash    Build and flash the test firmware"
    Write-Host "  unit-test     Run embedded unit tests"
    Write-Host "  integration   Run integration Pytest suite"
    Write-Host "  clean         Remove the build directory for the selected target`n"
    Write-Host "Options:"
    Write-Host "  -t, --target <target>    ESP32 target (esp32s3, esp32c6, esp32c3, esp32s2)."
    Write-Host "                           If omitted, an interactive menu will appear."
    Write-Host "  -b, --build-type <type>  CMake build type (Debug/Release)."
    Write-Host "                           If omitted, an interactive menu will appear."
    Write-Host "  --offline <dir>          Use offline assets directory for dependencies"
    Write-Host "  -h, --help               Show this help message`n"
    Write-Host "Examples:"
    Write-Host "  .\build.ps1 flash -t esp32c6 -b Debug"
    Write-Host "  .\build.ps1 app"
    Write-Host "  .\build.ps1 app --offline .\assets"
}

if ($Help -or ($PSBoundParameters.Count -eq 0 -and [string]::IsNullOrEmpty($Command))) {
    Print-Help
    exit 0
}

if ([string]::IsNullOrEmpty($Command)) {
    $Command = "app"
}

if ([string]::IsNullOrEmpty($Target)) {
    $platforms = @("esp32s3", "esp32c6", "esp32c3", "esp32s2", "Quit")
    Write-Host "Target platform was not specified." -ForegroundColor Yellow
    Write-Host "Please select a target platform:"

    for ($i = 0; $i -lt $platforms.Count; $i++) {
        Write-Host "  $($i + 1)) $($platforms[$i])"
    }

    do {
        $choice = Read-Host "Enter a number"
        $idx = 0
        if ([int]::TryParse($choice, [ref]$idx) -and $idx -ge 1 -and $idx -le $platforms.Count) {
            $selected = $platforms[$idx - 1]
            if ($selected -eq "Quit") {
                Write-Host "Aborted." -ForegroundColor Yellow
                exit 0
            }
            $Target = $selected
            Write-Host "Selected target: $Target" -ForegroundColor Green
            break
        } else {
            Write-Host "Invalid option. Please try again." -ForegroundColor Red
        }
    } while ($true)
}

if ([string]::IsNullOrEmpty($BuildType)) {
    $btypes = @("Release", "Debug")
    Write-Host "`nBuild Type was not specified." -ForegroundColor Yellow
    Write-Host "Please select a build type:"

    for ($i = 0; $i -lt $btypes.Count; $i++) {
        Write-Host "  $($i + 1)) $($btypes[$i])"
    }

    do {
        $choice = Read-Host "Enter a number"
        $idx = 0
        if ([int]::TryParse($choice, [ref]$idx) -and $idx -ge 1 -and $idx -le $btypes.Count) {
            $BuildType = $btypes[$idx - 1]
            Write-Host "Selected Build Type: $BuildType" -ForegroundColor Green
            break
        } else {
            Write-Host "Invalid option. Please try again." -ForegroundColor Red
        }
    } while ($true)
}

$BuildDir = "build_${Target}_$($BuildType.ToLower())"
$BuildTests = "ON"

if ([string]::IsNullOrEmpty($env:IDF_PATH)) {
    Write-Host "IDF_PATH is not set. Looking for export.ps1..." -ForegroundColor Yellow

    $possiblePaths = @(
        "$HOME\esp\esp-idf\export.ps1",
        "$HOME\esp-idf\export.ps1",
        "C:\Espressif\frameworks\esp-idf\export.ps1",
        "$env:USERPROFILE\esp\esp-idf\export.ps1"
    )

    $found = $false
    foreach ($p in $possiblePaths) {
        if (Test-Path $p) {
            Write-Host "Found ESP-IDF at $p" -ForegroundColor Green
            . $p
            $found = $true
            break
        }
    }

    if (-not $found -or [string]::IsNullOrEmpty($env:IDF_PATH)) {
        Write-Host "Error: Cannot find ESP-IDF export.ps1." -ForegroundColor Red
        Write-Host "Please run export.ps1 manually: . C:\path\to\esp-idf\export.ps1"
        Write-Host "`nIf you haven't installed ESP-IDF yet, download the official installer" -ForegroundColor Yellow
        Write-Host "Setup guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html`n" -ForegroundColor Gray
        exit 1
    }
} else {
    Write-Host "ESP-IDF environment already active (IDF_PATH=$env:IDF_PATH)" -ForegroundColor Green
}

$ToolchainFile = "$($env:IDF_PATH)\tools\cmake\toolchain-$Target.cmake".Replace('\', '/')

if (-not (Test-Path $ToolchainFile)) {
    Write-Host "Error: Toolchain file for target '$Target' not found!" -ForegroundColor Red
    Write-Host "Expected: $ToolchainFile"
    Write-Host "Check if you typed the target name correctly."
    exit 1
}

if ($Command -eq "clean") {
    Write-Host "Cleaning build directory: $BuildDir" -ForegroundColor Yellow
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    exit 0
}

$CMakeArgs = @(
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DBUILD_TESTING=$BuildTests"
)

$PythonCmd = if (Get-Command "python" -ErrorAction SilentlyContinue) { "python" } elseif (Get-Command "py" -ErrorAction SilentlyContinue) { "py" } else { "python3" }

if (-not [string]::IsNullOrEmpty($OfflineDir)) {
    Write-Host "Fetching offline flags from $OfflineDir..." -ForegroundColor Yellow
    if (-not (Test-Path "offline-fetch")) {
        Write-Host "Error: offline-fetch tool not found." -ForegroundColor Red
        exit 1
    }

    $OfflineOutput = & $PythonCmd offline-fetch get-args "$OfflineDir" | Select-String "-DFETCHCONTENT"
    if ($OfflineOutput) {
        $OfflineFlags = ($OfflineOutput -join " ").Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
        $CMakeArgs += $OfflineFlags
    } else {
        Write-Host "Error: Could not generate offline CMake flags." -ForegroundColor Red
        exit 1
    }
}

Write-Host "`n=================================================" -ForegroundColor Cyan
Write-Host " Target     : $Target" -ForegroundColor Green
Write-Host " Build Type : $BuildType" -ForegroundColor Green
Write-Host " Testing    : $BuildTests" -ForegroundColor Green
Write-Host " Build Dir  : $BuildDir" -ForegroundColor Green
Write-Host "=================================================`n" -ForegroundColor Cyan

if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "First-time configuration for $Target ($BuildType)..." -ForegroundColor Yellow
    & cmake $CMakeArgs .
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Command) {
    "app" {
        & cmake --build $BuildDir
    }
    "flash" {
        & cmake --build $BuildDir --target flash
    }
    "monitor" {
        & cmake --build $BuildDir --target monitor
    }
    "menuconfig" {
        & cmake --build $BuildDir --target menuconfig
    }
    "test-flash" {
        Write-Host "Building and flashing testing firmware..." -ForegroundColor Yellow
        & cmake --build $BuildDir --target test-flash
    }
    "unit-test" {
        Write-Host "Running unit tests Pytest suite..." -ForegroundColor Yellow
        & cmake --build $BuildDir --target pytest-unit
    }
    "integration" {
        Write-Host "Running integration Pytest suite..." -ForegroundColor Yellow
        & cmake --build $BuildDir --target pytest-integration
    }
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nExecution failed with error code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "`nDone" -ForegroundColor Green