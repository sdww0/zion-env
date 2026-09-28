#!/bin/sh
set -eu
cd "$(dirname "$0")"
. ./megrez-runtime.conf
mode=${1:-select}

if [ "$mode" = select ]; then
    mkdir -p state
    guest=${2:-}
    if [ -z "$guest" ]; then
        printf '%s\n' \
            '选择 Zion Guest 系统：' \
            '  1. Linux（SSH、virtio-net/blk、SQLite、enclave）' \
            '  2. Asterinas（SSH、virtio-net/blk、SQLite；enclave 不支持）'
        printf '请输入 1 或 2: '
        read -r guest
    fi
    case "$guest" in
        1|linux) guest=linux ;;
        2|asterinas) guest=asterinas ;;
        *) echo '无效选择，只接受 1/linux 或 2/asterinas。' >&2; exit 2 ;;
    esac
    printf '%s\n' "$guest" > state/guest-system
    echo "Selected Guest system: $guest"
    exit 0
fi

if [ "$mode" = guest ]; then
    [ -f state/guest-system ] || {
        echo '尚未选择 Guest；先运行 sh ./zion-runtime.sh select' >&2
        exit 2
    }
    read -r mode < state/guest-system
fi

case "$mode" in
    init)
        [ "$(id -u)" = 0 ] || { echo 'Run init as root' >&2; exit 1; }
        if [ -e /dev/tvm ]; then
            echo 'Driver already loaded; pool state is unknown. Do not reserve twice.' >&2
            exit 1
        fi
        insmod ./tvm-driver.ko
        ./tvm-control tvm "$POOL_PAGES"
        ;;
    linux|enclave|asterinas)
        mkdir -p state
        [ -e /dev/kvm ] && [ -e /dev/tvm ] || {
            echo 'Initialize Host driver and trusted pool first' >&2; exit 1;
        }
        if [ -f state/cvm.pid ]; then
            read -r pid < state/cvm.pid
            if kill -0 "$pid" 2>/dev/null; then
                echo "CVM PID $pid already running; stop it first" >&2; exit 1
            fi
        fi
        kernel=guest_kernel_image
        initrd=initrd-linux.img
        append='console=ttyS0 loglevel=7 memmap=2M$0x80000000 rdinit=/etc/zion-ssh.sh init=/etc/zion-ssh.sh'
        if [ "$mode" = asterinas ]; then
            kernel=asterinas_kernel
            initrd=initrd-asterinas.img
            append='SHELL=/bin/sh LOGNAME=root HOME=/ USER=root PATH=/bin:/usr/bin:/sbin:/usr/sbin:/benchmark/bin init=/etc/zion-ssh.sh ostd.log_level=error console=ttyS0'
        fi
        [ -f "$kernel" ] && [ -f "$initrd" ] || {
            echo "Missing runtime input: $kernel or $initrd" >&2; exit 1;
        }
        set --
        [ -f state/virtio-blk.img ] || truncate -s "$BLOCK_SIZE" state/virtio-blk.img
        set -- -drive file=state/virtio-blk.img,if=none,format=raw,id=vdisk \
            -device virtio-blk-device,drive=vdisk
        if [ "$mode" = asterinas ]; then
            net_device='virtio-net-device,netdev=net0,csum=off,guest_csum=off,gso=off,guest_tso4=off,guest_tso6=off,guest_ecn=off,guest_ufo=off,guest_uso4=off,guest_uso6=off,host_tso4=off,host_tso6=off,host_ecn=off,host_ufo=off,host_uso=off,mrg_rxbuf=off,ctrl_rx=off,ctrl_rx_extra=off,ctrl_vlan=off,ctrl_vq=off,ctrl_guest_offloads=off,ctrl_mac_addr=off,event_idx=off,queue_reset=off,guest_announce=off,indirect_desc=off,packed=off'
            set -- "$@" -netdev "user,id=net0,hostfwd=tcp:$SSH_LISTEN:$SSH_PORT-:22" \
                -device "$net_device"
        else
            set -- "$@" -netdev "user,id=net0,hostfwd=tcp:$SSH_LISTEN:$SSH_PORT-:22" \
                -device virtio-net-device,netdev=net0
        fi
        ./qemu-system-riscv64 -machine virt -cpu rv64 -m "$MEMORY" -smp "$VCPUS" \
            -enable-kvm -zion-cvm -kernel "./$kernel" -initrd "$initrd" \
            "$@" -display none -monitor none \
            -serial file:state/cvm.log -daemonize -pidfile state/cvm.pid \
            -append "$append"
        echo "QEMU started (not a Guest PASS). Log: $(pwd)/state/cvm.log"
        if [ "$mode" = asterinas ]; then
            echo "Asterinas: SSH -p $SSH_PORT root@127.0.0.1 ; password: debian"
            echo 'Asterinas supports virtio-net/blk and SQLite; Zion enclave requires Linux.'
        else
            echo "Host: ssh -p $SSH_PORT root@127.0.0.1 ; Guest password: debian"
            echo 'Guest: /usr/bin/run-zion-enclave-demo (same boot)'
        fi
        ;;
    status)
        tail -n 60 state/cvm.log
        ;;
    *)
        echo "Usage: $0 select [linux|asterinas]|init|guest|linux|enclave|asterinas|status"
        echo 'Initialize once per Host boot. Stop the CVM using its verified QEMU PID in state/cvm.pid.'
        [ "$mode" = help ] || exit 2
        ;;
esac
