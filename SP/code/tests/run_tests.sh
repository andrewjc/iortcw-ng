#!/bin/bash
#
# run_tests.sh - Headless framebuffer test runner for iortcw-ng
#
# This script sets up an Xvfb (X Virtual Framebuffer) display and runs
# the automated test suites for the iortcw-ng game engine.
#
# Usage:
#   ./run_tests.sh              Run all tests
#   ./run_tests.sh --core       Run core subsystem tests only
#   ./run_tests.sh --renderer   Run headless renderer tests only
#   ./run_tests.sh --integration Run engine integration tests only
#   ./run_tests.sh --build      Build and run all tests
#   ./run_tests.sh --debug      Run with GDB for crash debugging
#
# Requirements:
#   - Xvfb (apt install xvfb)
#   - Mesa software renderer (apt install mesa-utils libgl1-mesa-dev)
#   - SDL2 development headers (apt install libsdl2-dev)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SP_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="$SP_DIR/build/release-linux-x86_64"
TEST_BUILD_DIR="$SP_DIR/build/tests"
XVFB_PID=""
XVFB_DISPLAY=":99"
EXIT_CODE=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_pass()  { echo -e "${GREEN}[PASS]${NC}  $*"; }
log_fail()  { echo -e "${RED}[FAIL]${NC}  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }

# ================================================================
# Setup and teardown
# ================================================================

setup_xvfb() {
    if [ -n "${DISPLAY:-}" ] && [ "${FORCE_XVFB:-0}" = "0" ]; then
        log_info "Using existing DISPLAY=$DISPLAY"
        return 0
    fi

    log_info "Starting Xvfb on display $XVFB_DISPLAY..."

    # Kill any existing Xvfb on this display
    if [ -f "/tmp/.X${XVFB_DISPLAY#:}-lock" ]; then
        log_warn "Xvfb lock file exists, cleaning up..."
        rm -f "/tmp/.X${XVFB_DISPLAY#:}-lock" 2>/dev/null || true
    fi

    Xvfb "$XVFB_DISPLAY" -screen 0 1024x768x24 \
        -ac +extension GLX +render -noreset &
    XVFB_PID=$!
    export DISPLAY="$XVFB_DISPLAY"

    # Wait for Xvfb to be ready
    local retries=10
    while [ $retries -gt 0 ]; do
        if xdpyinfo -display "$XVFB_DISPLAY" >/dev/null 2>&1; then
            log_info "Xvfb ready on $XVFB_DISPLAY"
            return 0
        fi
        sleep 0.5
        retries=$((retries - 1))
    done

    log_fail "Xvfb failed to start"
    return 1
}

teardown_xvfb() {
    if [ -n "$XVFB_PID" ]; then
        log_info "Stopping Xvfb (PID $XVFB_PID)..."
        kill "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
        XVFB_PID=""
    fi
}

trap teardown_xvfb EXIT

# ================================================================
# Build
# ================================================================

build_game() {
    log_info "Building game engine..."
    cd "$SP_DIR"
    make BUILD_CLIENT=1 BUILD_SERVER=1 BUILD_GAME_SO=1 \
         BUILD_GAME_QVM=0 BUILD_RENDERER_REND2=0 \
         -j"$(nproc)" 2>&1 | tail -5

    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        log_fail "Game build failed"
        return 1
    fi
    log_pass "Game build succeeded"
}

build_tests() {
    log_info "Building test suite..."
    cd "$SCRIPT_DIR"
    make all 2>&1

    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        log_fail "Test build failed"
        return 1
    fi
    log_pass "Test build succeeded"
}

# ================================================================
# Test runners
# ================================================================

run_core_tests() {
    log_info "Running core subsystem tests..."
    echo ""
    if "$TEST_BUILD_DIR/test_core"; then
        log_pass "Core tests passed"
        return 0
    else
        log_fail "Core tests failed"
        return 1
    fi
}

run_renderer_tests() {
    log_info "Running headless renderer tests..."
    echo ""

    # Force software rendering for headless
    export LIBGL_ALWAYS_SOFTWARE=1
    export MESA_GL_VERSION_OVERRIDE=3.3

    if "$TEST_BUILD_DIR/test_renderer_headless"; then
        log_pass "Renderer tests passed"
        return 0
    else
        log_fail "Renderer tests failed"
        return 1
    fi
}

run_integration_tests() {
    log_info "Running engine integration tests..."
    echo ""

    export LIBGL_ALWAYS_SOFTWARE=1

    cd "$BUILD_DIR"
    if "$TEST_BUILD_DIR/test_engine_integration"; then
        log_pass "Integration tests passed"
        return 0
    else
        log_fail "Integration tests failed"
        return 1
    fi
}

run_debug_session() {
    log_info "Starting debug session..."
    export LIBGL_ALWAYS_SOFTWARE=1

    if ! command -v gdb >/dev/null 2>&1; then
        log_fail "GDB not found. Install with: apt install gdb"
        return 1
    fi

    cd "$BUILD_DIR"

    log_info "Running dedicated server under GDB..."
    gdb -batch -ex run -ex "thread apply all bt" -ex quit \
        --args ./iowolfspded.x86_64 \
        +set dedicated 1 \
        +set com_hunkmegs 64 \
        +set com_standalone 1 \
        +quit 2>&1

    echo ""
    log_info "Running client under GDB with Xvfb..."
    gdb -batch -ex run -ex "thread apply all bt" -ex quit \
        --args ./iowolfsp.x86_64 \
        +set r_fullscreen 0 \
        +set r_mode 3 \
        +set r_allowSoftwareGL 1 \
        +set com_hunkmegs 128 \
        +set s_initsound 0 \
        +set com_standalone 1 \
        +set in_nograb 1 \
        +quit 2>&1
}

# ================================================================
# Main
# ================================================================

main() {
    local mode="${1:-all}"

    echo "========================================"
    echo "iortcw-ng Automated Test Runner"
    echo "========================================"
    echo "Date: $(date)"
    echo "Mode: $mode"
    echo "Platform: $(uname -sm)"
    echo ""

    case "$mode" in
        --build)
            build_game
            build_tests
            setup_xvfb
            run_core_tests || EXIT_CODE=1
            run_renderer_tests || EXIT_CODE=1
            run_integration_tests || EXIT_CODE=1
            ;;
        --core)
            build_tests
            run_core_tests || EXIT_CODE=1
            ;;
        --renderer)
            build_tests
            setup_xvfb
            run_renderer_tests || EXIT_CODE=1
            ;;
        --integration)
            build_tests
            setup_xvfb
            run_integration_tests || EXIT_CODE=1
            ;;
        --debug)
            setup_xvfb
            run_debug_session || EXIT_CODE=1
            ;;
        all|"")
            build_tests
            setup_xvfb
            run_core_tests || EXIT_CODE=1
            run_renderer_tests || EXIT_CODE=1
            run_integration_tests || EXIT_CODE=1
            ;;
        *)
            echo "Usage: $0 [--build|--core|--renderer|--integration|--debug|all]"
            exit 1
            ;;
    esac

    echo ""
    echo "========================================"
    if [ $EXIT_CODE -eq 0 ]; then
        log_pass "All test suites completed successfully"
    else
        log_fail "Some tests failed (exit code $EXIT_CODE)"
    fi
    echo "========================================"

    exit $EXIT_CODE
}

main "$@"
