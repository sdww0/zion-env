# Zion 测试手册

前置步骤：[部署与 Host 初始化](DEPLOYMENT-zh.md)。默认 Guest 512 MiB、
1 vCPU，SSH 转发端口 10022。输出解释见[输出与通过依据](EVIDENCE-zh.md)。

## 0. Host 脚本初始化

在 Host 的 root 终端执行。若本次 Host 启动已经成功初始化内存池，跳过
本节初始化命令，直接进行测试一；切换 Linux/Asterinas 也不重复初始化。

```sh
cd /root/zion-tests
ls zion-runtime.sh megrez-runtime.conf tvm-driver.ko tvm-control \
   qemu-system-riscv64 guest_kernel_image initrd-linux.img \
   asterinas_kernel initrd-asterinas.img
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
ls /dev/kvm
```

本次启动尚未加载 TVM 驱动、尚未初始化内存池时执行：

```sh
sh ./zion-runtime.sh init
rc=$?
printf 'init_exit=%s\n' "$rc"
ls /dev/tvm
```

预期输出包含：

```text
[tvm-control] type=tvm, count=0x30d40
ioctl(): RTVM_IOC_RESERVE_TVM_MEM
[tvm-driver] rtvm_reserve_tvm_mem(): ... device_phys_addr=...
[tvm-driver] reserve TVM SBI result: error=0, value=0
init_exit=0
```

脚本加载 `tvm-driver.ko`，再按配置预留 200000 个 4 KiB 页（约 781 MiB）。
退出码应为零，且 SBI 结果必须为 `error=0`。若提示驱动已加载，先确认此前
是否预留成功；`/dev/tvm` 存在不代表池已建立。状态不明时查日志或重启
Host 后重新初始化，不要反复执行 reserve。

## 1. 受保护物理内存：Host 越权读取

`init` 已保存本轮成功预留的真实物理基地址。启动 CVM 前执行：

```sh
sh ./zion-runtime.sh protect
```

预期 Host 输出：

```text
RTVM_IOC_CVM_PHYS_MEMORY_ACCESS: Permission denied
[ZION MEMORY] PASS: host read of 0x... was blocked and recovered
```

驱动使用 Linux `copy_from_kernel_nofault()` 异常表恢复路径，所以该测试
不应再导致 Host panic。物理串口必须同时出现：

```text
[SM] TEE security check: the hypervisor is trying to r/w the protected region
```

通过条件：SBI 预留为 `error=0`，探针使用 `state/protected-addr`中的
本轮基地址，命令打印 `PASS` 且 Host/SSH 继续可用，串口有同次 SM
拦截记录。不要重复执行 `init`，后续 CVM 直接使用同一可信内存池。

## 2. 选择 Guest 系统

每轮公共 Guest 测试开始前选择 Linux 或 Asterinas：

```sh
cd /root/zion-tests
sh ./zion-runtime.sh select
```

输入 `1` 选择 Linux，输入 `2` 选择 Asterinas。选择结果保存在
`state/guest-system`，后续统一使用：

```sh
sh ./zion-runtime.sh guest
```

Linux 与 Asterinas 都执行 vCPU 检测、SSH、virtio 网络/块设备和 SQLite。
enclave frontend 当前依赖 Linux Guest 的 `/dev/zion_enclave` 驱动，因此
enclave 项必须使用 Linux，不属于 Asterinas 的通过条件。显式命令
`sh ./zion-runtime.sh linux` 和 `sh ./zion-runtime.sh asterinas` 继续保留。

## 3. vCPU 保护：受控 Host 篡改与 Zion 检测

物理串口的启动记录应有 ZION ASCII logo、OpenSBI 版本及：

```text
[ZION BOOT] OpenSBI=1.5 hart=... H=yes
[ZION BOOT] identity=INSECURE-TEST-KEYS not-for-production
[ZION BOOT] state=INITIALIZING (not a security-test PASS)
[ZION BOOT] state=READY CVM/enclave SBI registered; runtime validation still required
```

先做篡改测试，保存物理串口记录。每次 Host 启动全局只打印一个告警事件
（两行）；已有告警时，重启 Host 后才能重新观察。重启 CVM 不重置告警。

在 Host，以 root 执行：

```sh
cd /root/zion-tests
cat /sys/module/kvm/parameters/zion_vcpu_tamper
printf '1\n' > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh guest
```

下一次 CVM SBI exit 将触发一次注入，参数随后自动归零。Host 日志预期：

```text
[ZION HOST TEST] controlled-tamper cvm=... vcpu=... register=S3 value=0x5a494f4e; one-shot injection consumed
```

物理串口的 Zion 检测日志预期：

```text
[ZION VCPU ALERT] hart=... source=HOST-SHARED-CHANNEL register=S3 expected=0x0 observed=0x5a494f4e
[ZION VCPU ALERT] classification=SUSPECTED-TAMPER-OR-ABI-MISMATCH not proof of malicious intent; private saved state is retained
```

在 Host 检查：

```sh
dmesg | grep -F '[ZION HOST TEST]'
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

参数应为 `0`；仍为 `1` 时检查 CVM 启动，写入 `0` 可取消。没有 Zion
告警时，检查是否运行新版固件、是否已出现本次启动的首次告警。

通过条件：两端记录均为 S3=`0x5a494f4e`、参数归零，随后第二项通过。

## 4. 共享内存：virtio 网络与块设备

上一项已经启动所选 CVM 时，不要重复启动；未启动时在 Host 执行：

```sh
cd /root/zion-tests
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
```

Host 脚本预期输出：

```text
QEMU started (not a Guest PASS). Log: /root/zion-tests/state/cvm.log
Host: ssh -p 10022 root@127.0.0.1 ; Guest password: debian
```

等待 Guest 启动、生成 host key 和启动 Dropbear，可重复 `status` 查看有限
行数输出，不要让 `tail -f` 占住唯一串口。Guest 日志预期：

```text
Zion CVM SSH is starting on port 22.
Default login: root / debian
```

在 Host 执行：

```sh
ssh -p 10022 root@127.0.0.1
```

密码 `debian`。如果更换镜像后出现**已确认的** host key 变化，在 Host
执行以下命令后重连，不要无条件忽略身份告警：

```sh
ssh-keygen -R '[127.0.0.1]:10022'
```

进入 Guest 后：

```sh
uname -a
ifconfig eth0
ls -l /dev/vda
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
rc=$?
printf 'blk_read_exit=%s\n' "$rc"
wc -c /tmp/blk-read
```

Linux 的 `uname` 预期包含 `Linux zion-cvm 6.6.88+`；Asterinas 当前兼容
输出包含 `Linux zion-cvm 5.13.0`，同时启动串口必须有 Asterinas banner。
不能只凭 `uname` 区分实现。两者网卡均应为 `UP`，默认地址通常为
`10.0.2.15`。Asterinas 的部分 `SIOCGIFTXQLEN`/`SIOCGIFFLAGS` 查询尚不完整，
可能打印 `Inappropriate ioctl for device`；只要实际 SSH 与块读取通过，该提示
不单独判失败。块设备读取预期：

```text
1+0 records in
1+0 records out
blk_read_exit=0
512 /tmp/blk-read
```

通过条件：SSH 命令执行成功、`blk_read_exit=0`、读取长度为 512。
测试磁盘为 Host `state/virtio-blk.img`（64 MiB）。

## 5. SQLite

保持所选 Linux 或 Asterinas CVM 运行，在 Guest SSH 会话中执行：

```sh
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
rc=$?
printf 'sqlite_exit=%s\n' "$rc"
```

预期包含 SQLite 各子测试、`PRAGMA integrity_check`、`TOTAL`，最后：

```text
sqlite_exit=0
```

通过条件：实际工作负载完整执行且退出码为零。数据库位于内存中，本项不验证
块设备文件系统或持久化。

## 6. CVM 与 enclave 融合（仅 Linux Guest）

选择 Linux 时可保持同一个 CVM 和 SSH 会话，直接执行下述测试。选择
Asterinas 时，先按下一节步骤停止 CVM、保存日志，再启动 Linux：

```sh
sh ./zion-runtime.sh enclave
```

`enclave` 是 Linux 模式别名。Asterinas 当前没有 `/dev/zion_enclave` frontend，
不要在 Asterinas 中加载 Linux `.ko`，也不要把此项记为 Asterinas 失败。
若重新启动后出现已确认的 host key 变化，在 Host 清理记录后重连：

```sh
ssh-keygen -R '[127.0.0.1]:10022'
ssh -p 10022 root@127.0.0.1
```

所有 enclave 配套 binary 都已内置于 `initrd-linux.img`。在 Guest 执行：

```sh
/usr/bin/run-zion-enclave-demo
rc=$?
printf 'demo_exit=%s\n' "$rc"
```

预期 10 轮 `COMPUTE ... VERIFIED`，最终输出：

```text
[ZION DEMO] PASS: private checksum continuity across 10 OCALLs; destroyed
[DRIVER SECURITY] PASS: all negative ABI tests
[ZION DEMO] PASS: compute lifecycle and driver negative ABI tests
[ZION DEMO] SKIP: arbitrary private-page access; dedicated safe probe required
demo_exit=0
```

通过条件：10 轮均为 `VERIFIED`、销毁成功、驱动负向测试全部通过、
`demo_exit=0`。`SKIP` 项不计为通过。

## 停止或切换 Guest

先在 Guest 执行 `exit` 返回 Host。退出 SSH 不等于 CVM 已关闭。
在 Host 检查 PID：

```sh
cd /root/zion-tests
pid=$(cat state/cvm.pid)
ps -p "$pid" -o pid,args
```

确认属于本目录启动的 Zion QEMU 后，优先使用：

```sh
sh ./zion-runtime.sh stop
```

如果需要手工核查：

```sh
kill "$pid"
sleep 2
ps -p "$pid" -o pid,args
guest=$(cat state/guest-system 2>/dev/null || printf unknown)
cp state/cvm.log "state/${guest}-$(date +%Y%m%d-%H%M%S).log"
```

若仍运行，等待并查日志，不要无差别 `killall qemu`。模式切换不需要再
执行 `init`，下一次启动可能覆盖 `state/cvm.log`，因此先保存日志。

切换系统时不重复执行 `init`；重新运行 `select`，再执行 `guest`。

## 日志与结果表

| 项目 | 必须保存的证据 |
| --- | --- |
| vCPU 篡改检测 | Host 注入记录、同值的物理串口 Zion 告警、参数归零 |
| Linux/Asterinas 共享数据交换 | 所选系统、SSH 成功、`blk_read_exit=0` 和读取长度 512 |
| Linux/Asterinas SQLite | SQLite 实际测试/汇总、`sqlite_exit=0` |
| Linux CVM/enclave | 10 轮 VERIFIED、负向测试通过、`demo_exit=0` |
| 物理内存保护 | 本轮预留物理基址、探针实际地址、对应 SM 拦截及异常上下文，无成功读取值 |

物理串口保存 OpenSBI logo/告警，Host `dmesg` 保存注入及驱动日志，
`state/cvm.log` 保存 Guest 串口日志。SSH 内执行的输出不自动进入 QEMU
串口日志，应保存 SSH 终端记录。也可在 Host 非交互记录 enclave 测试：

```sh
ssh -p 10022 root@127.0.0.1 \
    '/usr/bin/run-zion-enclave-demo' \
    > state/enclave-ssh.log 2>&1
rc=$?
printf 'ssh_test_exit=%s\n' "$rc"
cat state/enclave-ssh.log
```

输入 Guest 密码。成功应为 `ssh_test_exit=0`。
