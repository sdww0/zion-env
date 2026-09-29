# Zion 一次启动测试流程

适用于刚重启并登录的 Megrez Host。整套流程只重启一次板卡；Guest 密码均为
`debian`。物理串口保持开启，用于观察 OpenSBI/Zion 输出。

## 1. 初始化并测试内存保护

```sh
cd /root/zion-tests
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
sh ./zion-runtime.sh protect
```

必须看到：

```text
reserve TVM SBI result: error=0, value=0
[ZION MEMORY] PASS: host read of 0x... was blocked and recovered
```

物理串口必须同时出现：

```text
[SM] TEE security check: the hypervisor is trying to r/w the protected region
```

Host 不应 panic，SSH 应继续可用。不要再次执行 `init`。

## 2. Linux：vCPU、共享设备、SQLite 和 enclave

在 Host 执行：

```sh
cd /root/zion-tests
printf '1\n' > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh select linux
sh ./zion-runtime.sh guest
```

等待 Guest 日志出现 SSH 启动信息：

```sh
sh ./zion-runtime.sh status
```

物理串口应出现一次 `[ZION VCPU ALERT]`，Host 检查：

```sh
dmesg | grep -F '[ZION HOST TEST]'
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

参数必须回到 `0`。然后登录 Linux Guest：

```sh
ssh-keygen -R '[127.0.0.1]:10022'
ssh -p 10022 root@127.0.0.1
```

进入 Guest 后依次执行：

```sh
uname -a
ifconfig eth0
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
wc -c /tmp/blk-read
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
/usr/bin/run-zion-enclave-demo
exit
```

通过条件：块读取为 512 字节；SQLite 完成并输出 `TOTAL`；enclave 完成 10 轮
`VERIFIED`，最终输出三个 `PASS`。回到 Host 后停止 Linux CVM：

```sh
sh ./zion-runtime.sh stop
```

## 3. Asterinas：共享设备和 SQLite

仍在同一次 Host 启动中执行，不重复初始化可信池：

```sh
cd /root/zion-tests
sh ./zion-runtime.sh select asterinas
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
ssh-keygen -R '[127.0.0.1]:10022'
ssh -p 10022 root@127.0.0.1
```

进入 Asterinas Guest 后依次执行：

```sh
uname -a
ifconfig eth0
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
wc -c /tmp/blk-read
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
exit
```

通过条件：SSH 可交互、块读取为 512 字节、SQLite 完成并输出 `TOTAL`。网络
查询出现 `Inappropriate ioctl for device` 时，以 SSH 数据通信是否正常为准。
Asterinas 当前不测试 enclave。

最后在 Host 执行：

```sh
sh ./zion-runtime.sh stop
```

Guest 启动日志保存在 `/root/zion-tests/state/`。输出含义和失败排查见
`docs/TESTS_ZH.md` 与 `docs/EVIDENCE_ZH.md`。
