# 修正角色构建后的库裁剪与烧录（2026-10-02）

## 恢复点与裁剪范围

按用户要求重试`0686c0e`的库裁剪范围。先保存当前源码、用户IAR设置、修正后的角色构建脚本及说明，恢复点为`6c298322d144aa1505602a39e456de4548214656`，位于`release`分支。

移出下列8个目录，共683个文件、约118.15 MiB：

- `Drivers/CMSIS/Lib`
- `Drivers/CMSIS/DSP`
- `Drivers/CMSIS/NN`
- `Drivers/CMSIS/Core_A`
- `Drivers/CMSIS/RTOS`
- `Drivers/CMSIS/RTOS2`
- `Middlewares/Third_Party/FreeRTOS`
- `Middlewares/ST/STM32_USB_Host_Library`

这些目录没有当前IAR源文件编译项，工程未链接CMSIS预编译库，应用没有CMSIS-DSP/NN调用，LwIP使用`NO_SYS=1`。保留CMSIS Cortex-M/STM32F7支持、HAL、BSP、Fonts、LwIP与FatFs。

文件按原相对结构保存在仓库外`E:/IARprojects/STM32CubeF7_cut/_裁剪备份/2026-10-02-unused-libraries-role-fixed/`，每个目录的文件数量和总大小已核对。IAR工程删除7条残留汇编搜索路径：USB_HOST App/Target、3条FreeRTOS路径、USB Host Core/CDC路径。现有C搜索路径及`app_range.c`优化配置不作额外精简。

## 编译与烧录

裁剪后核对180条工程源文件路径、64条C/汇编搜索路径，均存在。通过修正后的`tools/build_range_profiles.ps1`全量构建48 kHz、LEGACY/JOINT2的A/B固件，两份均0错误、0警告；每份177条C编译命令均通过板角色、采样率、方案和联合候选宏的一致性核对。原始头文件与IAR工程在构建前后哈希不变。

通过CSpyBat将A/B固件下载到短号67230834/87134138对应板，均启用下载校验，退出0并启动。完整构建日志位于各角色工程的`List/build.log`，烧录结果另存于上述备份目录的`build-flash-results.json`。

| 板 | OUT SHA256 |
| --- | --- |
| A | `c1560b837f6ea6853179fda2015cfb59b45354001e237c1150e31bdb73b39d6f` |
| B | `be1cf91fae7823281e454f20abdbd5be2f7a7d301829200234c904a2040d29ce` |

## 实机验证边界

此前命令行同步失败已定位到IAR文件级宏覆盖与旧脚本的交互，详见[角色一致性修复记录](2026-10-02-build-role-consistency.md)。本次编译、参数核对和下载校验通过后，用户复测反馈“功能没有任何问题”，确认本次裁剪后的工程功能回归通过。完整课程的量化验收仍需实验室测试。

按用户确认，将本次裁剪及实机反馈建立Git提交，并移除保留此前失败裁剪提交`0686c0e`的归档分支。如需退回本次裁剪前状态，可使用恢复点`6c29832`；所有被移出的库也保留独立副本。
