# Milk-V Megrez 部署与双 Guest 测试

本说明适用于运行包的 `boot-partition/` 和
`root-partition/root/zion-tests/` 布局。命令分别标为“构建主机”“Host”和
“Guest”，不要在错误的系统中执行。现有 SD 卡必须已经包含可启动的
RockOS；本包不是完整 rootfs，不负责格式化或重新分区。

## 1. 检查运行包

在构建主机解压，然后进入解压后的运行包根目录：

```sh
sha256sum -c SHA256SUMS
cat FIRMWARE_STATUS.txt
```

所有文件应显示 `OK`。查看 `VALIDATION.md`（如有）了解实际测试范围。
新固件包含 ZION ASCII logo、OpenSBI 版本和初始化状态、一次性 vCPU 异常
告警。新版 Host kernel 另包含默认关闭的受控篡改测试参数。

固件使用不安全的测试密钥，仅供实验。`identity/` 中是对应设备公钥；
不能沿用旧公钥进行新固件的远程证明。演示程序不执行远程证明。

## 2. 向目标 SD 卡导入文件

以下命令在插入 SD 卡的构建主机执行。先关闭板子再拔卡，不要在板子运行时
直接移除系统卡。确认磁盘，不能照抄旧脚本的 `/dev/sdb` 或 `/dev/sdc`：

```sh
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL
```

启动分区与 rootfs 分区的实际编号可能是 1/2，也可能是 1/3。根据现有
布局填写下面两个变量；`sdX` 和 `N` 都是占位符，必须替换。

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

确认第一处是原有启动文件，第二处是 Linux rootfs（有 `etc`、`usr`、`root`）。
若系统已自动挂载这些分区，应先卸载自动挂载点，或使用现有挂载点，不要重复
挂载后不清楚实际写入位置。不要运行 `mkfs`、`dd` 或分区工具。

覆盖前，把启动分区和已有测试目录备份到构建主机，而不是放回同一张 SD 卡：

```sh
BACKUP="$HOME/megrez-sd-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP"
sudo cp -a "$BOOT_MNT" "$BACKUP/boot-partition"
if sudo test -d "$ROOT_MNT/root/zion-tests"; then
    sudo cp -a "$ROOT_MNT/root/zion-tests" "$BACKUP/zion-tests"
fi
```

这只是 SD 卡文件备份，**不是板载 SPI/eMMC 内已刷入 bootloader 的备份**。
同时保留先前能启动的固件包，用于已知的板子恢复流程。

从运行包根目录导入启动文件：

```sh
sudo install -m 644 boot-partition/bootloader_secboot_ddr5_milkv_megrez.bin \
    "$BOOT_MNT/bootloader.bin"
sudo install -m 644 boot-partition/vmlinuz-6.6.87-win2030 "$BOOT_MNT/"
sudo install -m 644 boot-partition/initrd.img-6.6.87-win2030 "$BOOT_MNT/"
```

`bootloader.bin` 是现有工程脚本使用的 SD 卡固件文件名。复制到卡上不等于
刷入板载固件：如果现有板子流程还要求手动更新 bootloader，按此前已成功的
刷写流程执行。本说明不提供未经确认的 SPI/eMMC 原始写入命令。

DTB 的位置由现有启动配置决定。先查看已有配置：

```sh
sudo find "$BOOT_MNT" -maxdepth 3 -type f \
    \( -name extlinux.conf -o -name '*.dtb' -o -name boot.scr \) -print
```

只有配置明确使用启动分区根目录的 `eic7700-milkv-megrez.dtb` 时，才执行：

```sh
sudo install -m 644 boot-partition/eic7700-milkv-megrez.dtb "$BOOT_MNT/"
```

若配置引用其他子目录，替换实际引用的同名文件。不要仅复制新 DTB 后就认定
启动时使用了它。保留原有启动配置、根分区参数、Host 模块和系统设施；检查
配置中的 kernel/initramfs 文件名是否与上述文件一致，不要盲目替换配置。
包内 Host initramfs 在 virt 中验证过，但没有证明它适配所有板子的存储布局；
如需继续使用板子已验证的同版本 Host initramfs，应明确记录这种差异。

导入 rootfs 中的 CVM 配套文件；使用新目录避免混入旧脚本或旧 initramfs：

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

等待卸载成功再拔卡。若卸载提示 busy，先退出挂载目录或关闭占用程序，不要
强行拔卡。这里不更改 rootfs 的其他目录，也不重新格式化 SD 卡。

## 3. 网络传输更新（可选）

Host SSH 已配置后，可免拔卡传输。示例板子 IP 为 `10.16.25.118`，需以实际
地址为准。先确认远端 `/root/zion-tests-new` 不存在，然后在构建主机执行：

```sh
scp -r root-partition/root/zion-tests root@10.16.25.118:/root/zion-tests-new
```

在 Host 停止旧 CVM、确认它已经退出之后，再把旧目录改名备份，并将
`/root/zion-tests-new` 改名为 `/root/zion-tests`。不要覆盖仍运行的 QEMU binary
或镜像。只更新 Guest/QEMU 文件无需刷固件；新增 Host 篡改入口则必须更新
Host kernel 并重启。bootloader 的更新仍使用板子已验证的固件更新流程。

## 4. Host 启动检查与一次性初始化

启动串口应出现 ASCII ZION logo，以及：

```text
[ZION BOOT] OpenSBI=1.5 hart=... H=yes
[ZION BOOT] identity=INSECURE-TEST-KEYS not-for-production
[ZION BOOT] state=INITIALIZING (not a security-test PASS)
[ZION BOOT] state=READY CVM/enclave SBI registered; runtime validation still required
```

这些是物理串口上的 OpenSBI 输出，通常不进入 Linux `dmesg`。进入 Host 后：

```sh
cd /root/zion-tests
uname -r
ls /dev/kvm
ls -l /sys/module/kvm/parameters/zion_vcpu_tamper
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

测试参数默认输出 `0`。若不存在，当前启动的 Host kernel 不包含新注入入口；
仅更新 rootfs 的脚本不足以启用它。Host kernel 应为 `6.6.87-win2030`。

本次 Host 启动后尚未加载 TVM 驱动、尚未初始化内存池时执行：

```sh
sh ./zion-runtime.sh init
```

预期包含：

```text
[tvm-control] type=tvm, count=0x30d40
ioctl(): RTVM_IOC_RESERVE_TVM_MEM
[tvm-driver] rtvm_reserve_tvm_mem(): ... device_phys_addr=...
```

地址不固定。**每次 Host 启动仅初始化一次**。`/dev/tvm` 存在只证明驱动已
加载，不证明内存池初始化成功。不要反复 reserve；状态不明时先查此前日志，
必要时重新启动 Host，不能把重复初始化当成修复方法。

## 5. 测试一：Host 主动篡改 vCPU 共享通道

建议本次 Host 启动后先做这一项。Zion 异常告警每次启动全局只输出一次；
此前若已出现告警，重复注入不会再次打印，需要重启 Host 才能重新观察。
测试入口默认关闭，仅 root 可写。参数 `1` 会使下一次 CVM SBI exit 发生
一次受控 S3 篡改，之后自动变回 `0`。普通 VM 不消费该测试请求。

先选择 Linux 或 Asterinas，再启动所选 CVM：

```sh
cd /root/zion-tests
sh ./zion-runtime.sh select
echo 1 > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh guest
```

Host kernel 的 `dmesg` 应出现：

```text
[ZION HOST TEST] controlled-tamper cvm=... vcpu=... register=S3 value=0x5a494f4e; one-shot injection consumed
```

物理串口应出现 Zion 的独立检测输出：

```text
[ZION VCPU ALERT] hart=... source=HOST-SHARED-CHANNEL register=S3 expected=0x0 observed=0x5a494f4e
[ZION VCPU ALERT] classification=SUSPECTED-TAMPER-OR-ABI-MISMATCH not proof of malicious intent; private saved state is retained
```

查看 Host 注入记录及自动关闭状态：

```sh
dmesg | grep -F '[ZION HOST TEST]'
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

参数应为 `0`。若仍为 `1`，尚未消费请求，不能声称检测通过；可先检查 CVM
启动日志。取消尚未发生的请求可写入 `0`。若 Host 注入打印了但 Zion 告警
没有出现，检查是否已消费本次启动的一次性告警、是否使用新版固件，并保存
物理串口日志，不要把缺失日志判为成功。

本项功能测试的 PASS 条件：Host 注入记录与 Zion 检测记录的 S3 值一致、
请求消费后参数归零，且接下来的 Guest SSH/块设备测试仍通过。

该测试修改的是 Host 内存中的 vCPU **共享通道**，并非让 Host 直接访问
monitor 私有寄存器，也不是直接修改物理 CPU 的实时 S3。它模拟 Host 恶意
篡改该通道，展示 Zion 检测；普通告警本身不能证明恶意意图。当前未包含
Guest 私有 S3 的定值断言，因此不能宣称完成所有 vCPU 状态完整性验证。

## 6. 测试二：共享内存与 virtio 网络/块设备

若测试一已经启动所选 CVM，不要再启动第二个。否则在 Host 执行：

```sh
sh ./zion-runtime.sh guest
```

预期：

```text
QEMU started (not a Guest PASS). Log: /root/zion-tests/state/cvm.log
Host: ssh -p 10022 root@127.0.0.1 ; Guest password: debian
```

等待 Guest 生成密钥并启动 Dropbear，多次查看有限行数日志即可：

```sh
sh ./zion-runtime.sh status
```

不要在唯一的串口终端用 `tail -f` 长期占用终端。预期 Guest 日志包含：

```text
Zion CVM SSH is starting on port 22.
Default login: root / debian
```

在 Host 连接 Guest：

```sh
ssh -p 10022 root@127.0.0.1
```

Guest 密码是 `debian`。默认转发仅绑定 Host 的 `127.0.0.1`，不是板子的
外网接口。如果替换镜像后遇到已确认的 host key 变化，先退出并在 Host 执行：

```sh
ssh-keygen -R '[127.0.0.1]:10022'
```

只在确认镜像更新导致密钥变化后删除记录；不要无条件忽略身份告警。
SSH 成功后，以下命令在 Guest 中执行：

```sh
uname -a
echo '[TEST] PASS: SSH command executed inside CVM'
ifconfig eth0
ls -l /dev/vda
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
rc=$?
echo "blk_read_exit=$rc"
wc -c /tmp/blk-read
```

Linux 预期内核为 `Linux zion-cvm 6.6.88+ ... riscv64 GNU/Linux`；Asterinas
当前兼容输出包含 `Linux zion-cvm 5.13.0`，并应在串口出现 Asterinas banner。
网卡为 `UP`，默认地址通常是 `10.0.2.15`。块设备测试预期：

```text
1+0 records in
1+0 records out
blk_read_exit=0
512 /tmp/blk-read
```

PASS 条件：SSH 内命令执行成功且块设备实际读取成功。可用网络/块设备是
共享数据交换路径工作的功能证据，不是共享内存安全边界的完整证明。

## 7. 模式切换与停止 CVM

先在 Guest 执行 `exit` 返回 Host。不能只退出 SSH 就认为 CVM 已关闭。

```sh
cd /root/zion-tests
pid=$(cat state/cvm.pid)
ps -p "$pid" -o pid,args
```

确认 PID 属于这个目录启动的 Zion QEMU，再执行：

```sh
kill "$pid"
sleep 2
ps -p "$pid" -o pid,args
```

若仍运行，继续等待并检查日志，不要立即启动第二个 CVM或使用无差别
`killall qemu`。保存本轮日志，防止下次启动覆盖：

```sh
cp state/cvm.log state/linux-$(date +%Y%m%d-%H%M%S).log
```

切换模式不需要重复执行 `init`，可信内存池仍保留在 Host 中。

## 8. 测试三：CVM 与 enclave 融合（仅 Linux Guest）

Asterinas 当前没有 `/dev/zion_enclave` frontend，本项不适用于 Asterinas。
停止当前 Asterinas CVM 后，在 Host 启动 Linux enclave 模式：

```sh
sh ./zion-runtime.sh enclave
sh ./zion-runtime.sh status
ssh -p 10022 root@127.0.0.1
```

等待 SSH 启动，密码仍为 `debian`。在 Guest 执行：

```sh
/usr/bin/run-zion-enclave-demo
rc=$?
echo "demo_exit=$rc"
```

预期逐步输出：

```text
[ZION DEMO] CREATE nested enclave; ten verified compute/OCALL rounds
[ZION ENCLAVE] COMPUTE  1/10 checksum=0x6d5f6a3f4f50b24e VERIFIED
[CVM DEMO] shared payload overwritten; pause 1s, then resume enclave
...
[ZION ENCLAVE] COMPUTE 10/10 checksum=0xa2d5c68b989c3aae VERIFIED
[ZION DEMO] PASS: private checksum continuity across 10 OCALLs; destroyed
[DRIVER SECURITY] PASS: cross-file enclave access rejected
[DRIVER SECURITY] PASS: out-of-EPM mmap rejected
...
[DRIVER SECURITY] PASS: all negative ABI tests
[ZION DEMO] PASS: compute lifecycle and driver negative ABI tests
[ZION DEMO] SKIP: arbitrary private-page access; dedicated safe probe required
demo_exit=0
```

PASS 条件：10 轮均为 `VERIFIED`、enclave 正常销毁、驱动负向测试均通过、
退出码为 `0`。每轮一秒暂停在父 CVM 程序中，不代表 enclave 连续执行一秒。
共享 payload 覆盖与驱动 ABI 拒绝不是任意 enclave 私有物理页访问被硬件
拒绝的证明；私有页隔离探测和远程证明仍未覆盖。

## 9. 日志保存与常见问题

物理串口记录：OpenSBI logo、READY 状态和 vCPU 检测告警。
Host `dmesg`：Host 的受控注入日志及驱动日志。
Host `state/cvm.log`：Guest 启动与串口输出。
SSH 终端记录：在 SSH 中执行的 enclave/块设备测试输出；它不会自动进入
QEMU 的串口日志。需要非交互记录时，在 Host 执行：

```sh
ssh -p 10022 root@127.0.0.1 \
    '/usr/bin/run-zion-enclave-demo; rc=$?; echo "demo_exit=$rc"; exit "$rc"' \
    > state/enclave-ssh.log 2>&1
rc=$?
echo "ssh_test_exit=$rc"
cat state/enclave-ssh.log
```

仍需交互输入密码，命令适用于 enclave 镜像。成功应为 `ssh_test_exit=0`。
端口转发失败时，检查旧 CVM 是否尚未退出及端口占用；`Connection reset`
发生在 Guest 尚未启动 SSH 时可等待后重试，持续失败则检查 Guest 日志。
出现旧版 `/root/qemu/include/hw/qdev-core.h:77:DEVICE` 断言时，确认使用包内
修复后的 QEMU，而非 `/usr/local/qemu` 的旧 binary。
