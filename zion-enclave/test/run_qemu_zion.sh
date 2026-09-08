#!/bin/bash
# Zion Enclave QEMU Boot & Test (9p shared folder版)
# This is the canonical native-enclave QEMU entry point.
#
# 用法:
#   ./run_qemu_zion.sh           # 自动测试模式
#   ./run_qemu_zion.sh manual    # 手动模式(进入shell)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"

# ---- 路径 ----
SM_FW="${SM_FW:-$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.bin}"
KERNEL="$(zion_resolve_native_kernel "$ZION_DIR" "$BUILD_DIR")"
OLD_ROOTFS="$(zion_resolve_zion_base_rootfs "$ZION_DIR" "$BUILD_DIR")"
DRIVER="$(zion_resolve_native_driver "$ZION_DIR" "$BUILD_DIR")"
DRIVER_BUILD_MANIFEST="${ZION_DRIVER_MANIFEST:-$DRIVER.manifest}"
NEW_ROOTFS="$BUILD_DIR/zion_rootfs_9p.cpio"
SHARED_DIR="$BUILD_DIR/shared"    # 9p共享目录
EYRIE_RT="$BUILD_DIR/eyrie/eyrie-rt"
EYRIE_LINUX_RT="$BUILD_DIR/eyrie-linux-test/output/eyrie-rt"
EYRIE_IO_RT="$BUILD_DIR/eyrie-io-test/output/eyrie-rt"
EYRIE_NET_RT="$BUILD_DIR/eyrie-net-test/output/eyrie-rt"
LOADER_BIN="$ZION_DIR/runtime/loader.bin"
HELLO_BIN="$BUILD_DIR/examples/hello"
HELLO_RUNNER="$BUILD_DIR/examples/hello-runner"
TEST_RUNNER="$BUILD_DIR/examples/test-runner"
NATIVE_CRYPTO_BIN="$BUILD_DIR/examples/native-crypto"
NATIVE_CRYPTO_RUNNER="$BUILD_DIR/examples/native-crypto-runner"
LINUX_MEMORY_BIN="$BUILD_DIR/examples/linux-memory"
LINUX_ABI_BIN="$BUILD_DIR/examples/linux-abi"
IO_VECTOR_BIN="$BUILD_DIR/examples/io-vector"
IO_FILE_BIN="$BUILD_DIR/examples/io-file"
IO_MULTIPLEX_BIN="$BUILD_DIR/examples/io-multiplex"
NET_LOOPBACK_BIN="$BUILD_DIR/examples/net-loopback"
NET_PSELECT_BIN="$BUILD_DIR/examples/net-pselect"
NATIVE_HANDLE_TEST="$BUILD_DIR/native-enclave-handles/native-enclave-handles.ko"
DRIVER_SECURITY_TEST="$BUILD_DIR/zion-driver-security"
QEMU_LOG="${ZION_RUN_LOG:-$BUILD_DIR/zion-qemu.log}"
LOG_VERIFIER="$ZION_DIR/scripts/verify_runtime_log.sh"

MODE="${1:-auto}"

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'
info() { echo -e "${YELLOW}[BOOT]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }

case "$MODE" in
    auto|manual) ;;
    *) fail "未知模式: $MODE（使用 auto 或 manual）" ;;
esac

# ---- 检查 ----
info "检查文件..."
for f in "$SM_FW" "$KERNEL" "$OLD_ROOTFS" "$DRIVER" \
         "$DRIVER_BUILD_MANIFEST" "$EYRIE_RT" \
         "$EYRIE_LINUX_RT" \
         "$EYRIE_IO_RT" \
         "$EYRIE_NET_RT" \
         "$LOADER_BIN" "$HELLO_BIN" "$HELLO_RUNNER" "$TEST_RUNNER" \
         "$NATIVE_CRYPTO_BIN" "$NATIVE_CRYPTO_RUNNER" "$LINUX_MEMORY_BIN" \
         "$LINUX_ABI_BIN" \
         "$IO_VECTOR_BIN" "$IO_FILE_BIN" "$IO_MULTIPLEX_BIN" \
         "$NET_LOOPBACK_BIN" "$NET_PSELECT_BIN"; do
    [ -f "$f" ] || fail "缺少: $f"
    echo "  ✓ $(basename $f) ($(du -h "$f" | cut -f1))"
done
[ -f "$NATIVE_HANDLE_TEST" ] || fail "缺少: $NATIVE_HANDLE_TEST"
CROSS_COMPILE="$(zion_resolve_cross_compile)"
command -v "${CROSS_COMPILE}gcc" >/dev/null ||
    fail "缺少: ${CROSS_COMPILE}gcc"
"${CROSS_COMPILE}gcc" -static -O2 -Wall -Wextra -Werror \
    "$SCRIPT_DIR/zion-driver-security.c" -o "$DRIVER_SECURITY_TEST"

# ---- 准备9p共享目录 ----
mkdir -p "$SHARED_DIR"
cp "$DRIVER" "$SHARED_DIR/"
cp "$DRIVER_BUILD_MANIFEST" "$SHARED_DIR/zion-driver.ko.build-manifest"

# 同步本轮实际执行的运行时、loader、runner 和测试程序。
cp "$EYRIE_RT" "$SHARED_DIR/eyrie-rt"
cp "$EYRIE_LINUX_RT" "$SHARED_DIR/eyrie-linux-test"
cp "$EYRIE_IO_RT" "$SHARED_DIR/eyrie-io-test"
cp "$EYRIE_NET_RT" "$SHARED_DIR/eyrie-net-test"
cp "$LOADER_BIN" "$SHARED_DIR/"
cp "$HELLO_BIN" "$HELLO_RUNNER" "$TEST_RUNNER" "$SHARED_DIR/"
cp "$NATIVE_CRYPTO_BIN" "$NATIVE_CRYPTO_RUNNER" "$SHARED_DIR/"
cp "$LINUX_MEMORY_BIN" "$SHARED_DIR/"
cp "$IO_VECTOR_BIN" "$SHARED_DIR/"
cp "$IO_FILE_BIN" "$SHARED_DIR/"
cp "$NET_LOOPBACK_BIN" "$SHARED_DIR/"
cp "$NATIVE_HANDLE_TEST" "$SHARED_DIR/"
cp "$DRIVER_SECURITY_TEST" "$SHARED_DIR/"
for test_bin in test-stack test-loop test-fibonacci test-malloc; do
    [ -x "$BUILD_DIR/examples/$test_bin" ] || fail "缺少: $BUILD_DIR/examples/$test_bin"
    cp "$BUILD_DIR/examples/$test_bin" "$SHARED_DIR/"
done

info "9p共享目录: $SHARED_DIR"

# ---- 打包rootfs (只需一次, 不含驱动/测试) ----
if [ ! -f "$NEW_ROOTFS" ] || [ "$OLD_ROOTFS" -nt "$NEW_ROOTFS" ] || \
   [ "$SCRIPT_DIR/run_qemu_zion.sh" -nt "$NEW_ROOTFS" ]; then
    info "打包rootfs (带9p支持的init)..."
    TMPROOT=$(mktemp -d)
    cd "$TMPROOT"
    cpio -idm < "$OLD_ROOTFS" 2>/dev/null

    cat > "$TMPROOT/init" << 'INITEOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

echo ""
echo "========================================="
echo "  Zion Enclave Test (9p mode)"
echo "========================================="
echo ""

# 挂载9p共享目录
mkdir -p /mnt
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt
if [ $? -ne 0 ]; then
    echo "[ZION-QEMU] FAIL: 9p mount"
    poweroff -f
fi
echo "[OK] 9p shared folder mounted at /mnt"

# 加载驱动
echo "[OK] 加载 zion-driver.ko ..."
if ! insmod /mnt/zion-driver.ko; then
    echo "[ZION-QEMU] FAIL: driver load"
    poweroff -f
fi
if [ ! -e /dev/zion_enclave ]; then
    echo "[ZION-QEMU] FAIL: device node missing"
    poweroff -f
fi
ls -la /dev/zion_enclave
echo "[OK] 设备节点已创建"

# The minimal initramfs does not run a network manager. Bring loopback up
# explicitly so the NET producer regression has a deterministic local target.
if ! /sbin/ip link set lo up; then
    echo "[ZION-QEMU] FAIL: loopback setup"
    poweroff -f
fi
echo "[OK] loopback interface enabled"

if grep -qw auto /proc/cmdline 2>/dev/null; then
    FAILURES=0
    run_case() {
        CASE_NAME="$1"
        shift
        echo ""
        echo "[RUN] $CASE_NAME"
        if "$@"; then
            echo "[PASS] $CASE_NAME"
        else
            CASE_RC=$?
            echo "[FAIL] $CASE_NAME (exit=$CASE_RC)"
            FAILURES=$((FAILURES + 1))
        fi
    }

    echo ""
    echo "[TEST] 原生 Zion enclave 生命周期"
    echo "-----------------------------------------"
    run_case hello /mnt/hello-runner /mnt/hello /mnt/eyrie-rt /mnt/loader.bin
    run_case test-stack /mnt/test-runner /mnt/test-stack /mnt/eyrie-rt \
        /mnt/loader.bin --utm-size 256 --freemem-size 256 --retval 12345
    run_case test-loop /mnt/test-runner /mnt/test-loop /mnt/eyrie-rt \
        /mnt/loader.bin --utm-size 256 --freemem-size 256 --retval 54321
    run_case test-fibonacci /mnt/test-runner /mnt/test-fibonacci /mnt/eyrie-rt \
        /mnt/loader.bin --utm-size 256 --freemem-size 256 --retval 14930352
    run_case test-malloc /mnt/test-runner /mnt/test-malloc /mnt/eyrie-rt \
        /mnt/loader.bin --utm-size 256 --freemem-size 256 --retval 11411
    run_case linux-memory /mnt/test-runner /mnt/linux-memory \
        /mnt/eyrie-linux-test /mnt/loader.bin --utm-size 256 \
        --freemem-size 512 --retval 24680
    run_case linux-abi /mnt/test-runner /mnt/linux-abi \
        /mnt/eyrie-linux-test /mnt/loader.bin --utm-size 256 \
        --freemem-size 256 --retval 11235
    run_case io-vector /mnt/test-runner /mnt/io-vector \
        /mnt/eyrie-io-test /mnt/loader.bin --utm-size 256 \
        --freemem-size 256 --retval 13579
    run_case io-file /mnt/test-runner /mnt/io-file \
        /mnt/eyrie-io-test /mnt/loader.bin --utm-size 256 \
        --freemem-size 256 --retval 31415
    run_case io-multiplex /mnt/test-runner /mnt/io-multiplex \
        /mnt/eyrie-io-test /mnt/loader.bin --utm-size 256 \
        --freemem-size 256 --retval 27182
    run_case net-loopback /mnt/test-runner /mnt/net-loopback \
        /mnt/eyrie-net-test /mnt/loader.bin --utm-size 256 \
        --freemem-size 256 --retval 86420
    run_case net-pselect /mnt/test-runner /mnt/net-pselect \
        /mnt/eyrie-net-test /mnt/loader.bin --utm-size 256 \
        --freemem-size 256 --retval 16180
    run_case native-crypto /mnt/native-crypto-runner /mnt/native-crypto \
        /mnt/eyrie-rt /mnt/loader.bin
    run_case native-handle-negative insmod /mnt/native-enclave-handles.ko
    rmmod native_enclave_handles 2>/dev/null || true
    run_case driver-security /mnt/zion-driver-security
    echo "-----------------------------------------"

    echo ""
    rmmod zion-driver 2>/dev/null || true
    if [ "$FAILURES" -eq 0 ]; then
        echo "[ZION-QEMU] PASS: all native enclave tests"
    else
        echo "[ZION-QEMU] FAIL: $FAILURES native enclave test(s)"
    fi
    echo "========================================="
    echo "  测试完成，关机"
    echo "========================================="
    poweroff -f
fi

# 手动模式
echo ""
echo "[manual] 驱动已加载，进入shell"
echo "[manual] 用完后 poweroff -f 退出"
echo ""
exec sh
INITEOF
    chmod +x "$TMPROOT/init"

    cd "$TMPROOT"
    find . | cpio -o -H newc 2>/dev/null > "$NEW_ROOTFS"
    cd "$SCRIPT_DIR"
    rm -rf "$TMPROOT"
    info "rootfs: $NEW_ROOTFS ($(du -h "$NEW_ROOTFS" | cut -f1))"
else
    info "rootfs已存在, 跳过打包"
fi

# ---- QEMU ----
QEMU="$(zion_resolve_outer_qemu "$ZION_DIR")"

echo ""
info "启动 QEMU ..."
echo "  SM:      $SM_FW"
echo "  Kernel:  $KERNEL"
echo "  9p:      $SHARED_DIR → /mnt"
echo ""

if [ "$MODE" = "manual" ]; then
    # 手动模式：加载驱动后进入shell
    exec "$QEMU" \
        -machine virt -cpu rv64,sstc=false \
        -m 8G -smp 2 \
        -bios "$SM_FW" \
        -kernel "$KERNEL" \
        -initrd "$NEW_ROOTFS" \
        -append "console=ttyS0 root=/dev/ram rw memmap=256M\$0xF8000000" \
        -fsdev local,id=shared,path="$SHARED_DIR",security_model=none \
        -device virtio-9p-device,fsdev=shared,mount_tag=hostshare \
        -nographic -no-reboot
else
    # 自动模式：保留串口直连，同时让超时或 QEMU 失败传播给调用者。
    ARTIFACT_MANIFEST="${ZION_ARTIFACT_MANIFEST:-$QEMU_LOG.artifacts.sha256}"
    command -v sha256sum >/dev/null || fail "缺少 sha256sum"
    [ -x "$LOG_VERIFIER" ] || fail "缺少运行日志验证器: $LOG_VERIFIER"
	sha256sum "$QEMU" "$SM_FW" "$KERNEL" "$NEW_ROOTFS" \
	        "$SHARED_DIR/zion-driver.ko" \
	        "$SHARED_DIR/zion-driver.ko.build-manifest" \
        "$SHARED_DIR/eyrie-rt" "$SHARED_DIR/eyrie-linux-test" \
        "$SHARED_DIR/eyrie-io-test" \
        "$SHARED_DIR/eyrie-net-test" \
        "$SHARED_DIR/loader.bin" \
        "$SHARED_DIR/hello" "$SHARED_DIR/hello-runner" \
        "$SHARED_DIR/test-runner" "$SHARED_DIR/native-crypto" \
        "$SHARED_DIR/native-crypto-runner" \
        "$SHARED_DIR/linux-memory" "$SHARED_DIR/linux-abi" \
        "$SHARED_DIR/io-vector" \
        "$SHARED_DIR/io-file" "$SHARED_DIR/io-multiplex" \
        "$SHARED_DIR/net-loopback" "$SHARED_DIR/net-pselect" \
        "$SHARED_DIR/native-enclave-handles.ko" \
        "$SHARED_DIR/zion-driver-security" \
        "$SHARED_DIR/test-stack" "$SHARED_DIR/test-loop" \
        "$SHARED_DIR/test-fibonacci" "$SHARED_DIR/test-malloc" \
        > "$ARTIFACT_MANIFEST"
    MANIFEST_DIGEST="$(sha256sum "$ARTIFACT_MANIFEST" | awk '{print $1}')"
    printf '[ZION EVIDENCE] artifact manifest sha256: %s\n' \
        "$MANIFEST_DIGEST" > "$QEMU_LOG"
    set +e
    timeout --foreground 240s "$QEMU" \
        -machine virt -cpu rv64,sstc=false \
        -m 4G -smp 2 \
        -bios "$SM_FW" \
        -kernel "$KERNEL" \
        -initrd "$NEW_ROOTFS" \
        -append "console=ttyS0 root=/dev/ram rw memmap=128M\$0xF8000000 auto" \
        -fsdev local,id=shared,path="$SHARED_DIR",security_model=none \
        -device virtio-9p-device,fsdev=shared,mount_tag=hostshare \
        -nographic -no-reboot 2>&1 | tee -a "$QEMU_LOG"
    QEMU_RC=${PIPESTATUS[0]}
    set -e

    [ "$QEMU_RC" -eq 0 ] || fail "QEMU测试失败或超时 (exit=$QEMU_RC)"
    "$LOG_VERIFIER" native "$QEMU_LOG" "$ARTIFACT_MANIFEST"

    echo ""
    echo -e "${GREEN}[完成]${NC} QEMU测试通过（日志: $QEMU_LOG）"
fi
