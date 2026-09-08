#!/bin/bash
# Zion Enclave QEMU + GDB 调试模式
#
# 用法:
#   ./run_qemu_debug.sh              # QEMU 等待 GDB 连接 (端口 1234)
#   ./run_qemu_debug.sh gdb          # 同时启动 QEMU + GDB 自动连接
#
# 流程:
#   1. QEMU 启动后暂停在第一条指令 (-S)
#   2. GDB 连接后 continue 即可运行
#   3. 可在 SM 或 enclave 代码中设断点

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"
CROSS_COMPILE="$(zion_resolve_cross_compile)"
GDB="${GDB:-${CROSS_COMPILE}gdb}"

# ---- 路径 ----
SM_FW="${SM_FW:-$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.bin}"
KERNEL="$(zion_resolve_native_kernel "$ZION_DIR" "$BUILD_DIR")"
OLD_ROOTFS="$(zion_resolve_zion_base_rootfs "$ZION_DIR" "$BUILD_DIR")"
DRIVER="$(zion_resolve_native_driver "$ZION_DIR" "$BUILD_DIR")"
TEST_BIN="$BUILD_DIR/test_zion_ioctl"
NEW_ROOTFS="$BUILD_DIR/zion_rootfs_9p.cpio"
SHARED_DIR="$BUILD_DIR/shared"

# OpenSBI ELF (带符号表, 用于 GDB 调试 SM)
SM_ELF="$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.elf"
# Eyrie runtime ELF
EYRIE_ELF="$BUILD_DIR/eyrie/eyrie-rt"

MODE="${1:-standalone}"
GDB_PORT="${GDB_PORT:-1234}"

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'
info() { echo -e "${YELLOW}[DBG]${NC} $1"; }
ok()   { echo -e "${GREEN}[OK]${NC} $1"; }

# ---- 检查 ----
info "检查文件..."
for f in "$SM_FW" "$KERNEL" "$OLD_ROOTFS" "$DRIVER"; do
    [ -f "$f" ] || { echo -e "${RED}[FAIL]${NC} 缺少: $f"; exit 1; }
    echo "  ✓ $(basename $f)"
done

[ -f "$SM_ELF" ] && ok "SM ELF: $SM_ELF" || info "SM ELF 不存在 (无符号调试)"
[ -f "$EYRIE_ELF" ] && ok "Eyrie ELF: $EYRIE_ELF" || info "Eyrie ELF 不存在"

# ---- 准备9p共享目录 ----
mkdir -p "$SHARED_DIR"
cp "$DRIVER" "$SHARED_DIR/" 2>/dev/null
cp "$TEST_BIN" "$SHARED_DIR/" 2>/dev/null

for f in eyrie-rt loader.bin hello hello-runner; do
    [ -f "$BUILD_DIR/shared/$f" ] && cp "$BUILD_DIR/shared/$f" "$SHARED_DIR/" 2>/dev/null
done
for f in test-stack test-loop test-fibonacci test-malloc test-fib-bench test-runner; do
    [ -f "$BUILD_DIR/shared/$f" ] && cp "$BUILD_DIR/shared/$f" "$SHARED_DIR/" 2>/dev/null
done

# ---- 打包rootfs (与主脚本共用) ----
if [ ! -f "$NEW_ROOTFS" ] || [ "$OLD_ROOTFS" -nt "$NEW_ROOTFS" ]; then
    info "打包rootfs..."
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

# ---- GDB 启动脚本 ----
GDB_SCRIPT="/tmp/gdb_zion_cmds"
cat > "$GDB_SCRIPT" << GDBEOF
set pagination off
set confirm off

# 连接 QEMU
target remote :$GDB_PORT

# 加载 SM 符号 (如果有)
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

# 常用断点 (注释掉不需要的)
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

# ---- QEMU 参数 ----
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
info "启动 QEMU (调试模式) ..."
echo "  SM:      $SM_FW"
echo "  Kernel:  $KERNEL"
echo "  GDB:     localhost:$GDB_PORT"
echo ""
echo -e "${CYAN}=========================================${NC}"
echo -e "${CYAN}  QEMU 已暂停, 等待 GDB 连接${NC}"
echo -e "${CYAN}=========================================${NC}"
echo ""
echo "  在另一个终端运行:"
echo ""
echo -e "    ${GREEN}$GDB -x $GDB_SCRIPT${NC}"
echo ""
echo "  或直接:"
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
		echo -e "${RED}[FAIL]${NC} 缺少 GDB: $GDB"
		exit 1
	}
    # 后台启动 QEMU, 前台启动 GDB
    "$QEMU" "${QEMU_ARGS[@]}" &
    QEMU_PID=$!
    sleep 1

    "$GDB" -x "$GDB_SCRIPT"

    # GDB 退出后杀 QEMU
    kill $QEMU_PID 2>/dev/null; wait $QEMU_PID 2>/dev/null
    echo -e "${GREEN}[完成]${NC} 调试结束"
else
    # 只启动 QEMU, 用户手动连 GDB
    exec "$QEMU" "${QEMU_ARGS[@]}"
fi
