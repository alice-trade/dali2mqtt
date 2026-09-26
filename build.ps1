[CmdletBinding()]
param (
    [Parameter(Position = 0)]
    [ValidateSet("app", "flash", "monitor", "menuconfig", "clean", "test-flash", "unit-test", "integration", "")]
    [string]$Command = "",

    [Alias("t")]
    [string]$Target = "",

    [Alias("b")]
    [string]$BuildType = "",

    [Alias("d")]
    [string]$CustomBuildDir = "",

    [Alias("offline")]
    [string]$OfflineDir = "",

    [Alias("h")]
    [switch]$Help
)

$ErrorActionPreference = "Stop"

$esc = [char]27
$C_BORDER = "$esc[38;5;67m"
$C_TITLE  = "$esc[1;38;5;111m"
$C_ACTIVE = "$esc[1;38;5;51m"
$C_INACT  = "$esc[38;5;250m"
$C_SEL_BG = "$esc[48;5;237m"
$C_ACCENT = "$esc[1;38;5;48m"
$C_ERR    = "$esc[38;5;196m"
$NC       = "$esc[0m"

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
    Write-Host "  lint          Lint the sources with clang-tidy the selected target`n"
    Write-Host "  cppcheck      Run static analysis with cppcheck"
    Write-Host "  clean         Remove the build directory for the selected target`n"
    Write-Host "Options:"
    Write-Host "  -t <target>              ESP32 target (esp32s3, esp32c6, esp32c3, esp32s2, esp32)."
    Write-Host "                           If omitted, an interactive menu will appear."
    Write-Host "  -b <type>                CMake build type (Debug/Release)."
    Write-Host "                           If omitted, an interactive menu will appear."
    Write-Host "  -d <dir>                 Custom build output directory"
    Write-Host "  -i                       Run interactive TUI"
    Write-Host "  --offline <dir>          Use offline assets directory for dependencies"
    Write-Host "  -h, --help               Show this help message`n"
    Write-Host "Examples:"
    Write-Host "  .\build.ps1 flash -t esp32c6 -b Debug"
    Write-Host "  .\build.ps1 app"
    Write-Host "  .\build.ps1 app --offline .\assets"
}

if ($Help) {
    Print-Help
    exit 0
}

function Tui-Header {
    Write-Host "$C_BORDER╭────────────────────────────────────────────╮$NC"
    Write-Host "$C_BORDER│$NC  $C_TITLE DALI-to-MQTT Bridge$NC                      $C_BORDER│$NC"
    Write-Host "$C_BORDER│$NC  $C_INACT Build Helper$NC                             $C_BORDER│$NC"
    Write-Host "$C_BORDER╰────────────────────────────────────────────╯$NC"
}


function Tui-Select {
    param (
        [string]$Title,
        [string[]]$Options,
        [switch]$WithHeader
    )

    $count = $Options.Length
    $selected = 0
    $width = 46
    $linesToClear = $count + 2 + $(if ($WithHeader) { 4 } else { 0 })

    [Console]::CursorVisible = $false

    try {
        while ($true) {
            if ($WithHeader) {
                Tui-Header
            }

            $padTop = $width - $Title.Length - 5
            $topLine = "$C_BORDER╭─$C_TITLE $Title $C_BORDER" + ("─" * [Math]::Max(0, $padTop)) + "╮$NC"
            Write-Host $topLine

            for ($i = 0; $i -lt $count; $i++) {
                $opt = $Options[$i]
                $padSpace = $width - $opt.Length - 6
                $spaces = " " * [Math]::Max(0, $padSpace)

                if ($i -eq $selected) {
                    Write-Host "$C_BORDER│$NC$C_SEL_BG$C_ACTIVE ❯ $opt$spaces$NC$C_BORDER │$NC"
                } else {
                    Write-Host "$C_BORDER│$NC   $C_INACT$opt$spaces$NC$C_BORDER │$NC"
                }
            }

            $bottomLine = "$C_BORDER╰" + ("─" * ($width - 2)) + "╯$NC"
            Write-Host $bottomLine

            $key = [Console]::ReadKey($true)
            switch ($key.Key) {
                "UpArrow"   { $selected = ($selected - 1 + $count) % $count }
                "DownArrow" { $selected = ($selected + 1) % $count }
                "K"         { $selected = ($selected - 1 + $count) % $count }
                "J"         { $selected = ($selected + 1) % $count }
                "Enter" {
                    Write-Host "$esc[${linesToClear}A$esc[0J" -NoNewline
                    return $selected
                }
                "Q" {
                    Write-Host "$esc[${linesToClear}A$esc[0J" -NoNewline
                    [Console]::CursorVisible = $true
                    exit 0
                }
                "Escape" {
                    Write-Host "$esc[${linesToClear}A$esc[0J" -NoNewline
                    [Console]::CursorVisible = $true
                    exit 0
                }
            }

            Write-Host "$esc[${linesToClear}A" -NoNewline
        }
    } finally {
        [Console]::CursorVisible = $true
    }
}

function Tui-Input {
    param (
        [string]$Prompt,
        [string]$DefaultVal
    )

    [Console]::CursorVisible = $true
    Write-Host "$C_BORDER╭─$C_TITLE $Prompt $C_BORDER─────────────────────────────────╮$NC"
    Write-Host "$C_BORDER│$NC  Default: $C_INACT$DefaultVal$NC"
    Write-Host "$C_BORDER│$NC  ❯ " -NoNewline
    $val = Read-Host
    Write-Host "$C_BORDER╰─────────────────────────────────────────────╯$NC"
    [Console]::CursorVisible = $false
    if ([string]::IsNullOrWhiteSpace($val)) { return $DefaultVal }
    return $val
}

$isInteractive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected

if (($PSBoundParameters.Count -eq 0 -or $Interactive) -and $isInteractive) {
    $actions = @("Build", "Flash", "Monitor", "Config", "Test", "Lint", "Clean", "Quit")
    $actIdx = Tui-Select -Title "Action" -Options $actions -WithHeader

    switch ($actIdx) {
        0 { $Command = "app" }
        1 { $Command = "flash" }
        2 { $Command = "monitor" }
        3 { $Command = "menuconfig" }
        4 {
            $testOps = @("Unit Test", "Integration", "Flash Test")
            $tIdx = Tui-Select -Title "Test" -Options $testOps
            switch ($tIdx) {
                0 { $Command = "unit-test" }
                1 { $Command = "integration" }
                2 { $Command = "test-flash" }
            }
        }
        5 {
            $lintOps = @("Clang-Tidy", "Cppcheck")
            $lIdx = Tui-Select -Title "Lint" -Options $lintOps
            switch ($lIdx) {
                0 { $Command = "lint" }
                1 { $Command = "cppcheck" }
            }
        }
        6 { $Command = "clean" }
        7 { exit 0 }
    }
}

if ([string]::IsNullOrEmpty($Command)) {
    $Command = "app"
}

if ([string]::IsNullOrEmpty($Target)) {
    if ($isInteractive) {
        $targets = @("esp32s3", "esp32c6", "esp32c3", "esp32s2", "esp32")
        $tIdx = Tui-Select -Title "Target" -Options $targets
        $Target = $targets[$tIdx]
    } else {
        Write-Host "$C_ERR Target chip is required (-t)$NC"
        exit 1
    }
}

if ([string]::IsNullOrEmpty($BuildType)) {
    if ($isInteractive -and ($PSBoundParameters.Count -eq 0 -or $Interactive)) {
        $types = @("Release", "Debug")
        $bIdx = Tui-Select -Title "Build Type" -Options $types
        $BuildType = $types[$bIdx]
    } else {
        $BuildType = "Release"
    }
}

$defaultBuildDir = "build_${Target}_$($BuildType.ToLower())"

if ([string]::IsNullOrEmpty($CustomBuildDir) -and $isInteractive -and ($PSBoundParameters.Count -eq 0 -or $Interactive)) {
    $dirOps = @("Default ($defaultBuildDir)", "Custom")
    $dIdx = Tui-Select -Title "Build Directory" -Options $dirOps
    if ($dIdx -eq 1) {
        $CustomBuildDir = Tui-Input -Prompt "Path" -DefaultVal $defaultBuildDir
    }
}

$BuildDir = if (-not [string]::IsNullOrEmpty($CustomBuildDir)) {
    $CustomBuildDir
} else {
    $defaultBuildDir
}

$BuildTests = "ON"

if ([string]::IsNullOrEmpty($env:IDF_PATH)) {
    $possiblePaths = @(
        "$PSScriptRoot\esp-idf\export.ps1",
        "$PSScriptRoot\.esp-idf\export.ps1",
        "$pwd\esp-idf\export.ps1"
        "$HOME\esp\esp-idf\export.ps1",
        "$HOME\esp-idf\export.ps1",
        "C:\Espressif\frameworks\esp-idf\export.ps1",
        "$env:USERPROFILE\esp\esp-idf\export.ps1"
    )

    $found = $false
    foreach ($p in $possiblePaths) {
        if (Test-Path $p) {
            . $p
            $found = $true
            break
        }
    }

    if (-not $found -or [string]::IsNullOrEmpty($env:IDF_PATH)) {
        Write-Host "$C_ERR Cannot find ESP-IDF export.ps1.$NC"
        exit 1
    }
}

$ToolchainFile = "$($env:IDF_PATH)\tools\cmake\toolchain-$Target.cmake".Replace('\', '/')

if (-not (Test-Path $ToolchainFile)) {
    Write-Host "$C_ERR Toolchain file for target '$Target' not found!$NC"
    exit 1
}

if ($Command -eq "clean") {
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Host "$C_ACCENT Cleaned: $BuildDir$NC"
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
    if (-not (Test-Path "offline-fetch")) {
        Write-Host "$C_ERR offline-fetch tool not found.$NC"
        exit 1
    }

    $OfflineOutput = & $PythonCmd offline-fetch get-args "$OfflineDir" | Select-String "-DFETCHCONTENT"
    if ($OfflineOutput) {
        $OfflineFlags = ($OfflineOutput -join " ").Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
        $CMakeArgs += $OfflineFlags
    }
}

$padTarget = $Target.PadRight(32)
$padType   = $BuildType.PadRight(32)
$padCmd    = $Command.PadRight(32)
$padDir    = $BuildDir.PadRight(32)

Write-Host "$C_BORDER╭────────────────────────────────────────────╮$NC"
Write-Host "$C_BORDER│$NC  Target : $C_ACCENT$padTarget$NC$C_BORDER│$NC"
Write-Host "$C_BORDER│$NC  Config : $padType$C_BORDER│$NC"
Write-Host "$C_BORDER│$NC  Action : $C_ACTIVE$padCmd$NC$C_BORDER│$NC"
Write-Host "$C_BORDER│$NC  Output : $padDir$C_BORDER│$NC"
Write-Host "$C_BORDER╰────────────────────────────────────────────╯$NC"

if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    & cmake $CMakeArgs .
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Command) {
    "app"         { & cmake --build $BuildDir }
    "flash"       { & cmake --build $BuildDir --target flash }
    "monitor"     { & cmake --build $BuildDir --target monitor }
    "menuconfig"  { & cmake --build $BuildDir --target menuconfig }
    "test-flash"  { & cmake --build $BuildDir --target test-flash }
    "unit-test"   { & cmake --build $BuildDir --target pytest-unit }
    "integration" { & cmake --build $BuildDir --target pytest-integration }
    "lint"        { & cmake --build $BuildDir --target clang-tidy }
    "cppcheck"    { & cmake --build $BuildDir --target cppcheck }
}

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}