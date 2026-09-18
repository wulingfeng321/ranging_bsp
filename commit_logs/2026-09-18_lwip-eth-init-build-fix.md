# 修复启用 LwIP 后的 MX_ETH_Init 未定义错误

- 日期：2026-09-18
- 基线：be60e1a 及开发者重新生成的 CubeMX/LwIP 工作区改动。
- 状态：编译链接通过；未提交、未推送、未上板验证网络。

## 原因与修复

启用 LwIP 后，ETH 初始化由 MX_LWIP_Init 经 netif_add、ethernetif_init、low_level_init 调用 HAL_ETH_Init 完成。CubeMX 不再生成独立 MX_ETH_Init，但 main.c 的 USER CODE 中仍保留用于显式引用旧初始化函数的 `(void)MX_ETH_Init;`，导致 Pe020。

删除这条失效引用并添加说明注释；没有手工补造 MX_ETH_Init，也没有改动开发者的 LwIP 配置。

## 验证与交接

- IAR ranging_bsp 配置全量编译、链接通过，0 错误、0 警告。
- 当前 LCD_GRID_DEMO_ONLY=1，MX_LWIP_Init 仍处在不执行的分支；主循环也未调用 MX_LWIP_Process。本次仅解决编译错误，不表示网络已经启动。
- 当前生成的 LwIP 为 NO_SYS=1。后续接入运行流程需安排初始化和主循环轮询，并核查 PHY、DMA 内存及其他外设初始化归属，不能简单启用全部板载外设。
- 现有 CubeMX 生成文件、个人 IDE 设置及其他工作区改动保留。
