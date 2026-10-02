# 工具目录

更新日期：2026-10-02。

本目录收录固件构建、保存数据解码、音频生成及主机测试工具。下列路径均相对于本目录；示例命令在工程根目录执行。

## 1. 固件构建与数据解码

| 脚本 | 大致作用 |
| --- | --- |
| [build_range_profiles.ps1](build_range_profiles.ps1) | 调用IAR，按固定48 kHz、LEGACY/WIDE方案及JointPeaks选项分别全量构建A/B固件，统一工程及文件级宏并核对实际编译命令，输出OUT/HEX和构建日志；清理临时角色工程，不执行烧录 |
| [decode_capture.py](decode_capture.py) | 检查SD保存轮的完成标记、文件大小及校验值，解码A/B的RNG文件，导出双声道WAV、元数据JSON、时间锚点与日志CSV；保留原始文件 |

```powershell
./tools/build_range_profiles.ps1 -SampleRate 48000 -Profiles legacy -JointPeaks
python tools/decode_capture.py 'E:/captures/R000001'
```

构建需要IAR EWARM，默认路径为8.2安装目录，可通过`-IarBuild`指定。解码使用Python 3标准库，默认输出到保存轮内的`decoded/`，也可通过`--output`指定目录。当前解码面向标准音频测距；当前固件已移除击掌保存入口及击掌日志，不再开发该项导出。编译/烧录和完整解码操作见[用户手册](../USER_MANUAL.md)。

构建脚本将板角色、采样率、音频方案及联合候选开关写入所有现有的IAR编译宏列表，包含文件/组级覆盖，避免同一固件的不同模块采用不同板角色。脚本执行全量构建，并逐条检查实际C编译命令；完整日志保存为`EWARM/<临时角色工程名>/List/build.log`，核对失败时停止并报错。原始工程文件和`app_board_config.h`不被脚本修改。

## 2. 音频生成与模板导出

| 脚本 | 大致作用 |
| --- | --- |
| [audio/generate.py](audio/generate.py) | 统一命令行入口，生成标准测距、WIDE回归、声源定位扫频及同步波形正弦音频；支持参数注入和批量生成 |
| [audio/waveforms.py](audio/waveforms.py) | PCM波形生成与WAV写入的公共模块，供生成入口调用 |
| [audio/export_template.py](audio/export_template.py) | 从对应WAV中提取扫频签名，生成去直流的C模板头文件；必须显式指定输入与输出 |

```powershell
python tools/audio/generate.py all
python tools/audio/generate.py all --regression
```

这些脚本仅依赖Python 3标准库。音频输出在`audio/generated/`；参数、命名规则、五个默认音频及模板导出步骤见[音频工具说明](audio/README.md)。

## 3. 主机测试与分析

主机测试用于检查代码逻辑和模拟输入，实机功能与实验室指标仍需单独验证。三个PowerShell入口使用MSVC，可通过`-VcVars`指定编译环境路径。

| 脚本 | 大致作用 |
| --- | --- |
| [tests/run_host_tests.ps1](tests/run_host_tests.ps1) | 检查音频工具并生成回归输入，构建48 kHz检测器与A/B状态机，运行时基、联合峰、WIDE及处理量回归 |
| [tests/run_scope_tests.ps1](tests/run_scope_tests.ps1) | 构建并运行音频板级适配、内存布局编译拒绝、独立波形/击掌模块、字节编码、网络、界面、触摸、击掌、定位、波形、公共时基及DSP诊断测试，生成模拟页面图像 |
| [tests/run_capture_tests.ps1](tests/run_capture_tests.ps1) | 检查SD驱动与A/B保存事务，并调用解码脚本验证模拟保存数据 |
| [tests/test_audio_tools.py](tests/test_audio_tools.py) | 检查音频格式、时序、模板一致性、自定义参数、异常输入和导入副作用 |
| [tests/test_48k.py](tests/test_48k.py) | 检查48 kHz检测器的窗口对齐、分数延迟、完整播放及噪声/单音拒绝 |
| [tests/test_wide_48k.py](tests/test_wide_48k.py) | 检查48 kHz WIDE检测器的音频窗口对齐 |
| [tests/measure_dsp_load.py](tests/measure_dsp_load.py) | 统计48 kHz检测处理量、总乘加计数及最重切片 |
| [tests/render_ui_previews.py](tests/render_ui_previews.py) | 将界面测试产生的PPM转为PNG并拼接预览，输出到`commit_logs/assets/2026-09-29-touch-ui/`；依赖Pillow，先运行界面测试 |
| [tests/dsp_types.py](tests/dsp_types.py) | C检测器和候选配对接口的Python ctypes结构声明 |
| [tests/test_board_audio.c](tests/test_board_audio.c) | 音频初始化、DMA半缓冲派发、超时及错误通知回归 |
| [tests/test_memory_layout.py](tests/test_memory_layout.py) | 在MSVC环境编译真实布局头文件及故意冲突的副本，确认重叠/越界/未对齐/格式尺寸错误被拒绝 |
| [tests/test_wave_module.c](tests/test_wave_module.c) | 独立链接波形模块，检查插值、历史数据覆盖/epoch变化、时钟包验证与序号回绕 |
| [tests/test_clap_module.c](tests/test_clap_module.c) | 独立链接A/B击掌模块，检查滚动历史、载荷校验、重复/旧包、序号回绕及重传/过期时间边界 |
| [tests/test_wire.c](tests/test_wire.c) | 大小端编码的独立已知字节向量与非对齐访问回归 |
| [tests/audio_paths.py](tests/audio_paths.py) | 统一提供音频回归输入路径，供测试脚本导入 |

```powershell
./tools/tests/run_host_tests.ps1
./tools/tests/run_scope_tests.ps1
./tools/tests/run_capture_tests.ps1
```

DSP音频分析测试依赖NumPy；图像转换脚本依赖Pillow。可用`python -m pip install numpy pillow`准备这两项依赖，生成音频和解码RNG仍只需标准库。

部分独立Python测试需要传入具有对应导出接口的主机动态库，具体参数见各脚本开头。`run_host_tests.ps1`不会自动执行表内所有分析或历史回归脚本。

`tests/`中的C文件是测试程序及动态库适配代码，覆盖测距/批次/时钟、击掌/定位/波形、网络/界面和保存逻辑；`*_stubs/`为主机模拟硬件接口。`__pycache__/`为Python运行缓存，不属于工具源文件。

## 4. 维护约定

新增、移动或删除工具时同步更新本目录；专用工具的详细参数保留在对应子目录说明中。生成音频与模板时核对固件采样率和方案；测试产物、模拟图像与原始实验数据分别保存，避免混淆验证来源。
