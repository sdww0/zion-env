# Zion Megrez 部署手册

步骤 1–5 在插入 SD 卡的电脑上执行，步骤 6 在 Megrez Host 上执行。
前提：SD 卡已有可启动的 RockOS，Host 已具备 QEMU 所需动态库。

## 1. 解包和核验

```sh
tar -xzf zion-megrez-release-20260917.tar.gz
cd zion-megrez-release-20260917
sha256sum -c SHA256SUMS
```

校验应全部为 `OK`，包名日期按实际替换。

## 2. 确认 SD 卡分区

先关闭板子再拔卡，将 SD 卡插到构建主机，然后执行：

```sh
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL
```

确认目标 SD 卡，将 `sdX1`、`sdXN` 替换为实际启动/rootfs 分区。
不要格式化或重新分区；已自动挂载的分区先正常卸载。

```sh
BOOT_DEV=/dev/sdX1
ROOT_DEV=/dev/sdXN
BOOT_MNT=/mnt/zion-megrez-boot
ROOT_MNT=/mnt/zion-megrez-root
sudo mkdir -p "$BOOT_MNT" "$ROOT_MNT"
sudo mount "$BOOT_DEV" "$BOOT_MNT"
sudo mount "$ROOT_DEV" "$ROOT_MNT"
findmnt "$BOOT_MNT"
findmnt "$ROOT_MNT"
sudo ls "$BOOT_MNT"
sudo ls "$ROOT_MNT"
```

确认启动分区有原有 boot 文件，rootfs 有 `etc`、`usr`、`root`。

## 3. 备份文件

备份到构建主机，不要只备份到同一张 SD 卡：

```sh
BACKUP="$HOME/megrez-sd-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP"
sudo cp -a "$BOOT_MNT" "$BACKUP/boot-partition"
if sudo test -d "$ROOT_MNT/root/zion-tests"; then
    sudo cp -a "$ROOT_MNT/root/zion-tests" "$BACKUP/zion-tests"
fi
```

这里只备份 SD 文件。另保留已能启动的固件和板子恢复流程。

## 4. 导入启动文件

```sh
sudo install -m 644 boot-partition/bootloader_secboot_ddr5_milkv_megrez.bin \
    "$BOOT_MNT/bootloader.bin"
sudo install -m 644 boot-partition/vmlinuz-6.6.87-win2030 "$BOOT_MNT/"
sudo install -m 644 boot-partition/initrd.img-6.6.87-win2030 "$BOOT_MNT/"
```

保留启动配置、root 参数和系统默认 DTB；确认配置引用上述 kernel/initramfs。
包内 Host initramfs 仅在 virt 验证，部署前确认板子存储适配。
复制 `bootloader.bin` 不等于刷入板载固件，固件更新使用原有成功流程。

## 5. 导入 CVM 配套文件

使用新目录，避免旧 initramfs、脚本或 binary 混入：

```sh
if sudo test -e "$ROOT_MNT/root/zion-tests"; then
    sudo mv "$ROOT_MNT/root/zion-tests" \
        "$ROOT_MNT/root/zion-tests.previous-$(date +%Y%m%d-%H%M%S)"
fi
sudo cp -a root-partition/root/zion-tests "$ROOT_MNT/root/"
sudo chown -R root:root "$ROOT_MNT/root/zion-tests"
sync
sudo umount "$ROOT_MNT"
sudo umount "$BOOT_MNT"
```

卸载成功后再拔卡，装回板子并启动 Host。卸载失败时不要强行拔卡。

## 6. Host 启动与初始化

```sh
cd /root/zion-tests
uname -r
ls /dev/kvm
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

版本应为 `6.6.87-win2030`，参数应为 `0`。参数不存在时检查是否启动新内核。

本次 Host 启动尚未加载驱动且尚未初始化内存池时执行一次：

```sh
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
rc=$?
printf 'init_exit=%s\n' "$rc"
ls /dev/tvm
```

成功应有 `RTVM_IOC_RESERVE_TVM_MEM`、`device_phys_addr=...`、
`reserve TVM SBI result: error=0`，且 `init_exit=0`。不能仅凭
`/dev/tvm` 存在判断成功。
每次 Host 启动只初始化一次，切换 CVM 模式不重复初始化。

先选择本轮 Guest：

```sh
sh ./zion-runtime.sh select
```

然后按[测试手册](TESTS_ZH.md)操作。Linux 与 Asterinas Guest 用户/密码均为
`root / debian`，SSH 转发只绑定 Host `127.0.0.1:10022`。enclave 测试只支持
Linux Guest。
