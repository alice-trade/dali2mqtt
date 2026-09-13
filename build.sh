#!/usr/bin/env bash
#
# // Copyright (c) 2026 Alice-Trade Inc.
# // SPDX-License-Identifier: GPL-3.0-or-later
#

set -e

TARGET=""
BUILD_TYPE=""
COMMAND="app"
BUILD_TESTS="ON"
OFFLINE_DIR=""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

function print_help() {
    echo -e "${BLUE}DaliMQTT Build Helper${NC}"
    echo "Usage: ./build.sh [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  app           Configure and build the firmware (default)"
    echo "  flash         Build and flash the firmware to the device"
    echo "  monitor       Open the ESP-IDF serial monitor"
    echo "  menuconfig    Open the Kconfig menu"
    echo "  test-flash    Build and flash the test firmware"
    echo "  unit-test     Run embedded unit tests"
    echo "  integration   Run integration Pytest suite"
    echo "  clean         Remove the build directory for the selected target"
    echo ""
    echo "Options:"
    echo "  -t, --target <target>    ESP32 target (esp32s3, esp32c6, esp32c3, esp32s2)."
    echo "                           If omitted, an interactive menu will appear."
    echo "  -b, --build-type <type>  CMake build type (Debug/Release)."
    echo "                           If omitted, an interactive menu will appear."
    echo "  --offline <dir>          Use offline assets directory for dependencies"
    echo "  -h, --help               Show this help message"
    echo ""
    echo "Examples:"
    echo "  ./build.sh flash -t esp32c6 -b Debug"
    echo "  ./build.sh app"
    echo "  ./build.sh app --offline ./assets"
}

if [ $# -eq 0 ]; then
    print_help
    exit 0
fi

while [[ $# -gt 0 ]]; do
    case $1 in
        app|flash|monitor|menuconfig|clean|test-flash|unit-test|integration)
            COMMAND="$1"
            shift
            ;;
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        -b|--build-type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        --offline)
            OFFLINE_DIR="$2"
            shift 2
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_help
            exit 1
            ;;
    esac
done

if [ -z "$TARGET" ]; then
    if [ -t 0 ]; then
        echo -e "${YELLOW}Target platform was not specified.${NC}"
        echo "Please select a target platform:"

        platforms=("esp32s3" "esp32c6" "esp32c3" "esp32s2" "Quit")

        PS3="Enter a number: "
        select opt in "${platforms[@]}"; do
            case $opt in
                "esp32s3"|"esp32c6"|"esp32c3"|"esp32s2")
                    TARGET="$opt"
                    echo -e "Selected target: ${GREEN}$TARGET${NC}"
                    break
                    ;;
                "Quit")
                    echo -e "${YELLOW}Aborted.${NC}"
                    exit 0
                    ;;
                *)
                    echo -e "${RED}Invalid option. Please try again.${NC}"
                    ;;
            esac
        done
    else
        echo -e "${RED}FATAL ERROR: Target platform is not specified and the shell is not interactive.${NC}"
        echo "You must specify the target explicitly using: -t <target> (e.g., ./build.sh app -t esp32s3)"
        exit 1
    fi
fi

if [ -z "$BUILD_TYPE" ]; then
    if [ -t 0 ]; then
        echo -e "\n${YELLOW}Build Type was not specified.${NC}"
        echo "Please select a build type:"

        btypes=("Release" "Debug")

        PS3="Enter a number: "
        select opt in "${btypes[@]}"; do
            case $opt in
                "Release"|"Debug")
                    BUILD_TYPE="$opt"
                    echo -e "Selected Build Type: ${GREEN}$BUILD_TYPE${NC}"
                    break
                    ;;
                *)
                    echo -e "${RED}Invalid option. Please try again.${NC}"
                    ;;
            esac
        done
    else
        echo -e "${YELLOW}Non-interactive shell. Defaulting to Release build.${NC}"
        BUILD_TYPE="Release"
    fi
fi

if [ -z "$TARGET" ] || [ -z "$BUILD_TYPE" ]; then
    echo -e "\n${RED}Aborted: Missing target or build type.${NC}"
    exit 1
fi

BUILD_DIR="build_${TARGET}_${BUILD_TYPE,,}"

if [ -z "$IDF_PATH" ]; then
    echo -e "${YELLOW}IDF_PATH is not set. Looking for export.sh...${NC}"
    POSSIBLE_PATHS=(
        "$HOME/esp/esp-idf/export.sh"
        "$HOME/esp-idf/export.sh"
        "/opt/esp-idf/export.sh"
    )
    FOUND=0
    for p in "${POSSIBLE_PATHS[@]}"; do
        if [ -f "$p" ]; then
            echo -e "${GREEN}Found ESP-IDF at $p${NC}"
            source "$p"
            FOUND=1
            break
        fi
    done
    if [ $FOUND -eq 0 ]; then
        echo -e "${RED}Error: Cannot find ESP-IDF export.sh.${NC}"
        echo "Please source it manually: . /path/to/esp-idf/export.sh"
        echo -e "\n${YELLOW}If you haven't installed ESP-IDF yet, download and install it:${NC}"
        echo -e "${BLUE}https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/${NC}\n"
        exit 1
    fi
else
    echo -e "${GREEN}ESP-IDF environment already active (IDF_PATH=$IDF_PATH)${NC}"
fi

TOOLCHAIN_FILE="$IDF_PATH/tools/cmake/toolchain-${TARGET}.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo -e "${RED}Error: Toolchain file for target '$TARGET' not found!${NC}"
    echo "Expected: $TOOLCHAIN_FILE"
    echo "Check if you typed the target name correctly."
    exit 1
fi

if [ "$COMMAND" == "clean" ]; then
    echo -e "${YELLOW}Cleaning build directory: $BUILD_DIR${NC}"
    rm -rf "$BUILD_DIR"
    exit 0
fi

CMAKE_ARGS=(
    "-B" "$BUILD_DIR"
    "-G" "Ninja"
    "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DBUILD_TESTING=$BUILD_TESTS"
)

if [ -n "$OFFLINE_DIR" ]; then
    echo -e "${YELLOW}Fetching offline flags from $OFFLINE_DIR...${NC}"
    if [ ! -f "offline-fetch" ]; then
        echo -e "${RED}Error: offline-fetch tool not found.${NC}"
        exit 1
    fi
    OFFLINE_FLAGS=$(python3 offline-fetch get-args "$OFFLINE_DIR" | grep "\-DFETCHCONTENT" || true)
    if [ -n "$OFFLINE_FLAGS" ]; then
        read -r -a OFFLINE_ARGS <<< "$OFFLINE_FLAGS"
        CMAKE_ARGS+=("${OFFLINE_ARGS[@]}")
    else
        echo -e "${RED}Error: Could not generate offline CMake flags.${NC}"
        exit 1
    fi
fi

echo -e "\n${BLUE}=================================================${NC}"
echo -e " Target     : ${GREEN}$TARGET${NC}"
echo -e " Build Type : ${GREEN}$BUILD_TYPE${NC}"
echo -e " Testing    : ${GREEN}$BUILD_TESTS${NC}"
echo -e " Build Dir  : ${GREEN}$BUILD_DIR${NC}"
echo -e "${BLUE}=================================================${NC}\n"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo -e "${YELLOW}First-time configuration for $TARGET ($BUILD_TYPE)...${NC}"
    cmake "${CMAKE_ARGS[@]}" .
fi

case $COMMAND in
    app)
        cmake --build "$BUILD_DIR"
        ;;
    flash)
        cmake --build "$BUILD_DIR" --target flash
        ;;
    monitor)
        cmake --build "$BUILD_DIR" --target monitor
        ;;
    menuconfig)
        cmake --build "$BUILD_DIR" --target menuconfig
        ;;
    test-flash)
        echo -e "${YELLOW}Building and flashing testing firmware...${NC}"
        cmake --build "$BUILD_DIR" --target test-flash
        ;;
    unit-test)
        echo -e "${YELLOW}Running unit tests Pytest suite...${NC}"
        cmake --build "$BUILD_DIR" --target pytest-unit
        ;;
    integration)
        echo -e "${YELLOW}Running integration Pytest suite...${NC}"
        cmake --build "$BUILD_DIR" --target pytest-integration
        ;;
esac

echo -e "\n${GREEN}Done${NC}"