# STM32746G-Discovery 网口直连测试

目标：LAN8742A / RMII，电脑通过网线直接 ping 开发板。

## 修改

- `LWIP/Target/ethernetif.c`：补齐 ETH 时钟、AF11 引脚、LAN8742 MDIO 接口和初始化；按 PHY 协商结果设置 MAC，使用无 RTOS 的轮询收发，支持拔插网线后重新建立链路。
- MAC 地址改为静态存储，避免初始化返回后留下栈指针；发送失败返回 `ERR_IF`，增加收发计数和 PHY 状态供调试。
- `Core/Src/main.c`：使用 BSP 初始化屏幕和麦克风，跳过无关的全外设启动（包括 SD 卡初始化）；`MX_LWIP_Init()` 独立执行，主循环继续执行 `MX_LWIP_Process()`。
- `EWARM/ranging_bsp.ewp`：加入本项目内的 LAN8742 源码及头文件路径，使用 `stm32f746xx_ethernet.icf`。
- 链接脚本和 MPU 共同预留 `0x20040000..0x2004FFFF` 为不可缓存的网络内存：接收池在前 32 KB，LwIP 堆从 `0x20048000` 开始（8 KB 加分配器开销），描述符位于 `0x2004C000` / `0x2004C0A0`。
- LwIP 内存对齐改为 32 字节；发送其他 SRAM 中的数据时，若 D-Cache 开启则先清理相应缓存行。

## 烧录与测试

1. IAR 若提示工程文件被外部修改，重新载入；使用 `EWARM/ranging_bsp.ewp` 的 `ranging_bsp` 配置。
2. 下载生成的 `EWARM/ranging_bsp/Exe/ranging_bsp.out`，然后 Go / F5，使程序持续运行。
3. 电脑以太网设为 `192.168.10.20`，掩码 `255.255.255.0`，默认网关留空。板端固定为 `192.168.10.10`。
4. 等待几秒后执行 `ping 192.168.10.10 -t`；正常结果应来自 `192.168.10.10`。按 Ctrl+C 查看丢包率。
5. 拔线后应超时，插回后等待自动协商完成，确认回复恢复。也可在断线状态启动板子，稍后插线验证。

## 若仍然不通

在 IAR Watch 中观察，读完后恢复运行再继续 ping：

| 变量 | 含义 |
|---|---|
| `ethPhyInitStatus` | 0 表示 PHY 初始化成功；负数表示失败 |
| `ethLinkState` | 1 无链路；2 为 100M 全双工；3 为 100M 半双工；4/5 为 10M；6 协商未完成；负数为读写错误 |
| `heth.gState` | 建立链路后应为 `HAL_ETH_STATE_STARTED` |
| `ethRxPackets` | 收到并交给协议栈的以太网帧数量，包含 ARP 等，不仅是 ping |
| `ethTxPackets` | HAL 成功发送的帧数量 |
| `ethTxErrors` | HAL 发送失败次数 |
| `heth.ErrorCode` / `heth.DMAErrorCode` | HAL / DMA 错误信息 |

若程序停在 `Error_Handler()`，查看调用栈确定失败位置。

本修改包含 CubeMX 自动生成区以外及以内的驱动适配。重新生成代码后需检查 PHY 链路函数、RX 池放置方式、独立网络初始化调用和 IAR 链接脚本是否仍然保留。

## 编译验证

- 2026-09-18：使用本机 `E:/12/common/bin/IarBuild.exe` 全量编译、链接，0 错误、0 警告。命令行传入工程的绝对路径，避免旧版 IAR 对自定义链接脚本的相对路径解析失败。
- 已生成 `EWARM/ranging_bsp/Exe/ranging_bsp.out`，并通过 IAR ELF Tool 导出同目录的 `ranging_bsp.hex`；HEX 可用于 STM32CubeProgrammer 烧录。
- 链接映射确认：向量表 `0x08000000`；8 KB 栈位于 DTCM；RX 池 `0x20040000`，大小 `0x49A0`；RX/TX 描述符分别位于 `0x2004C000` / `0x2004C0A0`，各 `0xA0` 字节。与预留 LwIP 堆不重叠。
- 链接结果包含本项目的 `HAL_ETH_MspInit()` 和 `LAN8742_Init()` 实现。
- 尚未上板：真实 ping、长时间运行和拔插恢复需要硬件验证。
