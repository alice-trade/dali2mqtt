#!/usr/bin/env bash
set -e

CLI_ARGS_COUNT=$#
TARGET=""
BUILD_TYPE=""
COMMAND=""
BUILD_DIR=""
CUSTOM_BUILD_DIR=""
BUILD_TESTS="ON"
OFFLINE_DIR=""
FORCE_INTERACTIVE=0

C_BORDER='\033[38;5;67m'
C_TITLE='\033[1;38;5;111m'
C_ACTIVE='\033[1;38;5;51m'
C_INACTIVE='\033[38;5;250m'
C_SEL_BG='\033[48;5;237m'
C_ACCENT='\033[1;38;5;48m'
C_ERR='\033[38;5;196m'
NC='\033[0m'

cleanup() {
    printf "\e[?25h\e[0m"
}
trap cleanup EXIT
trap 'cleanup; exit 130' SIGINT SIGTERM

print_help() {
    echo -e "${C_TITLE}DaliMQTT Build Helper${NC}"
    echo "Usage: ./build.sh [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  app           Configure and build the firmware (default)"
    echo "  flash         Build and flash the firmware to the device"
    echo "  monitor       Open the ESP-IDF serial monitor"
    echo "  menuconfig    Open the Kconfig menu"
    echo "  lint          Lints sources with clang-tidy"
    echo "  cppcheck      Runs cppcheck static analysis"
    echo "  test-flash    Build and flash the test firmware"
    echo "  unit-test     Run embedded unit tests"
    echo "  integration   Run integration Pytest suite"
    echo "  clean         Remove the build directory for the selected target"
    echo ""
    echo "Options:"
    echo "  -t, --target <chip>       esp32s3, esp32c6, esp32c3, esp32s2, esp32"
    echo "  -b, --build-type <type>   Release, Debug"
    echo "  -d, --build-dir <dir>     Custom build output directory"
    echo "  --offline <dir>           Offline dependencies path"
    echo "  -i, --interactive         Run graphical TUI"
    echo "  -h, --help                Show help"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        app|flash|monitor|menuconfig|clean|test-flash|unit-test|integration|lint|cppcheck)
            COMMAND="$1"; shift ;;
        -t|--target)
            TARGET="$2"; shift 2 ;;
        -b|--build-type)
            BUILD_TYPE="$2"; shift 2 ;;
        -d|--build-dir)
            CUSTOM_BUILD_DIR="$2"; shift 2 ;;
        --offline)
            OFFLINE_DIR="$2"; shift 2 ;;
        -i|--interactive)
            FORCE_INTERACTIVE=1; shift ;;
        -h|--help)
            print_help; exit 0 ;;
        *)
            echo -e "${C_ERR}Unknown argument: $1${NC}"; exit 1 ;;
    esac
done

tui_header() {
    printf "${C_BORDER}╭────────────────────────────────────────────╮${NC}\n"
    printf "${C_BORDER}│${NC}  ${C_TITLE}DALI-to-MQTT Bridge${NC}                       ${C_BORDER}│${NC}\n"
    printf "${C_BORDER}│${NC}  ${C_INACTIVE}Build Helper${NC}                              ${C_BORDER}│${NC}\n"
    printf "${C_BORDER}╰────────────────────────────────────────────╯${NC}\n"
}

tui_select() {
    local title="$1"
    local with_header="${2:-0}"
    shift 2
    local options=("$@")
    local count=${#options[@]}
    local selected=0
    local width=46
    local lines_to_clear=$((count + 2))
    [ "$with_header" -eq 1 ] && lines_to_clear=$((lines_to_clear + 4))

    printf "\e[?25l"

    while true; do
        [ "$with_header" -eq 1 ] && tui_header

        printf "${C_BORDER}╭─${C_TITLE} %s ${C_BORDER}" "$title"
        local title_len=${#title}
        local pad_top=$((width - title_len - 5))
        for ((i=0; i<pad_top; i++)); do printf "─"; done
        printf "╮${NC}\n"

        for ((i=0; i<count; i++)); do
            local opt="${options[$i]}"
            local opt_len=${#opt}
            local pad_space=$((width - opt_len - 6))

            if [ $i -eq $selected ]; then
                printf "${C_BORDER}│${NC}${C_SEL_BG}${C_ACTIVE} ❯ %s" "$opt"
                for ((j=0; j<pad_space; j++)); do printf " "; done
                printf "${NC}${C_BORDER} │${NC}\n"
            else
                printf "${C_BORDER}│${NC}   ${C_INACTIVE}%s" "$opt"
                for ((j=0; j<pad_space; j++)); do printf " "; done
                printf "${NC}${C_BORDER} │${NC}\n"
            fi
        done

        printf "${C_BORDER}╰"
        for ((i=0; i<width-2; i++)); do printf "─"; done
        printf "╯${NC}\n"

        IFS= read -rsn1 key
        if [[ $key == $'\x1b' ]]; then
            read -rsn2 -t 0.1 key2
            key+="$key2"
        fi

        case "$key" in
            $'\x1b[A'|$'\x1bOA'|k|K)
                selected=$(( (selected - 1 + count) % count ))
                ;;
            $'\x1b[B'|$'\x1bOB'|j|J)
                selected=$(( (selected + 1) % count ))
                ;;
            "")
                printf "\e[%dA\e[0J" "$lines_to_clear"
                TUI_RESULT="$selected"
                return 0
                ;;
            q|Q)
                cleanup
                exit 0
                ;;
        esac

        printf "\e[%dA" "$lines_to_clear"
    done
}

tui_input() {
    local prompt="$1"
    local default_val="$2"
    printf "\e[?25h"
    printf "${C_BORDER}╭─${C_TITLE} %s ${C_BORDER}─────────────────────────────────╮${NC}\n" "$prompt"
    printf "${C_BORDER}│${NC}  Default: ${C_INACTIVE}%s${NC}\n" "$default_val"
    printf "${C_BORDER}│${NC}  ❯ "
    read -r user_val
    printf "${C_BORDER}╰─────────────────────────────────────────────╯${NC}\n"
    printf "\e[4A\e[0J\e[?25l"
    echo "${user_val:-$default_val}"
}

IS_INTERACTIVE=0
if [ -t 0 ] && [ -t 1 ]; then
    IS_INTERACTIVE=1
fi

RUN_TUI=0
if [ $IS_INTERACTIVE -eq 1 ] && { [ $CLI_ARGS_COUNT -eq 0 ] || [ $FORCE_INTERACTIVE -eq 1 ]; }; then
    RUN_TUI=1
fi

if [ $RUN_TUI -eq 1 ]; then
    actions=("Build" "Flash" "Monitor" "Config" "Test" "Lint" "Clean" "Quit")
    tui_select "Action" 1 "${actions[@]}"
    case $TUI_RESULT in
        0) COMMAND="app" ;;
        1) COMMAND="flash" ;;
        2) COMMAND="monitor" ;;
        3) COMMAND="menuconfig" ;;
        4)
            test_ops=("Unit Test" "Integration" "Flash Test")
            tui_select "Test" 0 "${test_ops[@]}"
            case $TUI_RESULT in
                0) COMMAND="unit-test" ;;
                1) COMMAND="integration" ;;
                2) COMMAND="test-flash" ;;
            esac
            ;;
        5)
            lint_ops=("Clang-Tidy" "Cppcheck")
            tui_select "Lint" 0 "${lint_ops[@]}"
            case $TUI_RESULT in
                0) COMMAND="lint" ;;
                1) COMMAND="cppcheck" ;;
            esac
            ;;
        6) COMMAND="clean" ;;
        7) cleanup; exit 0 ;;
    esac
fi

[ -z "$COMMAND" ] && COMMAND="app"

if [ -z "$TARGET" ]; then
    if [ $IS_INTERACTIVE -eq 1 ]; then
        targets=("esp32s3" "esp32c6" "esp32c3" "esp32s2" "esp32")
        tui_select "Target" 0 "${targets[@]}"
        TARGET="${targets[$TUI_RESULT]}"
    else
        echo -e "${C_ERR}Target chip is required (-t)${NC}"
        exit 1
    fi
fi

if [ -z "$BUILD_TYPE" ]; then
    if [ $RUN_TUI -eq 1 ]; then
        types=("Release" "Debug")
        tui_select "Build Type" 0 "${types[@]}"
        BUILD_TYPE="${types[$TUI_RESULT]}"
    else
        BUILD_TYPE="Release"
    fi
fi

DEFAULT_BUILD_DIR="build_${TARGET}_${BUILD_TYPE,,}"

if [ -z "$CUSTOM_BUILD_DIR" ] && [ $RUN_TUI -eq 1 ]; then
    dir_ops=("Default (${DEFAULT_BUILD_DIR})" "Custom")
    tui_select "Build Directory" 0 "${dir_ops[@]}"
    if [ $TUI_RESULT -eq 1 ]; then
        CUSTOM_BUILD_DIR=$(tui_input "Path" "$DEFAULT_BUILD_DIR")
    fi
fi

BUILD_DIR="${CUSTOM_BUILD_DIR:-$DEFAULT_BUILD_DIR}"

cleanup

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "$IDF_PATH" ]; then
    possible_paths=(
        "$SCRIPT_DIR/esp-idf/export.sh"
        "$SCRIPT_DIR/.esp-idf/export.sh"
        "$HOME/esp/esp-idf/export.sh"
        "$HOME/esp-idf/export.sh"
        "/opt/esp-idf/export.sh"
    )

    for p in "${possible_paths[@]}"; do
        if [ -f "$p" ]; then
            source "$p"
            break
        fi
    done
    if [ -z "$IDF_PATH" ]; then
        echo -e "${C_ERR}IDF_PATH not found${NC}"
        exit 1
    fi
fi

TOOLCHAIN_FILE="$IDF_PATH/tools/cmake/toolchain-${TARGET}.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo -e "${C_ERR}Missing toolchain for ${TARGET}${NC}"
    exit 1
fi

if [ "$COMMAND" == "clean" ]; then
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        echo -e "${C_ACCENT}Cleaned: ${BUILD_DIR}${NC}"
    fi
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
    [ ! -f "offline-fetch" ] && exit 1
    OFFLINE_FLAGS=$(python3 offline-fetch.py get-args "$OFFLINE_DIR" | grep "\-DFETCHCONTENT" || true)
    if [ -n "$OFFLINE_FLAGS" ]; then
        read -r -a OFFLINE_ARGS <<< "$OFFLINE_FLAGS"
        CMAKE_ARGS+=("${OFFLINE_ARGS[@]}")
    fi
fi

printf "${C_BORDER}╭────────────────────────────────────────────╮${NC}\n"
printf "${C_BORDER}│${NC}  Target : ${C_ACCENT}%-32s${NC}${C_BORDER}│${NC}\n" "$TARGET"
printf "${C_BORDER}│${NC}  Config : %-32s${C_BORDER}│${NC}\n" "$BUILD_TYPE"
printf "${C_BORDER}│${NC}  Action : ${C_ACTIVE}%-32s${NC}${C_BORDER}│${NC}\n" "$COMMAND"
printf "${C_BORDER}│${NC}  Output : %-32s${C_BORDER}│${NC}\n" "$BUILD_DIR"
printf "${C_BORDER}╰────────────────────────────────────────────╯${NC}\n"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake "${CMAKE_ARGS[@]}" .
fi

case $COMMAND in
    app)         cmake --build "$BUILD_DIR" ;;
    flash)       cmake --build "$BUILD_DIR" --target flash ;;
    monitor)     cmake --build "$BUILD_DIR" --target monitor ;;
    menuconfig)  cmake --build "$BUILD_DIR" --target menuconfig ;;
    test-flash)  cmake --build "$BUILD_DIR" --target test-flash ;;
    unit-test)   cmake --build "$BUILD_DIR" --target pytest-unit ;;
    integration) cmake --build "$BUILD_DIR" --target pytest-integration ;;
    lint)        cmake --build "$BUILD_DIR" --target clang-tidy ;;
    cppcheck)    cmake --build "$BUILD_DIR" --target cppcheck ;;
esac