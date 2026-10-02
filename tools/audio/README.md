# 音频生成工具

更新日期：2026-10-02。全部脚本使用Python 3标准库，无需NumPy、SciPy或soundfile。统一由`generate.py`选择信号类型，`waveforms.py`提供PCM生成函数；导入模块不会写文件。

## 1. 生成入口与文件命名

在工程根目录执行：

```powershell
python tools/audio/generate.py all
python tools/audio/generate.py all --regression
```

第一条生成当前三个页面的48 kHz播放音频；第二条额外生成两个48 kHz模板/WIDE回归输入。默认输出固定在脚本旁的`generated/`，与运行时所在目录无关。

文件命名为`用途_采样率hz_时长s.wav`；正弦波用途包含频率，文件名中小数点写为`p`。例如`standard_repeat_48000hz_15p5s.wav`。所有音频均为单声道、16位PCM WAV。重复执行会覆盖同名生成文件，修改振幅或相同时长的其他参数时，可用`--output`保存不同实验版本。

| 默认文件 | 用途与参数 |
| --- | --- |
| `standard_repeat_48000hz_15p5s.wav` | STANDARD：2～6 kHz三脉冲，每组5签名、3组，组间额外静音1.5秒、末尾静音5秒；共15签名，振幅0.9 |
| `position_48000hz_30s.wav` | POSITION：12 ms、2～6 kHz Hann扫频，每0.5秒一次，共60脉冲，振幅0.65 |
| `sine_500hz_48000hz_60s.wav` | SYNC WAVE：连续500 Hz正弦波，60秒，振幅0.8 |
| `standard_group_48000hz_2p5s.wav` | 标准48 kHz单组与模板导出输入，5签名 |
| `wide_repeat_48000hz_15p5s.wav` | WIDE 48 kHz回归输入，1.5～6.5 kHz、4 ms脉冲间隙，振幅0.75 |

后两个文件由`--regression`生成，用于现有回归或显式模板导出。当前板上STANDARD使用LEGACY/JOINT2，WIDE音频用于匹配WIDE配置的测试。

## 2. 参数与示例

四种单文件命令统一支持`--sample-rate`、`--amplitude`、`--output-dir`及`--output`。振幅范围0～1；采样率仅支持48000；16 kHz固件、模板与默认音频已移除。时间单位为秒，频率单位为Hz。

| 命令 | 专用参数与默认值 |
| --- | --- |
| `standard` / `wide` | `--rounds 3`、`--signatures 5`、`--interval 0.5`、`--gap-seconds 1.5`、`--tail-seconds 5`；`--group-only`只生成一组并忽略轮数、额外组间静音与末尾静音 |
| `position` | `--duration 30`、`--interval 0.5`，间隔不得短于12 ms脉冲 |
| `sine` | `--duration 60`、`--frequency 500`，频率须低于采样率的一半 |
| `all` | 默认生成三种当前播放音；`--regression`加入两个回归输入；可用`--output-dir`指定整批目录 |

```powershell
python tools/audio/generate.py standard --rounds 5 --gap-seconds 3 --output-dir 'E:/audio-tests'
python tools/audio/generate.py position --duration 60
python tools/audio/generate.py sine --frequency 550 --duration 30 --amplitude 0.6
python tools/audio/generate.py wide --sample-rate 48000
```

`--output`指定单文件路径并优先于`--output-dir`；输出目录会自动创建，路径须以`.wav`结尾。输入须为有限数值；时长/间隔为正，轮数/签名数至少1，组间/末尾静音可为0。

默认波形保留旧生成器的采样率、相位、渐变窗、PCM量化和时序。修改采样率、扫频签名或模板参数后，须保持固件配置匹配；改变轮数/播放时序后，11秒统计窗和16秒保存窗不一定覆盖整段音频。

## 3. 显式模板导出与验证

音频生成不会修改`Core/Inc`。只有显式调用`export_template.py`并指定输出位置才生成去直流的C模板：

```powershell
python tools/audio/export_template.py --profile standard --sample-rate 48000 --input tools/audio/generated/standard_group_48000hz_2p5s.wav --output 'E:/audio-tests/range_template_48k.h'
python tools/tests/test_audio_tools.py
```

模板导出支持`standard`/`wide`及48000；输入须为对应签名的单声道PCM16，输出须为`.h`。确认模板与固件配置、音频一致后再按工程流程使用。

`run_host_tests.ps1`先检查生成工具，再生成默认回归输入，执行现有C检测器/状态机和Python音频回归。音频工具测试覆盖WAV格式、完整时序、两套48 kHz固件模板逐样本一致性、自定义参数、异常输入及无导入副作用。
