# 修复模块拆分后 LTDC 中断使用错误句柄

- 日期：2026-09-18
- 基线：be60e1a 及工作区 CubeMX 模块拆分、LwIP 配置和编译修复。
- 状态：编译链接通过，待开发者重新烧录验证；未提交或推送。

## 现象与源码证据

开发者反馈界面停在 Waiting for audio samples。拆分后的 Core/Src/ltdc.c 在 HAL_LTDC_MspInit 中启用 LTDC_IRQn；旧单文件版本这段生成代码被 BSP 接管保护屏蔽。

波形模块实际用 hLtdcHandler 初始化 LCD，当前演示分支未调用 MX_LTDC_Init，因此 CubeMX 的 hltdc 未初始化。首次画面更新经 BSP_LCD_Reload 调用 HAL_LTDC_Reload，后者启用 LTDC_IT_RR。原中断入口却将 hltdc 交给 HAL_LTDC_IRQHandler，可能导致错误寄存器访问、无法清除真实中断或异常。这个确定的句柄不一致与画面早期停住吻合，尚未通过现场寄存器/调用栈确认它是唯一原因。

## 修复

在 Core/Src/stm32f7xx_it.c 的 LTDC_IRQn 用户代码区检查 BSP 句柄；当 hLtdcHandler.Instance==LTDC 时，用它处理并返回。否则保留 CubeMX 生成处理作为回退。修改位于 USER CODE 区，便于重新生成代码时保留。

音频 DMA2_Stream7_IRQHandler 仍转发给 haudio_in_sai.hdmarx，本次未更改采样或网络配置。

## 验证与交接

- IAR 编译、链接通过：0 错误、0 警告。
- 待上板检查波形恢复、触发保持和距离区刷新。
- 若仍停住，暂停调试检查 PC 是否在 HardFault_Handler 或 LTDC_IRQHandler；观察 micDmaBlocks 是否增长及 HAL_GetTick 是否前进，以区分显示中断、音频数据或系统时基问题。
- 本次未宣称完成硬件验证，保留其他 CubeMX 和个人 IDE 改动。
