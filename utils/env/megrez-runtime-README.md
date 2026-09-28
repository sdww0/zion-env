# Zion Megrez 完整运行包

本包用于已有可启动 RockOS 的 Milk-V Megrez，不是完整 Linux rootfs 或
SD 卡镜像。包内只有运行所需 binary、固定配置、脚本、公钥及说明，不包含
源码、SDK、编译缓存、解包后的 initramfs、调试 ELF 或编译日志。

完整的 SD 卡导入、固件更新边界、Host 初始化、双 Guest 测试项及预期输出见：
[中文部署与测试说明](boot-partition/MEGREZ_DEPLOY_TEST_ZH.md)。

## 目录与版本

| 目录或文件 | 用途 |
| --- | --- |
| `boot-partition/` | Megrez bootloader、Host kernel/initramfs、板子 DTB 和中文操作说明 |
| `root-partition/root/zion-tests/` | Guest kernel、SSH/enclave initramfs、QEMU、Host 驱动和启动配置 |
| `identity/` | 对应测试设备身份的公钥，不包含私钥源文件 |
| `FIRMWARE_STATUS.txt` | 本包固件模式及是否经过板子验证 |
| `FIRMWARE_INPUTS.txt`（如有） | 新固件构建输入 |
| `BASE_SOURCE_VERSIONS.txt` | 基础发布包的组件版本，不等于本包所有 binary 的最新版本 |
| `ASSEMBLY_INPUTS.txt` | 实际选择的 binary 路径及打包时工作区状态 |
| `VALIDATION.md`（如有） | 本包实际测试范围及尚未覆盖的项目 |
| `SHA256SUMS` | 部署前完整性检查 |

Host 为 `6.6.87-win2030`，Guest 为 `6.6.88+`。Guest 与基础驱动来自已能
在板子运行的旧包；新版 QEMU 修复了 MachineState 被当作 DeviceState 的
断言。新版 Host 测试内核另包含默认关闭的 root-only 一次性通道篡改入口。
是否选用了新内核，应以构建输入记录、验证说明和实际 sysfs 参数为准。

新 OpenSBI 有 ASCII ZION logo、版本/初始化状态和更明确的一次性 vCPU 告警。
新固件沿用旧 U-Boot、DDR 输入及启动布局，但“编译和封装成功”不能替代
这一份新固件在真实 Megrez 上的启动验证。QEMU virt 测试使用相同 Zion
源码、不同平台 DTB，不能证明板子的全部 CSR 行为。

固件含不安全的测试密钥，仅限实验。重新构建固件会生成新的随机测试设备
身份，外部证明客户端必须同步公钥；演示本身不执行远程证明。

## 快速运行

在 Host 完成部署、确认本次启动尚未初始化可信内存池后：

```sh
cd /root/zion-tests
sh ./zion-runtime.sh init
sh ./zion-runtime.sh select
echo 1 > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
ssh -p 10022 root@127.0.0.1
```

Guest 用户名为 `root`，密码为 `debian`。`select` 可选择 Linux 或
Asterinas；两者均支持 SSH、virtio-net/blk 和 SQLite。参数必须由新版 Host
kernel 提供；若只想正常运行不注入，省略 `echo 1`。不要重复初始化，也不要
在旧 CVM 仍运行时启动第二个。停止与模式切换步骤见详细操作说明。

Enclave 目前仅支持 Linux Guest。使用 `sh ./zion-runtime.sh enclave`，进入
Guest 后运行：

```sh
/usr/bin/run-zion-enclave-demo
rc=$?
echo "demo_exit=$rc"
```

Enclave 的 module、runtime、loader、demo 和安全测试程序都内置于
`initrd-enclave.img`，无需在 Guest 另行安装。测试应有 10 轮 `VERIFIED`、
驱动负向测试通过及退出码 `0`。它不等于任意私有物理页隔离或远程证明。

脚本从自身目录解析相对路径，直接使用同目录的 QEMU，不依赖旧的
`/usr/local/qemu`。现有 Host 系统仍需提供 QEMU 的动态库。配置在
`megrez-runtime.conf`；默认仅转发 Host `127.0.0.1:10022`，不会向外网开放。
PID、日志和测试块磁盘运行时生成于 `state/`，干净发布包不含这些文件。

## 校验与重打包

从运行包根目录执行：

```sh
sha256sum -c SHA256SUMS
```

从项目源码工作区重新构建固件并白名单打包：

```sh
bash utils/build-megrez-release-firmware.sh output/new-firmware-build
MEGREZ_FIRMWARE_BUILD="$PWD/output/new-firmware-build" \
MEGREZ_HOST_KERNEL="$PWD/output/new-host-build/host_kernel_image" \
    bash utils/package-megrez-runtime.sh output/new-runtime-package
```

两个输出目录必须不存在；`new-host-build/host_kernel_image` 须先实际编译得到。
如果不设置 `MEGREZ_HOST_KERNEL`，默认使用旧发布包的 Host kernel，不能据此
声称包含了新篡改入口。打包只组装 binary，不会自动编译 Host/Guest 内核。
固件编译与签名中间文件留在构建目录，不复制进部署包。不要将整个构建目录
发布或拷到板子上。SD 卡文件复制也不等于已刷入板载 bootloader。
