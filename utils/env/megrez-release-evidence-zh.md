# 测试输出与判定依据

操作见[测试手册](TESTS_ZH.md)。以下输出须来自同一轮测试。

## 1. vCPU 共享通道篡改检测

| 关键输出 | 来源和位置 | 对应检查 |
| --- | --- | --- |
| `[ZION HOST TEST] ... register=S3 value=0x5a494f4e` | Host KVM `vcpu_sbi.c`，Host dmesg | 向 Host 可见的 vCPU 共享通道 S3 写入测试值 |
| `[ZION VCPU ALERT] ... register=S3 expected=0x0 observed=0x5a494f4e` | OpenSBI `context.c`，物理串口 | CVM 的 SBI 返回路径检测到共享 S3 非零；此时 S3 不是 SBI 参数或 MMIO 数据字段 |
| 测试参数由 `1` 变为 `0` | Host sysfs 参数 | 一次性注入请求已消费 |

**通过条件：**两端记录的字段和值一致、参数归零，且后续 Guest SSH 和块
读取成功。证明受控共享通道修改被 monitor 检测，且该次运行能够继续。

注入对象不是物理 CPU 实时 S3 或 monitor 私有状态。告警中的
`private saved state is retained` 是检查路径的处理说明，不是 Guest 私有
S3 的实测断言；本测试没有验证所有私有寄存器的完整性。Zion 告警每次 Host
启动全局一次（两行），已出现告警后须重启 Host 才能重新观察。

## 2. virtio 网络与块设备数据交换

| 关键结果 | 来源和位置 | 对应检查 |
| --- | --- | --- |
| Host SSH 登录成功并执行 `uname -a`、`id` | Linux 或 Asterinas Guest 命令，SSH 终端 | 经本次 CVM 的转发端口完成连接、认证及远端命令执行 |
| `blk_read_exit=0` | shell 捕获 Guest dd 的退出码，SSH 终端 | `/dev/vda` 的 512 字节读取命令成功 |
| `512 /tmp/blk-read` | Guest wc，SSH 终端 | 实际读取文件长度为 512 字节 |

**通过条件：**SSH 内命令成功执行，块读取退出码为零且长度为 512。
结合启动配置的 `virtio-net-device` 与 `virtio-blk-device`，这是对应
virtio 数据通路可用的端到端功能证据。

测试块磁盘是 Host `state/virtio-blk.img`，不使用 Host 系统盘。接口 `UP`、
设备节点存在或 Dropbear 启动提示不能替代实际通信和读取。上述结果不单独
证明 monitor 内部共享页映射及私有页权限正确。

Linux 与 Asterinas 都必须实际完成上述网络和块读取。Asterinas 中个别网络
查询 ioctl 返回 `Inappropriate ioctl for device` 不等于数据面失败，判定仍以
SSH 远程命令和块数据读取为准。

## 3. CVM 内 enclave 执行与驱动约束（仅 Linux Guest）

| 关键输出 | 来源和位置 | 对应检查 |
| --- | --- | --- |
| `[ZION ENCLAVE] COMPUTE 1/10 ... VERIFIED` 至 `10/10` | Guest 父程序 demo-runner，SSH 终端 | enclave 通过 OCALL 传出结果；父程序重新计算，逐轮核对编号与校验值 |
| `[CVM DEMO] shared payload overwritten ...` | demo-runner 的 OCALL 回调，SSH 终端 | 将共享 payload 覆盖为 0xa5；后续轮次正确表明计算可在覆盖后继续 |
| `PASS: private checksum continuity across 10 OCALLs; destroyed` | demo-runner，SSH 终端 | 10 轮校验正确、运行接口成功、enclave 返回零、销毁接口成功 |
| `PASS: cross-file enclave access rejected` | Guest zion-driver-security，SSH 终端 | 另一个文件描述符发起 UTM 初始化被 EPERM 拒绝 |
| `PASS: out-of-EPM mmap rejected` | zion-driver-security，SSH 终端 | EPM 范围外映射被 EINVAL 拒绝 |
| `PASS: all negative ABI tests` | zion-driver-security，SSH 终端 | 全部已实现的驱动测试无失败项 |
| `PASS: compute lifecycle and driver negative ABI tests`、`demo_exit=0` | Guest 包装脚本及 shell，SSH 终端 | demo 和驱动测试均成功返回，整个测试脚本退出成功 |

**通过条件：**10 轮均校验正确、销毁成功、驱动测试全部通过、退出码为零。
证明受测 CVM/enclave 计算与 OCALL 生命周期，以及受测驱动接口约束可用。

`COMPUTE` 由父程序校验后打印，不是 enclave 直接打印；一秒暂停也发生在
父程序。共享 payload 覆盖、驱动拒绝和计算校验不等于私有物理页硬件隔离
或远程证明。`SKIP: arbitrary private-page access` 表示该项未测试。

Asterinas 当前没有 Linux `/dev/zion_enclave` ioctl frontend，本项不适用于
Asterinas。Linux enclave 通过不能外推为 Asterinas enclave 支持。

## 4. Linux/Asterinas SQLite

| 关键输出 | 来源与位置 | 实际检查 |
| --- | --- | --- |
| SQLite 各测试耗时、`TOTAL` | sqlite-speedtest1，Guest SSH 终端 | 实际运行 SQLite 工作负载及 integrity_check |
| `sqlite_exit=0` | 紧接 SQLite 命令捕获的退出码，Guest SSH 终端 | 被测进程正常完成 |

**通过条件：**实际工作负载及汇总输出完整，退出码为零。使用 `--memdb`，
证明所选 Guest 执行 SQLite；不证明块设备文件系统或持久化。Asterinas 的
网络和块设备由上一项独立验证，enclave 仍为 Linux 专属。

## 5. 受保护物理内存读取拦截

| 关键输出 | 来源与位置 | 实际检查 |
| --- | --- | --- |
| `device_phys_addr=...` | tvm-driver 预留接口，Host 日志 | 本轮预留内存的物理基地址；不是虚拟地址 |
| `reserve TVM SBI result: error=0` | tvm-driver 的 SBI 返回值 | SM 已接受该物理区间；非零时不得把同轮地址当作保护区 |
| `Access the physical memory: ... physical address = ...` | tvm-driver 访问探针，Host 日志 | 实际尝试读取的物理地址，须落入本轮受保护区域 |
| `TEE security check: ... r/w the protected region` 及地址/异常上下文 | OpenSBI `tee-mem.c`，物理串口 | 对受保护区域的访问进入 SM 安全检查路径 |

**通过条件：**预留成功、探针目标与本轮保护区域相符、同次访问有 SM
拦截记录，且没有驱动成功读取后的 `Access the physical memory: value=...`。
仅有权限数字、映射失败、Host 崩溃或缺少输出不能证明通过。

探针使用 `READ_ONCE` 读取，不执行写入。此项验证本次越权读取被拦截，
不单独证明写保护、所有地址隔离或无信息泄露；Host 异常可能是该探针的
后果，不要求本项访问后系统继续运行。独立重启后测试并保存串口记录。

## 日志与范围

OpenSBI 输出看物理串口，Host 注入看 dmesg，Guest 启动看
`state/cvm.log`，SSH 内测试输出保存 SSH 记录；SSH 输出不会自动进入
Guest 串口日志。退出码必须紧接被测命令捕获。

Logo、`READY`、`QEMU started`、服务启动提示和单独的 hello 文本都不是
测试的通过依据。上述判定适用于受控实验，不是密码学证明；QEMU virt
记录不能代替 Megrez 上实际采集的测试结果。
