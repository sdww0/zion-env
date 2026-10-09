# Zion

[英文版](README.md)

本仓库提供 Zion 机密虚拟机的软件栈构建和运行集成，支持 Milk-V Megrez
与 QEMU `virt`。组件源码维护在独立 Git 仓库中，主仓库负责下载、Podman
构建及运行文件打包，输出位于 `output/`。

## 一键入口

在主仓库目录执行：

| 操作 | 命令 |
| --- | --- |
| 下载组件源码 | `bash ./download.sh` |
| 准备构建容器 | `bash ./utils/docker/docker-build.sh` |
| 编译 OpenSBI、Linux 和 QEMU | `bash ./build-all.sh` |
| 编译 Asterinas 并同步测试二进制 | `bash ./utils/build-asterinas-zion.sh` |
| 预览缓存清理 | `bash ./utils/clean-cache.sh` |
| 清理指定缓存 | `bash ./utils/clean-cache.sh --apply` |

工具链、下载依赖、发布包、镜像及日志会保留；清理跳过挂载目录。
不要将编译缓存和运行二进制提交到主仓库。

## 环境准备

构建主机需要 Git、Podman，以及更新 QEMU 根文件系统所需的 `sudo`。
在非 RISC-V 主机上运行 RISC-V 构建容器时，还需要 `qemu-user-static`
和 binfmt 支持。源码下载使用 HTTPS，无需 SSH Git 凭据。

```bash
bash ./download.sh
bash ./utils/docker/docker-build.sh
```

下载脚本使用单分支方式准备以下组件：

| 目录 | 仓库 | 分支 |
| --- | --- | --- |
| `opensbi/` | `https://github.com/sdww0/zion.git` | `main` |
| `u-boot/` | `https://github.com/sdww0/rockos-u-boot.git` | `rockos-v2024.01` |
| `zion-host/` | `https://github.com/sdww0/rockos-kernel.git` | `zion-host` |
| `zion-guest/` | `https://github.com/sdww0/rockos-kernel.git` | `zion-guest` |
| `qemu/` | `https://github.com/sdww0/qemu.git` | `zion-qemu` |

已有仓库只进行快进更新；存在已跟踪的本地改动或分支分叉时停止，不重置
本地工作。未跟踪的构建输出不受影响。

默认容器镜像为 `localhost/zion-kernel:0.1.0` 和
`localhost/zion-qemu:0.1.0`。镜像构建先拉取 Ubuntu 基础镜像，失败后
尝试加载 `utils/docker/ubuntu-24.04-*.tar`。

## 编译

```bash
bash ./build-all.sh
```

只编译一侧时使用：

```bash
bash ./build-all.sh --kernel
bash ./build-all.sh --qemu
```

可通过 `ZION_KERNEL_IMAGE` 和 `ZION_QEMU_IMAGE` 指定镜像。
构建容器退出后自动删除。主要输出包括 Megrez 固件、QEMU 的
`fw_dynamic-qemu.bin`、`host_kernel_image`、`guest_kernel_image`、
`tvm-driver/` 及 `qemu/`。

## QEMU 运行

```bash
bash ./virt/run.sh
```

该入口先构建、更新根文件系统，再启动 QEMU。已有运行文件时可跳过构建：

```bash
bash ./virt/run.sh pass-compile
```

串口连接当前终端，日志保存在 `virt/qemu.log`。

## Megrez 发布与部署

准备已验证的完整运行包及包含 U-Boot 的基础包后：

```bash
bash ./utils/build-megrez-release.sh /path/to/validated-runtime-release /path/to/uboot-base-release
```

基础包必须有 `boot/u-boot-megrez.bin` 和 `boot/u-boot-megrez.dtb`。
运行包必须有有效的 `SHA256SUMS`。该命令重编译 OpenSBI 并签名，沿用
已验证的主机、客户机内核、initramfs、驱动及 QEMU，不重编译这些组件。

构建记录位于 `output/megrez-firmware-*`，完整发布包位于
`output/zion-megrez-release-*`。发布包仅包含运行文件与说明，不包含
编译中间产物。随机生成的密钥仅用于实验，不是生产安全配置。

确认 SD 卡设备后执行：

```bash
sudo bash /path/to/release/install-to-device.sh /dev/sdb
```

安装器覆盖第一分区的同名文件，固件命名为 `bootloader.bin`；第三分区
只替换 `/root/zion-tests`，不清空整个分区。仅复制文件不等于完成板端
固件刷新，仍须使用板卡此前已成功的刷写流程。

仅复制固件时须显式指定启动分区：

```bash
sudo bash ./utils/flash_bootloader.sh /dev/sdb1
```

两个安装入口写入前均要求确认设备。主机启动后按发布包中的
`QUICK_TEST-zh.md` 初始化并执行测试。Linux 与 Asterinas 均支持公共
客户机测试；enclave 测试目前只支持 Linux。
