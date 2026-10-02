# 代码模块与调用边界

更新日期：2026-10-02。

本轮以已验证的`5db6b01`为起点，先将音频硬件操作从界面中拆出，并收敛公共协议编码。保留`Core/Inc`与`Core/Src`目录，避免一次移动所有应用文件；文件名及接口区分生成代码、板级适配和应用职责。

## 1. 模块职责

| 模块 | 职责与依赖 |
| --- | --- |
| `main.c` | CubeMX启动及用户区组装：初始化各模块，注册音频回调，执行主循环 |
| `board_audio.c/.h` | 独占BSP音频启动、DMA半/全缓冲和错误回调；检查采集超时与SAI错误，通过回调通知应用；不依赖UI、测距、SD或网络模块 |
| `app_mic_scope.c/.h` | 四页绘制、触摸与刷新调度；从`BoardAudio_GetState`读取采集状态，不再启动采音或拥有DMA回调 |
| `app_range.c/.h` | 音频时基、页面/温度联动、同步/测量协议和各页面算法调度；仍是应用协调层，后续可逐批拆分 |
| `range_dsp`、`range_peak_pair`、`range_batch` | 标准音频检测、候选配对及统计；只保留48 kHz模板和窗口 |
| `clap_dsp`、`position_dsp` | 击掌瞬态检测、定位短扫频与方向计算；保留原算法 |
| `app_net`、`range_clock`、`range_sync` | 连接会话、硬件时间戳时基与时钟模型 |
| `app_wire.h` | 32/64位大小端字节读写；网络/测量采用BE，保存/锚点采用LE；不使用对齐或本机结构体布局假设 |
| `app_capture`、`capture_sd` | 仅STANDARD的录音与双板保存事务、SD DMA驱动；不实现击掌导出 |

`app_wire.h`只共享字节编码，不合并不同协议的包格式、状态机或校验规则。测量协议仍为12，CAP1/RNG1布局保持一致。

## 2. 调用上下文与缓冲生命周期

- 初始化顺序：网络、测距、LCD/SDRAM、保存、界面，最后`BoardAudio_Init(AppRange_Audio, AppRange_AudioError)`。音频回调注册后才启动DMA。
- 主循环顺序：`MX_LWIP_Process` → `AppNet_Process` → `AppRange_Process` → `AppCapture_Process` → `BoardAudio_Process` → `MicScope_Process`。
- 音频ISR：`board_audio`按半缓冲调用`AppRange_Audio`，应用完成PCM/锚点搬运及定位两点平均。PCM指针只在当前回调期间借用，消费者不得保留指针等待下次主循环；DMA会重复使用该缓冲。
- DMA错误通知可来自ISR；超时/SAI错误通知来自主循环。`AppRange_AudioError`应保持短小、可重复调用。检测相关、网络事务、绘屏和SD写入在主循环执行。
- `AppCapture_SetEnabled`只在主循环调用，由页面切换和保存轮询保证状态一致。离开/进入STANDARD清空未保存的PCM、锚点有效计数和日志，不重置时钟同步；同页调温不清空录音。保存占用期间禁止切页，保留失败轮重试。

| 缓冲 | 起始地址 | 归属 |
| --- | --- | --- |
| LCD双缓冲 | `0xC0000000`、`0xC0080000` | LCD初始化及界面呈现 |
| 音频DMA | `0xC0100000` | `board_audio`，3072个16位字，半缓冲768个双通道帧/16 ms |
| 保存PCM | `0xC0200000` | `app_capture`，最多16秒双通道录音 |
| 保存锚点 | `0xC0520000` | `app_capture` |
| 保存日志 | `0xC0530000` | `app_capture`，128 KiB |
| RNG文件头 | `0xC0550000` | `app_capture`，4096字节 |
| SD DMA中转 | `0xC0551000` | `capture_sd`，512字节 |

本轮保持上述地址及MPU/Cache配置。链接器RAM统计不包含全部通过固定地址访问的SDRAM缓冲，不能只用链接报告评估总内存使用。

## 3. 配置与生成代码维护

固件和工具仅支持48 kHz；其他采样率在配置或参数校验时拒绝。LEGACY/JOINT2为当前默认，WIDE 48 kHz仍用于回归；48 kHz模板、检测阈值和采样时序保持原值。

`main.c`对新模块的包含、初始化及主循环调用均在USER CODE区。IAR源文件列表新增`board_audio.c`；CubeMX再生成后检查该自定义源文件仍纳入工程。系统/公共外设时钟、GPIO/TIM2及用户区网络初始化归属不变，`ethernetif.c`仍按既有约定恢复。

三类回归入口见[工具目录](../tools/README.md)，本轮构建与未验证项见[实现记录](../commit_logs/2026-10-02-phase4-modules.md)。
