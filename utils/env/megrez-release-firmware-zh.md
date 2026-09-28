# Megrez 固件与配套文件

| 文件 | 用途 |
| --- | --- |
| `bootloader_secboot_ddr5_milkv_megrez.bin` | 经 nsign 封装的 OpenSBI + U-Boot 启动固件；按旧工程约定导入 SD 卡时命名为 `bootloader.bin` |
| `vmlinuz-6.6.87-win2030` | Host Linux，完整测试包中的版本含默认关闭的 Host 一次性篡改测试入口 |
| `initrd.img-6.6.87-win2030` | 匹配版本的 Host initramfs；已用于 virt，板子存储布局仍须确认 |

固件提供 ZION logo、OpenSBI 版本、初始化状态和一次性 vCPU 告警。
Host 沿用系统默认安装的 DTB，无需复制或替换设备树。
更新 Host kernel 后，检查 `/sys/module/kvm/parameters/zion_vcpu_tamper`。

操作手册：

- [SD 卡导入与初始化](../docs/DEPLOYMENT_ZH.md)
- [测试项目及预期输出](../docs/TESTS_ZH.md)
- [测试输出与通过依据](../docs/EVIDENCE_ZH.md)
- [整体发布包说明](../README.md)

保留已能启动的旧固件，检查 SD 卡设备和分区后再覆盖。不要直接照抄旧
脚本中写死的 `/dev/sdb` 或 `/dev/sdc`。本包不自动格式化、分区或刷写板载
SPI/eMMC；拷贝固件到 SD 卡不等于刷入板载固件。使用板子此前已成功的
更新流程，不采用未经确认的原始写入命令。
