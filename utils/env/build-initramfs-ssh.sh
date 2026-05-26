#! /bin/bash

set -euo pipefail

BASE_INITRD="${BASE_INITRD:-utils/env/initrd.img}"
WORK_DIR="${WORK_DIR:-utils/env/initramfs-ssh}"
OUT_INITRD="${OUT_INITRD:-utils/env/initrd-ssh.img}"
PKG_BASE="${PKG_BASE:-http://ports.ubuntu.com/ubuntu-ports}"

packages=(
	"pool/universe/d/dropbear/dropbear-bin_2020.81-5_riscv64.deb"
	"pool/universe/libt/libtomcrypt/libtomcrypt1_1.18.2-5_riscv64.deb"
	"pool/main/libt/libtommath/libtommath1_1.2.0-6build3_riscv64.deb"
	"pool/main/g/gmp/libgmp10_6.2.1+dfsg-3ubuntu1_riscv64.deb"
)

if [ ! -f "$BASE_INITRD" ]; then
	echo "missing base initrd: $BASE_INITRD" >&2
	exit 1
fi

BASE_INITRD="$(realpath "$BASE_INITRD")"
WORK_DIR="$(realpath -m "$WORK_DIR")"
OUT_INITRD="$(realpath -m "$OUT_INITRD")"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/root" "$WORK_DIR/pkgs"

(
	cd "$WORK_DIR/root"
	gzip -dc "$BASE_INITRD" | cpio -id --quiet
)

for pkg in "${packages[@]}"; do
	dst="$WORK_DIR/pkgs/$(basename "$pkg")"
	if [ ! -f "$dst" ]; then
		wget -q -O "$dst" "$PKG_BASE/$pkg"
	fi
	dpkg-deb -x "$dst" "$WORK_DIR/root"
done

mkdir -p "$WORK_DIR/root/etc/dropbear" "$WORK_DIR/root/run" \
	"$WORK_DIR/root/var/run" "$WORK_DIR/root/tmp" "$WORK_DIR/root/dev/pts"
chmod 1777 "$WORK_DIR/root/tmp"

cat > "$WORK_DIR/root/etc/passwd" <<'EOF'
root:x:0:0:root:/:/bin/sh
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
EOF

cat > "$WORK_DIR/root/etc/group" <<'EOF'
root:x:0:
nogroup:x:65534:
EOF

cat > "$WORK_DIR/root/etc/shadow" <<'EOF'
root:$6$zion$UcTeObOJ/IVDYcl1nnnZC8rfNez0WK3f1i.btDgAsLglyBsXRexb5FrtsxzpUnpNmIOUVUAyvnN8ytTyQDbaY1:19723:0:99999:7:::
EOF
chmod 600 "$WORK_DIR/root/etc/shadow"

cat > "$WORK_DIR/root/etc/resolv.conf" <<'EOF'
nameserver 10.0.2.3
EOF

cat > "$WORK_DIR/root/etc/zion-ssh.sh" <<'EOF'
#! /bin/sh

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts /run /var/run /tmp /etc/dropbear
mount -t devpts devpts /dev/pts 2>/dev/null || true
chmod 1777 /tmp

hostname zion-cvm
ip link set lo up 2>/dev/null || ifconfig lo up
ip link set eth0 up 2>/dev/null || ifconfig eth0 up
ip addr add 10.0.2.15/24 dev eth0 2>/dev/null || ifconfig eth0 10.0.2.15 netmask 255.255.255.0
ip route add default via 10.0.2.2 dev eth0 2>/dev/null || route add default gw 10.0.2.2 2>/dev/null || true

if [ ! -s /etc/dropbear/dropbear_rsa_host_key ]; then
	/usr/bin/dropbearkey -t rsa -f /etc/dropbear/dropbear_rsa_host_key
fi
if [ ! -s /etc/dropbear/dropbear_ecdsa_host_key ]; then
	/usr/bin/dropbearkey -t ecdsa -f /etc/dropbear/dropbear_ecdsa_host_key
fi

echo "Zion CVM SSH is starting on port 22."
echo "Default login: root / debian"
echo "From the host running this nested QEMU: ssh -p 10022 root@127.0.0.1"
echo "Network status:"
ip -br addr show eth0 2>/dev/null || ifconfig eth0 2>/dev/null || true
echo "Virtio block device status:"
ls -l /dev/vd* 2>/dev/null || true

SSHD_ADDR="${SSHD_ADDR:-10.0.2.15}"
/usr/sbin/dropbear -E -R -p "$SSHD_ADDR:22"
exec /bin/sh
EOF
chmod +x "$WORK_DIR/root/etc/zion-ssh.sh"

(
	cd "$WORK_DIR/root"
	find . -print0 | cpio --null -o -H newc --owner=0:0 --quiet | gzip -9 > "$OUT_INITRD"
)

echo "wrote $OUT_INITRD"
