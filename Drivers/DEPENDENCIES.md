# 工程内BSP与字体依赖

更新日期：2026-09-30。

本次依赖来自整理前已经用于工程构建的本地`STM32CubeF7_cut/Drivers/BSP`及`STM32CubeF7_cut/Utilities/Fonts`，使用当前有效文件副本，没有升级驱动版本。各库的版本说明、许可证及源文件版权声明随文件保留。原上层目录未移动或删除。

## 收录范围

| 目录 | 内容与用途 |
| --- | --- |
| `BSP/STM32746G-Discovery/` | 完整板级BSP文件，包括当前使用的基础、音频、EEPROM、LCD、QSPI、SDRAM与触摸驱动；摄像头和SD BSP文件保留，但未新增到当前IAR编译列表 |
| `BSP/Components/Common/` | 音频、触摸、摄像头等组件公共接口 |
| `BSP/Components/ft5336/` | 触摸控制器 |
| `BSP/Components/wm8994/` | 音频编解码器 |
| `BSP/Components/rk043fn48h/` | LCD参数 |
| `BSP/Components/n25q128a/`、`w25q128j/` | BSP的两种板卡版本QSPI头文件依赖 |
| `BSP/Components/ov9655/` | 随完整板级BSP保留的摄像头依赖，未新增到当前IAR编译列表 |
| `BSP/Components/lan8742/` | 工程原有以太网PHY组件，本次未覆盖 |
| `Fonts/` | 原有8/12/16/20/24号字体、字体接口和版本说明 |

仅补齐该板级BSP的依赖闭包，没有引入上层BSP中其他板卡的全部组件。复制时排除上层库的`.git`元数据，保留普通源码、说明和资源文件。

## 工程引用与本地适配

IAR源文件列表和C头文件搜索路径均使用仓库内部相对路径。LCD驱动的`stm32746g_discovery_lcd.c/.h`将原`../../../Utilities/Fonts/`引用改为`../../Fonts/`，字体仍由LCD源文件直接包含，未重复加入独立编译列表。

除上述两个LCD文件的字体路径外，复制文件内容与源目录一致；保留原组件相对目录关系。原上层目录中的文件未修改。后续升级或重新复制BSP时需保留本地字体路径适配。

工程仍需安装匹配的IAR工具链；主机测试环境和烧录配置的可移植性是独立事项。CubeMX重新生成后，核对自定义BSP源文件、搜索路径和链接配置；`ethernetif.c`由用户通过Git恢复。
