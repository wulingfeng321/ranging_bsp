# CubeMX初始化调用配置核对（2026-09-30）

用户在CubeMX中保留系统时钟、GPIO及TIM2初始化调用，禁止其余外设/中间件自动调用后重新生成代码，并通过Git恢复`ethernetif.c`。

核对结果：SystemClock_Config和PeriphCommonClock_Config仍在启动流程中，MX_LWIP_Init只在用户区调用一次；SDMMC初始化保持非static，保存模块可跨文件调用。main的生成区顺序为GPIO后TIM2，均在应用启动前执行。

生成后main不再自动包含lwip.h，但用户区仍调用MX_LWIP_Init和MX_LWIP_Process，因此将该头文件补入Includes用户区，保证声明可见并在下次生成时保留。其他不再直接使用的外设头文件无需补回。

48 kHz、LEGACY/JOINT2的A/B IAR构建均0错误0警告；ethernetif.c与release提交一致。本次未烧录、未创建Git提交，板上仍为此前烧录版本。

## 裁剪前检查点

用户随后将IAR的C头文件搜索路径由29条精简至15条，删除冗余BSP组件及LwIP子目录路径。相同精简配置已经通过临时A/B构建，均0错误0警告；保存工程同时产生XML排版及IDE配置记录差异。本检查点提交当前仓库改动，之后再裁剪未使用库；汇编搜索路径由用户调整。
