# Milk-V Megrez 部署与运行测试

完整运行包包含 `boot-partition/` 和 `root-partition/root/zion-tests/`。
它不是完整根文件系统或 SD 卡镜像，需要已有可启动的 RockOS。

## 从电脑部署

在解压后的发布包中执行：

```sh
sha256sum -c SHA256SUMS
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL
sudo ./install-to-device.sh /dev/sdb
```

设备必须替换为实际目标磁盘。先备份启动文件和原有 `/root/zion-tests`。
安装器使用第一分区和第三分区，覆盖同名启动文件，仅替换
`/root/zion-tests`。其他分区布局按 `docs/DEPLOYMENT-zh.md` 手工部署。

固件复制后命名为 `bootloader.bin`，但复制文件不等于刷入板载 SPI/eMMC。
使用此前已成功的板卡刷写流程，保留能启动的固件与恢复办法。
沿用系统安装的 DTB，不格式化或重新分区。

编译和签名成功不能替代板卡启动验证。固件使用实验测试密钥，对应公钥
在 `identity/` 中；演示程序不执行远程证明。

## 初始化主机

完成固件更新并重新启动主机后，保留物理串口日志。启动日志应有 ZION
标识及 `[ZION BOOT] state=READY`，但该标识本身不代表测试通过。

在主机中执行：

```sh
cd /root/zion-tests
uname -r
ls /dev/kvm
cat /sys/module/kvm/parameters/zion_vcpu_tamper
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
sh ./zion-runtime.sh protect
```

主机内核应为 `6.6.87-win2030`，篡改参数默认是 `0`。每次主机启动仅
初始化一次。要求 SBI 预留成功、使用本轮真实物理地址、访问失败可恢复、
串口有同次安全监控拦截记录，且主机继续运行。

## Linux 测试

在主机中执行：

```sh
printf '1\n' > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh select linux
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
dmesg | grep -F '[ZION HOST TEST]'
cat /sys/module/kvm/parameters/zion_vcpu_tamper
ssh -p 10022 root@127.0.0.1
```

等待 SSH 启动后连接，密码为 `debian`。要求注入与检测日志均记录
S3=`0x5a494f4e`、串口有一次 `[ZION VCPU ALERT]`，参数恢复为 `0`。

进入 Linux 客户机后：

```sh
uname -a
id
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
printf 'blk_read_exit=%s\n' "$?"
wc -c /tmp/blk-read
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
printf 'sqlite_exit=%s\n' "$?"
/usr/bin/run-zion-enclave-demo
printf 'demo_exit=%s\n' "$?"
exit
```

要求块读取为 512 字节、各退出码为零、SQLite 输出完整的 `TOTAL`，
enclave 十轮校验通过、生命周期及驱动测试成功。私有页访问的跳过项
不算通过。

## Asterinas 测试

返回主机后：

```sh
sh ./zion-runtime.sh stop
sh ./zion-runtime.sh select asterinas
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
ssh -p 10022 root@127.0.0.1
```

不再初始化内存池。确认因更换镜像导致 SSH 密钥变化时，在主机执行
`ssh-keygen -R '[127.0.0.1]:10022'` 后重连。切换回 Linux 进行 enclave
测试时，同样注意密钥变化。

Asterinas 内执行相同的 SSH 命令、块读取及 SQLite 测试，不执行
仅支持 Linux 的 enclave 测试。退出 SSH 后在主机运行
`sh ./zion-runtime.sh stop`。

## 日志与判定

安全监控输出在物理串口，主机注入及驱动日志在 dmesg，客户机启动及
串口日志在 `state/cvm.log`。SSH 输出需要单独保存，不自动进入串口日志。

完整发布包中的 `QUICK_TEST-zh.md`、`docs/TESTS-zh.md` 和
`docs/EVIDENCE-zh.md` 提供详细步骤与证据范围。仅固件构建目录没有
客户机运行文件，执行测试需要部署完整运行包。
