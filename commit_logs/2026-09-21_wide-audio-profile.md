# 新旧音频与检测算法切换

## 配置

修改 `Core/Inc/app_board_config.h` 中唯一的音频选择宏，两板必须相同并重新编译烧录：

```c
// 新方案（当前默认）
#define APP_RANGE_AUDIO_PROFILE APP_RANGE_AUDIO_WIDE
// 如需旧方案，将上面这一行改成：
// #define APP_RANGE_AUDIO_PROFILE APP_RANGE_AUDIO_LEGACY
```

| 模式 | 音频 | 屏幕标识 | 检测 |
| --- | --- | --- | --- |
| WIDE / 2 | `tools/test_audio_wide_repeat.wav` | RANGE WIDE1 | 宽带模板、多峰比较、较早可信峰、三脉冲一致性 |
| LEGACY / 1 | `tools/test_audio_chirp.wav` 或 `tools/test_audio_chirp_repeat.wav` | RANGE LEGACY | 保留原 STAT4 检测器 |

宏也可在 IAR 编译选项中定义；编译选项会覆盖头文件默认值。禁止两块板选择不同模式。
两套模式共用同步、音频时基、距离换算及 STAT4 批次统计，切换音频不代表绕过统计门槛。
旧单文件只有 5 个签名；开启当前至少 6 样本的批次统计时，应使用旧 repeat 文件。

## 新音频

16 kHz、16-bit PCM、单声道，10.5 秒，15 个签名。
起点仍为 0/0.5/1/1.5/2、4/4.5/5/5.5/6、8/8.5/9/9.5/10 秒，兼容原 11 秒批次。
每个签名是 32 ms 上扫频、4 ms 静音、32 ms 下扫频、4 ms 静音、32 ms 上扫频，共 104 ms。
频带 1.5～6.5 kHz，每个脉冲两端约 4 ms 余弦渐变；峰值为满幅的 75%。
保持 32 ms 脉冲长度是为了兼容现有 2048 样点窗口及实时处理流程，并未声称加长脉冲。

`python tools/make_wide_audio.py` 同时生成 WAV 和 `Core/Inc/range_template_wide.h`，模板严格来自
同一次生成的 PCM 并去除直流分量；不修改旧 WAV 或 `range_template.h`。

## 新检测器

代码仍在 `Core/Src/range_dsp.c`，通过条件编译保留原检测器，接口不变。

1. 每次处理最多 64 个起点，附加前后比较范围；计算归一化相关平方，保持反相兼容。
2. 首脉冲通过门槛后检查另外两脉冲，三者均通过才能构成候选峰。
3. 比较候选峰前后 3 样点，避免把上升沿或邻近旁瓣当作到达峰。
4. 与随后最多 32 样点（2 ms）内的最强有效候选比较；候选联合分数至少达到其 25%。
5. 各脉冲在候选位置附近 ±2 样点内的独立最佳峰偏移差不超过 1 样点。
6. 按时间顺序选择满足以上条件的较早峰，以未截断的相邻相关分数做抛物线插值。

新默认单脉冲相关平方门槛为 0.12，并且不会低于 `APP_RANGE_MIN_QUALITY` 对应门槛；
参数在配置头文件中集中定义。固定局部数组，无堆分配，保留每 DMA 块四次扫描的流程。
新扫描计算量高于旧版，主机测试不能证明板上实时性；上板需观察 `audioOverruns`、`audioGapDrops`
和积压诊断，特别是在 LCD 刷新与网络同时运行时。

这只是“较早可信路径”的启发式规则，不是房间声学反演。直达声被遮挡、弱于门槛、
反射与直达声无法分离或设备频响失真时仍可能误选或漏检；不得把检出峰保证为直达声。
未修正固定采集延迟，未改变几何前提，也未证明 ±10 mm 绝对精度。

匹配滤波与窗函数的设计参考：[MathWorks Radar Pulse Compression](https://www.mathworks.com/help/signal/ug/radar-pulse-compression.html)。
本文的具体频段、门槛和三脉冲组合为本工程的实验参数，须通过真实扬声器/麦克风验证。

## 构建与测试

```powershell
# 独立生成两种模式各自的 A/B 固件，不改原项目和角色默认值
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_range_profiles.ps1
# 仅生成新模式（IAR 路径可通过 -IarBuild 参数覆盖）
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_range_profiles.ps1 -Profiles wide
```

产物在 `EWARM/ranging_bsp/Exe/`：`ranging_wide_A/B.hex/.out`、`ranging_legacy_A/B.hex/.out`。
构建脚本显式指定所选模式，不受头文件默认模式影响；普通 IAR 原项目构建才使用头文件默认选择。

主机测试使用实际 C 检测器：

- `test_range_wide.py`：全部 256 个整数起点，弱信号、反相和连续波形的分数样点延迟；
  分数延迟测试误差小于 0.6 样点（不是毫米级精度验收）。
- 150 个模拟回声组合：直达增益 0.3，回声增益 0.15、0.45、-0.45，
  延迟 8/16/32/48/80 样点，10 种起始位置；返回直达起点误差小于 0.8 样点。
- 静音、随机噪声、单音、缺失脉冲、错误脉冲和旧音频拒绝。
- 完整新 WAV 五种对齐下均检出 15 个签名，组间静音无额外事件。
- 旧模式 `test_range_math.py` 全部通过，并拒绝新音频。
- 新旧模式 A/B 单次状态机、批次状态机回归全部通过，覆盖真实 C 连续音频处理。
- 四套 IAR 固件编译链接全部为 0 错误、0 警告，已生成对应 HEX/OUT；尚未烧录实测。
- WAV 与模板重新生成的字节完全一致，旧音频 SHA256 保持不变。

上板先确认对应屏幕标识、`SYNC READY` 和 `AT:OK`，再播放匹配的音频。统计仍要求至少 6 个
一致样本、60% 占比、40 mm 最大簇跨度和最终 100～200 mm 范围。等待首次配对后的 11 秒。
请用固定板距、共线外侧声源，在多个声源距离下比较新旧方案的失败率和距离偏差。
