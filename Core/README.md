# 代码模块与调用边界

更新日期：2026-10-02。

`9ee54c2`、`e6607ee`、`51ba242`分别保存音频/UI与功能裁剪、内存/波形、击掌拆分；`3bb7718`保存经过用户功能反馈及10核验的定位拆分。本检查点独立测量协议编解码，三类回归、A/B全量构建和双板下载校验通过，12/13已通过标准测距采集、配对及完整声音对应的保存核验；11提前触发边界仍未修复，其他功能反馈待补充。保留Core/Inc与Core/Src目录，通过接口划分职责。

## 1. 模块职责

| 模块 | 职责与依赖 |
| --- | --- |
| `main.c` | CubeMX启动及用户区组装：初始化各模块，注册音频回调，执行主循环 |
| `board_memory.h` | 应用SDRAM地址/容量及重叠、边界、对齐、格式尺寸检查；main核对BSP帧缓冲基址 |
| `app_audio_view.h` | 波形/击掌共享的只读环形缓冲视图、计数/epoch快照回调与采样时间映射 |
| `app_position.c/.h` | 定位短扫频检测状态、配对、校准/偏置、方向及重传选择；无HAL/UI/UDP依赖 |
| `app_clap.c/.h` | 击掌检测状态、候选配对/过期、历史快照、载荷去重与重传选择；无HAL/UI/UDP依赖 |
| `app_wave.c/.h` | 单音周期拟合、公共周期状态/去重、10 ms窗口插值；通过只读音频视图和快照回调访问环形缓冲，无HAL/UI/UDP依赖 |
| `board_audio.c/.h` | 独占BSP音频启动、DMA半/全缓冲和错误回调；检查采集超时与SAI错误，通过回调通知应用；不依赖UI、测距、SD或网络模块 |
| `app_mic_scope.c/.h` | 四页绘制、触摸与刷新调度；从`BoardAudio_GetState`读取采集状态，不再启动采音或拥有DMA回调 |
| `app_range.c/.h` | 音频时基、页面/温度联动、同步/测量协议和各页面算法调度；仍是应用协调层，后续可逐批拆分 |
| `range_dsp`、`range_peak_pair`、`range_batch` | 标准音频检测、候选配对及统计；只保留48 kHz模板和窗口 |
| `clap_dsp`、`position_dsp` | 击掌瞬态检测、定位短扫频与方向计算；保留原算法 |
| `app_net`、`range_clock`、`range_sync` | 连接会话、硬件时间戳时基与时钟模型 |
| `app_range_protocol.c/.h` | RAN2测量包常量、大端编解码及包头/角色/会话验证；不负责UDP或业务状态 |
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

上述地址及容量现由`board_memory.h`统一定义，地址及MPU/Cache属性保持原值。Ethernet SRAM保留区仍由原链接脚本/以太网适配管理，CubeMX生成的外设文件及BSP默认常量保持原样。链接器RAM统计不包含全部通过固定地址访问的SDRAM缓冲，不能只用链接报告评估总内存使用。

## 3. 配置与生成代码维护

固件和工具仅支持48 kHz；其他采样率在配置或参数校验时拒绝。LEGACY/JOINT2为当前默认，WIDE 48 kHz仍用于回归；48 kHz模板、检测阈值和采样时序保持原值。

`main.c`对新模块的包含、初始化及主循环调用均在USER CODE区。IAR源文件列表包含`board_audio.c`、`app_wave.c`、`app_clap.c`、`app_position.c`与`app_range_protocol.c`；CubeMX再生成后检查这些自定义源文件仍纳入工程。`app_wave.c`、`app_clap.c`、`app_position.c`、`app_range_protocol.c`保持与原`app_range.c`相同的高优化选项，文件级设置不另建角色宏列表。系统/公共外设时钟、GPIO/TIM2及用户区网络初始化归属不变，`ethernetif.c`仍按既有约定恢复。

三类回归入口见[工具目录](../tools/README.md)，最新构建与验证边界见[协议整理记录](../commit_logs/2026-10-02-range-protocol.md)；首批裁剪见[实现记录](../commit_logs/2026-10-02-phase4-modules.md)。

## 4. 波形拆分后的接口

`app_range`继续拥有PCM环形缓冲、音频时基模型、时钟映射、页面联动及UDP发送。公共非标准页时基维护函数改名为`ObservePageAudioTime`，完整保留击掌/定位的音频恢复分支；跨页清理改名为`ResetPageMeasurements`，避免误认为仅清理波形。

`app_wave`拥有周期拟合器、临时单通道块、共享周期/起点和接收序号。校准输入包含只读PCM视图、DMA锚点和拟合采样周期；读取波形时传入已经映射到请求时域的锚点与采样周期。模块不自行获取板角色、页面、锁定或网络状态，这些前置条件由协调层检查。

复制PCM后通过`AudioSnapshot`检查数据是否被覆盖或音频epoch变化。该回调包含内存屏障和短临界区，一起读取64位计数及epoch，避免在Cortex-M7上读到一半更新的计数；全段复制和相关计算仍在开中断的主循环执行。所有借用指针仅在本次调用内有效。

本批验证、产物及后续拆分范围见[内存布局与波形模块日志](../commit_logs/2026-10-02-memory-wave.md)。

## 5. 击掌拆分后的接口

`app_clap`在主循环接收只读PCM视图和本地采样时间映射，输出未取整的本地起音；`app_range`完成B到A时钟换算、分配共享序号并发布候选。模块负责配对及消息选择，协调层继续控制页面/设置/锁定准入与UDP包头校验。载荷检查通过的重复包返回成功但不更新寿命。

时间回调用于检测预算，序号回调仅在产生新的A板配对结果时调用；二者使用协调层既有时钟和全局序号，不建立第二套时基/序号。`AppClap_Reset`清空测量，`AppClap_Restart`用于音频不连续并累计drops；联动清空与跨页恢复仍由协调层发起。UI可继续通过app_range头文件读取原AppClapStatus。

视图借用现有L通道环形缓冲，长度是2的幂且大于DMA半缓冲；计数单调推进，重新开始采集时更新epoch。快照回调与波形共用，模块不保存输入视图指针。验证与产物见[击掌模块日志](../commit_logs/2026-10-02-clap-module.md)。

## 6. 定位拆分后的接口

`app_range`仍在ISR将双通道每两个样点平均后写入原有环形缓冲，先写PCM再发布计数。`app_position`只在主循环借用双环；计数及快照使用24 kHz单位，AppAudioTime锚点和采样周期仍为48 kHz单位。模块将分数位置乘2换算成本地双精度纳秒，协调层对B做主时钟映射后取整。模块不持有借用视图指针。

全量Reset用于页面/设置/失锁等既有清理；RestartTimebase用于真实音频时基变化，清除原偏置、重收集样本，但保留正在校准的意图。仅处理积压则重启搜索、保留校准进度/偏置，不重新生成模板。BeginCalibration由可靠UI清理完成后调用。页面、会话、revision与epoch准入仍在app_range，载荷检查/去重在模块，消息编码/发送由协调层负责。

appPositionStatus继续供界面使用，其中DSP/LCD耗时高水位仍由协调层和界面更新；不把LCD依赖引入定位模块。实现、独立模块测试和原峰值边界回归见[定位拆分日志](../commit_logs/2026-10-02-position-module.md)。

## 7. 测量协议编解码边界

AppRangeProtocol只处理固定RAN2线格式，AppRangePacket是调用内数据，不作网络结构体发送。所有字段采用显式大端编码，未知类型留给协调层分派拒绝。调用者提供有效、互不重叠的缓冲；解码先验证长度、包头及传入的对端角色/会话，通过后才写输出。

| 字节偏移 | 字段 |
| --- | --- |
| 0～3 | RAN2魔数 |
| 4～7 | 版本12、消息类型、发送角色、长度76 |
| 8 / 16 | 发送/接收会话，各64位 |
| 24 / 28 | 序号、epoch，各32位 |
| 32 / 40 / 48 / 56 / 64 | 五个64位载荷槽，语义由消息类型定义 |
| 72 | UI revision，32位 |

来源IP/端口、pbuf生命周期、ARP解析和硬件收发时间戳由app_range保留；页面/epoch/revision准入和去重仍由原消息处理层负责。CAP1保存协议及网络身份协议不合并入RAN2。详见[协议整理记录](../commit_logs/2026-10-02-range-protocol.md)。
