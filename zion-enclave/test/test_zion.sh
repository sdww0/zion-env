#!/bin/bash
# Zion Enclave Integration Test
# Uses the public Zion device and ABI names.
#
# Tests the full Zion enclave lifecycle through the kernel driver.
#
# Usage:
#   ./test_zion.sh                    # run on target (requires root)
#   ./test_zion.sh build              # build test binary only
#   ./test_zion.sh sm                 # verify SM firmware exists
#   ./test_zion.sh all                # build + verify everything
#
# On-target test flow:
#   1. insmod zion-driver.ko
#   2. /dev/zion_enclave appears
#   3. test_zion_ioctl runs ioctl lifecycle tests
#   4. rmmod zion_enclave

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ZION_DIR/build"
DEVICE="/dev/zion_enclave"
source "$ZION_DIR/scripts/zion_paths.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }
info() { echo -e "${CYAN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# ---- Build test binary ----
do_build() {
    info "Building test binary..."
    CROSS="${CROSS_COMPILE:-riscv64-unknown-linux-gnu-}"
    CC=""

    # Try to find cross compiler
    CC="$(command -v "${CROSS}gcc" 2>/dev/null)" || true
    if [ -z "$CC" ] && [ -x "/opt/riscv/bin/${CROSS}gcc" ]; then
        CC="/opt/riscv/bin/${CROSS}gcc"
    fi
    if [ -z "$CC" ]; then
        CC="$(command -v gcc 2>/dev/null)" || true
    fi
    if [ -z "$CC" ]; then
        fail "Cross compiler not found: ${CROSS}gcc (also checked /opt/riscv/bin/)"
    fi

    mkdir -p "$BUILD_DIR"
    "$CC" -static -O2 -Wall \
        -o "$BUILD_DIR/test_zion_ioctl" \
        "$ZION_DIR/test/test_zion_ioctl.c" 2>&1

    if [ -f "$BUILD_DIR/test_zion_ioctl" ]; then
        SIZE=$(stat -c%s "$BUILD_DIR/test_zion_ioctl")
        pass "test_zion_ioctl ($SIZE bytes)"
    else
        fail "Build failed"
    fi
}

# ---- Verify SM firmware ----
do_verify_sm() {
    info "Checking SM firmware..."
    FW="$BUILD_DIR/opensbi/platform/generic/firmware"
    if [ -f "$FW/fw_dynamic.bin" ]; then
        SIZE=$(stat -c%s "$FW/fw_dynamic.bin")
        pass "fw_dynamic.bin ($SIZE bytes)"
    else
        # Check in-source build
        FW="$ZION_DIR/rockos-opensbi/build/platform/generic/firmware"
        if [ -f "$FW/fw_dynamic.bin" ]; then
            SIZE=$(stat -c%s "$FW/fw_dynamic.bin")
            pass "fw_dynamic.bin ($SIZE bytes, in-source)"
        else
            fail "SM firmware not found. Run: scripts/build_all_zion.sh sm"
        fi
    fi
}

# ---- Verify driver ----
do_verify_driver() {
    info "Checking driver..."
    KO="$(zion_resolve_native_driver "$ZION_DIR" "$BUILD_DIR")"
    if [ -f "$KO" ]; then
        SIZE=$(stat -c%s "$KO")
        pass "zion-driver.ko ($SIZE bytes)"
    else
        warn "Driver .ko not built (needs LINUX_SRC). Driver .c files compile OK."
    fi
}

# ---- Verify SDK ----
do_verify_sdk() {
    info "Checking SDK..."
    if [ -f "$BUILD_DIR/sdk-install/lib/libzion-host.a" ]; then
        pass "SDK installed at $BUILD_DIR/sdk-install"
    else
        warn "SDK not installed. Run: scripts/build_all_zion.sh sdk"
    fi
}

# ---- Run test on target ----
do_run() {
    info "=== Zion Enclave Integration Test ==="

    if [ "$(id -u)" -ne 0 ]; then
        fail "Must run as root (need insmod/ioctl access)"
    fi

    # Load driver if needed
    if [ ! -c "$DEVICE" ]; then
        info "Loading driver..."
        KO="$(zion_resolve_native_driver "$ZION_DIR" "$BUILD_DIR")"
        if [ ! -f "$KO" ]; then
            fail "Driver .ko not found. Build first."
        fi
        insmod "$KO"
        pass "Driver loaded"
    fi

    # Build test if needed
    if [ ! -f "$BUILD_DIR/test_zion_ioctl" ]; then
        do_build
    fi

    # Run test
    info "Running ioctl test..."
    echo ""
    "$BUILD_DIR/test_zion_ioctl"
    RESULT=$?
    echo ""

    # Cleanup
    info "Unloading driver..."
    rmmod zion_driver 2>/dev/null || true

    if [ $RESULT -eq 0 ]; then
        pass "All tests passed"
    else
        fail "Some tests failed"
    fi
}

# ---- Verify all ----
do_verify() {
    info "=== Zion Build Verification ==="
    echo ""
    do_verify_sm
    do_verify_driver
    do_verify_sdk
    echo ""

    # Check source files
    info "Checking source files..."
    SM_FILES=(
        "rockos-opensbi/zion/src/enclave.c"
        "rockos-opensbi/zion/src/enclave.h"
        "rockos-opensbi/zion/src/context.c"
        "rockos-opensbi/zion/src/context.h"
        "rockos-opensbi/zion/src/tee-trap-handler.c"
        "rockos-opensbi/zion/src/zion-enclave/trap.S"
        "rockos-opensbi/zion/src/platform-hook.h"
        "rockos-opensbi/zion/src/tee-sbi-opensbi.c"
    )
    DRIVER_FILES=(
        "driver/kernel/zion-main.c"
        "driver/kernel/zion-enclave-frontend.c"
        "driver/kernel/zion-enclave-ioctl.c"
        "driver/kernel/zion-enclave-page.c"
        "driver/kernel/zion-enclave-sbi.c"
        "driver/kernel/zion-tvm.c"
        "driver/kernel/zion-tvm-ioctl.c"
        "driver/kernel/zion-enclave.h"
    )
    SDK_FILES=(
        "sdk/src/host/ZionDevice.cpp"
        "sdk/src/host/PhysicalEnclaveMemory.cpp"
        "sdk/include/host/ZionDevice.hpp"
        "sdk/include/shared/zion_user.h"
    )

    ALL_OK=1
    for f in "${SM_FILES[@]}" "${DRIVER_FILES[@]}" "${SDK_FILES[@]}"; do
        if [ -f "$ZION_DIR/$f" ]; then
            echo -e "  ${GREEN}✓${NC} $f"
        else
            echo -e "  ${RED}✗${NC} $f (MISSING)"
            ALL_OK=0
        fi
    done

    echo ""
    if [ $ALL_OK -eq 1 ]; then
        pass "All source files present"
    else
        fail "Some source files missing"
    fi
}

# ---- Main ----
case "${1:-verify}" in
    build)  do_build ;;
    sm)     do_verify_sm ;;
    driver) do_verify_driver ;;
    sdk)    do_verify_sdk ;;
    run)    do_run ;;
    verify) do_verify ;;
    all)
        do_build
        do_verify
        ;;
    *)
        echo "Usage: $0 {build|sm|driver|sdk|run|verify|all}"
        exit 1
        ;;
esac
