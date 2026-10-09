# Zion Megrez 运行包

本包面向已安装可启动 RockOS 的 Milk-V Megrez，不是完整根文件系统
或 SD 卡镜像。仅包含运行二进制、配置、脚本、测试公钥和说明，不包含
源码、SDK、编译缓存、解包后的 initramfs、调试 ELF 或构建日志。

## 目录

| 位置 | 内容 |
| --- | --- |
| `boot-partition/` | OpenSBI/U-Boot 固件、主机内核和 initramfs、固件说明 |
| `root-partition/root/zion-tests/` | Linux/Asterinas 内核、SSH/SQLite 镜像、QEMU、驱动、控制程序及配置 |
| `identity/` | 与固件对应的测试设备公钥 |
| `SHA256SUMS` | 文件完整性校验 |

沿用系统默认 DTB。主机内核为 `6.6.87-win2030`，Linux 客户机内核为
`6.6.88+`。受控 vCPU 篡改入口默认关闭，以运行中的 sysfs 参数为准，
不能仅凭版本字符串判断支持情况。

固件打印 ZION 标识、版本、初始化状态及 vCPU 告警。编译、签名或 QEMU
测试成功不能代替物理板卡验证。密钥仅用于实验，重新编译生成新的测试
设备身份；演示程序不执行远程证明。

## 部署和测试

在解压后的发布包中执行：

```sh
sha256sum -c SHA256SUMS
sudo ./install-to-device.sh /dev/sdb
```

先核实目标磁盘。安装器更新第一分区，并仅替换第三分区的
`/root/zion-tests`。复制文件不等于刷新固件，仍须使用板卡既有刷写流程。

主机重新启动后按 `QUICK_TEST-zh.md` 执行。详细步骤在
`docs/DEPLOYMENT-zh.md` 与 `docs/TESTS-zh.md`，判定依据在
`docs/EVIDENCE-zh.md`。

Linux 与 Asterinas 均支持 SSH、virtio 网络和块设备、SQLite 测试。
enclave 仅支持 Linux，测试程序已包含在 `initrd-linux.img`。
每次主机启动仅初始化一次，切换内核前停止 CVM；跳过项和启动标识
不代表测试通过。

脚本使用相对自身目录的路径和包内 QEMU，主机仍须提供其动态库。
配置在 `megrez-runtime.conf`，SSH 仅绑定主机 `127.0.0.1:10022`。
运行时生成的 PID、日志和测试磁盘位于 `state/`。

## 重新打包

在源码工作区执行：

```sh
bash utils/build-megrez-release.sh /path/to/validated-runtime-release /path/to/uboot-base-release
```

该入口重编译并签名 OpenSBI，沿用已验证的运行二进制，不重编译 Linux
或 Asterinas。输出目录必须不存在，固件编译中间产物不进入发布包。
