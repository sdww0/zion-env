#!/bin/bash
# Zion Enclave QEMU + GDB debug mode
#
# Usage:
#   ./run_qemu_debug.sh              # QEMU waits for GDB (port 1234)
#   ./run_qemu_debug.sh gdb          # Start QEMU and automatically connect GDB
#
# Workflow:
#   1. QEMU pauses at the first instruction (-S)
#   2. Connect GDB and use continue to run
#   3. Set breakpoints in SM or enclave code

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"
CROSS_COMPILE="$(zion_resolve_cross_compile)"
GDB="${GDB:-${CROSS_COMPILE}gdb}"

# ---- Paths ----
SM_FW="${SM_FW:-$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.bin}"
KERNEL="$(zion_resolve_native_kernel "$ZION_DIR" "$BUILD_DIR")"
OLD_ROOTFS="$(zion_resolve_zion_base_rootfs "$ZION_DIR" "$BUILD_DIR")"
DRIVER="$(zion_resolve_native_driver "$ZION_DIR" "$BUILD_DIR")"
TEST_BIN="$BUILD_DIR/test_zion_ioctl"
NEW_ROOTFS="$BUILD_DIR/zion_rootfs_9p.cpio"
SHARED_DIR="$BUILD_DIR/shared"

# OpenSBI ELF (with symbols for debugging SM in GDB)
SM_ELF="$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.elf"
# Eyrie runtime ELF
EYRIE_ELF="$BUILD_DIR/eyrie/eyrie-rt"

MODE="${1:-standalone}"
GDB_PORT="${GDB_PORT:-1234}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'
info() { echo -e "${YELLOW}[DBG]${NC} $1"; }
ok()   { echo -e "${GREEN}[OK]${NC} $1"; }

# ---- Checks ----
info "Checking files..."
for f in "$SM_FW" "$KERNEL" "$OLD_ROOTFS" "$DRIVER"; do
    [ -f "$f" ] || { echo -e "${RED}[FAIL]${NC} Missing: $f"; exit 1; }
    echo "  [OK] $(basename $f)"
done

[ -f "$SM_ELF" ] && ok "SM ELF: $SM_ELF" || info "SM ELF unavailable (debugging without symbols)"
[ -f "$EYRIE_ELF" ] && ok "Eyrie ELF: $EYRIE_ELF" || info "Eyrie ELF unavailable"

# ---- Prepare the 9p shared directory ----
mkdir -p "$SHARED_DIR"
cp "$DRIVER" "$SHARED_DIR/" 2>/dev/null
cp "$TEST_BIN" "$SHARED_DIR/" 2>/dev/null

for f in eyrie-rt loader.bin hello hello-runner; do
    [ -f "$BUILD_DIR/shared/$f" ] && cp "$BUILD_DIR/shared/$f" "$SHARED_DIR/" 2>/dev/null
done
for f in test-stack test-loop test-fibonacci test-malloc test-fib-bench test-runner; do
    [ -f "$BUILD_DIR/shared/$f" ] && cp "$BUILD_DIR/shared/$f" "$SHARED_DIR/" 2>/dev/null
done

# ---- Package rootfs (shared with the main script) ----
if [ ! -f "$NEW_ROOTFS" ] || [ "$OLD_ROOTFS" -nt "$NEW_ROOTFS" ]; then
    info "Packaging rootfs..."
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
echo "  Zion Enclave (DEBUG MODE)"
echo "========================================="
echo ""

mkdir -p /mnt
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt
if [ $? -ne 0 ]; then
    echo "[FAIL] 9p mount failed"
    poweroff -f
fi
echo "[OK] 9p mounted at /mnt"

echo "[OK] Loading zion-driver.ko ..."
insmod /mnt/zion-driver.ko
ls -la /dev/zion_enclave 2>/dev/null && echo "[OK] device node created"

echo ""
echo "[DEBUG] Driver loaded. Use GDB to set breakpoints."
echo "[DEBUG] Useful commands:"
echo "  /mnt/hello-runner /mnt/hello /mnt/eyrie-rt /mnt/loader.bin"
echo "  /mnt/test-runner /mnt/test-stack /mnt/eyrie-rt /mnt/loader.bin"
echo ""
echo "[DEBUG] Entering shell. Type 'poweroff -f' to exit."
echo ""
exec sh
INITEOF
    chmod +x "$TMPROOT/init"

    cd "$TMPROOT"
    find . | cpio -o -H newc 2>/dev/null > "$NEW_ROOTFS"
    cd "$SCRIPT_DIR"
    rm -rf "$TMPROOT"
fi

# ---- GDB startup script ----
GDB_SCRIPT="/tmp/gdb_zion_cmds"
cat > "$GDB_SCRIPT" << GDBEOF
set pagination off
set confirm off

# Connect to QEMU
target remote :$GDB_PORT

# Load SM symbols if available
GDBEOF

if [ -f "$SM_ELF" ]; then
    echo "add-symbol-file $SM_ELF" >> "$GDB_SCRIPT"
    echo "echo [GDB] SM symbols loaded: $SM_ELF\n" >> "$GDB_SCRIPT"
fi

if [ -f "$EYRIE_ELF" ]; then
    echo "add-symbol-file $EYRIE_ELF" >> "$GDB_SCRIPT"
    echo "echo [GDB] Eyrie symbols loaded: $EYRIE_ELF\n" >> "$GDB_SCRIPT"
fi

cat >> "$GDB_SCRIPT" << 'GDBEOF'

# Common breakpoints (comment out unwanted entries)
# break enclave_trap_handler
# break stop_enclave
# break run_enclave
# break resume_enclave
# break context_switch_to
# break context_switch_from
# break tee_dispatch_trap

echo [GDB] Ready. Type 'c' to continue boot.\n
echo [GDB] Useful breakpoints:\n
echo   break enclave_trap_handler\n
echo   break stop_enclave\n
echo   break run_enclave\n
echo   break context_switch_to\n
echo   break tee_dispatch_trap\n
echo \n
GDBEOF

# ---- QEMU arguments ----
QEMU="$(zion_resolve_outer_qemu "$ZION_DIR")"
QEMU_ARGS=(
    -machine virt -cpu rv64,sstc=false
    -m 4G -smp 2
    -bios "$SM_FW"
    -kernel "$KERNEL"
    -initrd "$NEW_ROOTFS"
    -append "console=ttyS0 root=/dev/ram rw memmap=128M\$0xF8000000"
    -fsdev local,id=shared,path="$SHARED_DIR",security_model=none
    -device virtio-9p-device,fsdev=shared,mount_tag=hostshare
    -nographic -no-reboot
    # GDB server
    -gdb "tcp::$GDB_PORT" -S
)

echo ""
info "Starting QEMU (debug mode)..."
echo "  SM:      $SM_FW"
echo "  Kernel:  $KERNEL"
echo "  GDB:     localhost:$GDB_PORT"
echo ""
echo -e "${CYAN}=========================================${NC}"
echo -e "${CYAN}  QEMU is paused, waiting for GDB${NC}"
echo -e "${CYAN}=========================================${NC}"
echo ""
echo "  Run in another terminal:"
echo ""
echo -e "    ${GREEN}$GDB -x $GDB_SCRIPT${NC}"
echo ""
echo "  Or directly:"
echo ""
echo -e "    ${GREEN}$GDB${NC}"
echo "    (gdb) target remote :$GDB_PORT"
if [ -f "$SM_ELF" ]; then
    echo "    (gdb) add-symbol-file $SM_ELF"
fi
echo "    (gdb) c"
echo ""

if [ "$MODE" = "gdb" ]; then
	command -v "$GDB" >/dev/null 2>&1 || {
		echo -e "${RED}[FAIL]${NC} Missing GDB: $GDB"
		exit 1
	}
    # Start QEMU in the background and GDB in the foreground
    "$QEMU" "${QEMU_ARGS[@]}" &
    QEMU_PID=$!
    sleep 1

    "$GDB" -x "$GDB_SCRIPT"

    # Stop QEMU when GDB exits
    kill $QEMU_PID 2>/dev/null; wait $QEMU_PID 2>/dev/null
    echo -e "${GREEN}[DONE]${NC} Debugging finished"
else
    # Start only QEMU; connect GDB manually
    exec "$QEMU" "${QEMU_ARGS[@]}"
fi
