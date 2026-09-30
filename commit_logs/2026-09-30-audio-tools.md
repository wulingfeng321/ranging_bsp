# 音频生成工具统一（2026-09-30）

## 目的与范围

清理tools原有音频与分散生成脚本，统一生成入口、参数和文件命名。仅调整PC工具、回归输入路径与相关说明，固件源码/模板数值及板上程序保持原状。

## 修改内容

- 新目录tools/audio/：generate.py提供standard/position/sine/wide/all命令；waveforms.py提供无第三方依赖的PCM函数；export_template.py单独执行模板导出。
- 删除tools根目录的6个旧生成/模板脚本和旧音频，包括用户恢复的sound_make.py；其逻辑合并进入新工具。
- 默认输出generated/，名称为用途_采样率hz_时长s.wav，小数以p表示。all生成3个当前48 kHz播放音，all --regression追加5个标准单组/16 kHz/WIDE输入。
- 参数统一通过argparse注入，校验有限数值、范围、采样率、脉冲间隔和WAV/头文件输出扩展名；默认输出不依赖启动目录。导入不生成文件，生成WAV不自动改写Core头文件。
- 回归读取统一到tools/tests/audio_paths.py；run_host_tests.ps1先运行音频工具测试，再生成回归输入，避免依赖手工恢复旧文件。

## 验证

- 对用户恢复的sound_make.py生成结果进行16/48 kHz逐样本比较，全部一致。已有标准重复音、定位音及500 Hz音的PCM、声道、采样率、位宽和帧数也逐项一致。
- 显式导出STANDARD/WIDE、16/48 kHz四套模板到临时目录，与当前Core/Inc的Up/Down数组逐样本一致，固件头文件未修改。
- test_audio_tools.py的6项测试通过：文件格式/完整组间时序、四套模板、定位60脉冲/正弦周期、参数和自定义路径、异常输入不写文件、导入无副作用。
- run_host_tests.ps1完整通过：标准16/48 kHz两角色状态机及采样时基、联合候选/线协议、768种48 kHz窗口对齐、200个分数延时、四偏移整段15.5秒/15事件、WIDE 48 kHz和计算量回归。
- 当前播放音的PCM不变，未执行新的IAR构建或烧录，也未将主机回归当作实验室精度验收。

## 使用与边界

入口：python tools/audio/generate.py all --regression。参数及全部输出清单见tools/audio/README.md。标准统计/录音窗口和定位模板仍对应原默认音频；自定义签名时序或频率后需重新检查固件匹配。
