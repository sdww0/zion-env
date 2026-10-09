# Zion Milk-V Megrez 整体发布包

[英文版](README.md)

适用：已安装可启动 RockOS 的 Milk-V Megrez。Guest 登录：`root / debian`。

## 说明书

- [一次启动测试流程](QUICK_TEST-zh.md)：从刚重启的 Host 开始，按命令顺序完成全部测试。
- [部署手册](docs/DEPLOYMENT-zh.md)：SD 卡导入、备份及 Host 初始化。
- [测试手册](docs/TESTS-zh.md)：测试操作、预期输出、判定条件和日志位置。
- [输出与通过依据](docs/EVIDENCE-zh.md)：输出来源、对应检查和证据范围。
- [固件说明](boot-partition/MEGREZ_DEPLOY_TEST-zh.md)：固件位置与更新边界。

## 配套组件

| 位置 | 内容 |
| --- | --- |
| `boot-partition/` | 含 Zion 的 Megrez bootloader（OpenSBI + U-Boot）、Host kernel、Host initramfs |
| `root-partition/root/zion-tests/` | Linux/Asterinas kernel、配套 initramfs、Zion QEMU、TVM 驱动及控制程序、统一启动脚本和配置 |
| `identity/` | 与固件对应的测试设备公钥 |
| `SHA256SUMS` | 包内所有文件的完整性校验 |

在插卡主机上可运行 `sudo ./install-to-device.sh`，默认部署到 `/dev/sdb`；
也可将目标整盘设备作为参数，例如 `sudo ./install-to-device.sh /dev/sdc`。
脚本覆盖 boot 文件，并只替换 rootfs 中的 `/root/zion-tests`。

`initrd-linux.img` 包含 SSH、enclave module/runtime/测试程序和 SQLite。
`initrd-asterinas.img` 包含 SSH 和 SQLite。Linux 与 Asterinas 都支持
virtio 网络、块设备及 SQLite；CVM 内 enclave 目前只支持 Linux Guest。
切换内核只需停止 CVM，不重启 Host。

## 测试项目

1. Host 对本轮实际预留的受保护物理内存发起越权读取，SM 拦截后
   Linux 异常表恢复执行，Host 不重启。
2. Host 受控篡改 vCPU 共享通道，Zion 检测并输出一次性告警。
3. Linux 或 Asterinas CVM 使用 virtio 网络和共享数据交换路径，可从 Host
   SSH 连接，并通过 virtio-blk 实际读取块设备。
4. Linux CVM 内创建并执行 enclave，进行 10 轮计算/OCALL 校验、销毁及
   驱动负向测试；Asterinas 暂不支持此项。
5. Linux 或 Asterinas CVM 运行 `sqlite-speedtest1 --size 10 --memdb`。

Host：`6.6.87-win2030`；Guest：`6.6.88+`。资源和端口配置在
`megrez-runtime.conf`，运行日志、PID 和测试磁盘位于 `state/`。

## 注意事项

首次进入 Host 后，以 root 初始化脚本和可信内存池（每次 Host 启动一次）：

```sh
cd /root/zion-tests
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
sh ./zion-runtime.sh select
```

已成功初始化时跳过 `init`。`select` 中选择 Linux 或 Asterinas，后续使用
`sh ./zion-runtime.sh guest` 启动；也可继续使用显式的 `linux` 或
`asterinas` 命令。初始化和选择完成后按测试手册执行测试。

沿用系统默认安装的 DTB，保留现有设备树路径和启动配置。

部署前执行 `sha256sum -c SHA256SUMS`。固件使用测试密钥，仅限实验。

保存已启动成功的固件与 SD 卡文件，采用原有板子刷写流程。拷贝
`bootloader.bin` 到 SD 卡不等于已经刷入板载固件。若已使用对应新版固件，
无需为更新 Guest 或 Host 测试脚本而重复刷 bootloader。

测试范围和输出含义见[输出与通过依据](docs/EVIDENCE-zh.md)。

在源项目重新组装完整发布包时，可指定已验证运行包作为 binary 输入：

```sh
ZION_RUNTIME_INPUT=/path/to/validated-package \
    bash utils/build-zion-test-initramfs.sh
bash utils/build-asterinas-zion.sh
MEGREZ_RUNTIME_INPUT=/path/to/validated-package \
    bash utils/package-megrez-runtime.sh output/zion-megrez-release-YYYYMMDD
```

输出目录必须不存在。先构建镜像和内核、运行测试，再执行最后的打包命令。
