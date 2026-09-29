# ranging_bsp · 双开发板声学测距

STM32F746G-DISCO双板，通过以太网统一时间基准，采集同一声音、交换到达时间，显示距离与A/B声源侧，并把双板录音和测量记录保存到A板SD卡。

- [项目整体状态与测试概况（2026-09-29）](PROJECT_STATUS_2026-09-29.md)
- [提交历史与功能日志索引](CHANGELOG.md)
- [early1回波尝试归档报告](EARLY1_ECHO_REPORT.md)
- [任务范围与验收指标来源](DEVELOPMENT_PLAN.md)（旧计划，实现状态以当前状态文档为准）

## 1. 最新进展

当前分支：**`codex/ui-features`**。固件基线：**`547e914`**，48 kHz LEGACY/JOINT2，含SD保存功能。下一阶段进行UI优化及其他功能开发。

early1回波实验已归档至`codex/echo-early1`（代码`b33a16a`、报告`09a6611`），未合入当前固件。**回波处理暂停，待实验室验收环境测试基线后再决定是否继续。**

最后一次已知两板固件仍为early1；切换Git分支不会改变板上程序或旧构建产物。要测试基线，须重新编译并分别烧录A/B。

## 2. 整体目标

完成课程基础1～5及提高2/4/6：双板自动通信/同步、到达时间估计、0.20～2.00 m测距与A/B侧判断、环境/设备适应、现场温度补偿，以及单点标定、测量质量指示、同步波形。

主要验收指标包括同步脉冲±10 μs、单次误差±10 mm、至少10次标准差≤2 mm、两屏差≤1 mm及播放后2秒内显示。提高标定量程与精度、波形相位等完整要求见状态文档。**目前尚未完成整体验收。** 当前统计允许0.10～2.00 m不等同于量程精度已经达标。

声源应在两板外侧，距近端指定麦克风0.5～1 m，与两板指定L麦克风近似共线；测量的是声程差，几何不满足时不等于板间距离。四颗麦克风保留阵列布局。

## 3. 已有功能与验证

| 功能 | 当前实现 | 验证与边界 |
| --- | --- | --- |
| 网络 | A `.10` / B `.11`，UDP握手、心跳、ACK与恢复 | 双板实测及协议回归；长期/交换机负载验收待补 |
| 同步 | ETH硬件时间戳、频漂拟合、D9/PA15检验脉冲 | 用户报告±1.5 μs；连续边沿原始记录待归档 |
| 采集/检测 | 48 kHz双通道PCM、样点时基、LEGACY三脉冲联合候选 | 编译、主机回归和多轮录音验证；遮挡/回波仍有失败 |
| 距离/方向 | 两板L通道配对，A/B侧判断，单次预览及整轮统计 | 多个距离点结果；统计通过不保证选中直达声 |
| 波形/状态 | 同步后500 ms整帧刷新，最近160 ms本地L/R波形，诊断快照 | 已上板；同步刷新不等于波形相位指标已验收 |
| 数据保存 | A板USER键保存双板PCM/锚点/事件，成功后清零并重同步 | 实卡写入、回读校验和录音连续性已验证 |

当前温度为编译期25°C，固定偏置为0；结果保持15秒。统计窗口11秒，至少6个样本、至少60%且严格多数、簇跨度≤40 mm。现场温度交互与标定尚未完成。

常规测试：A插FAT/FAT32 SD卡、两板联网 → 等`CAP ARM`、`SYNC READY`、`AT:OK` → 播放[48 kHz测试音](tools/test_audio_chirp_48k_repeat.wav)一次（10.5+5秒）→ `CAP HELD`后按A蓝色USER键 → 等保存完成及重新同步。失败轮也保存，完整目录须含`A.RNG`、`B.RNG`及`COMPLETE.TXT`。详细异常处理和解码步骤见状态文档第6节。

已有主机回归入口（需要本机MSVC与相应Python依赖，脚本中的工具路径可调整）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_host_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_scope_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_capture_tests.ps1
```

这些脚本已有通过记录，本次文档整理没有重跑或重新验收硬件。

## 4. 待开发内容

- UI布局与交互优化：清晰区分单次、统计、旧值、失败及保存状态，保留方向判断与必要诊断。
- 现场温度设置、声速展示及参数持久化；单点标定流程与误差预算。
- 验收环境下的单次精度、重复性、两屏差、显示延迟、静默/换设备/异常恢复测试。
- 同步波形的单音相位、抖动及移动实验；不能仅以两屏同时刷新代替验收。
- 回实验室后评估是否需要恢复回波开发。当前不继续调门限、不进行RTOS重构或2 m以上新测试。

## 5. 工程结构

| 路径 | 职责 |
| --- | --- |
| `Core/Inc/app_board_config.h` | A/B角色、采样率、音频方案、温度和统计配置 |
| `Core/Src/main.c` | 初始化、MPU与主循环调度 |
| `Core/Src/app_net.c` | 双板网络与会话管理 |
| `Core/Src/range_clock.c`、`range_sync.c` | 硬件时基、D9脉冲及同步模型 |
| `Core/Src/app_range.c`、`range_dsp.c` | 音频时基、检测、事件与测距 |
| `Core/Inc/range_peak_pair.h`、`range_batch.h` | 联合候选配对、批次统计 |
| `Core/Src/app_mic_scope.c` | 本地双通道波形与测量界面 |
| `Core/Src/app_capture.c`、`capture_sd.c` | 双板录音事务及SD驱动 |
| `LWIP/`、`FATFS/`、`Drivers/`、`Middlewares/` | 网络、文件系统与芯片依赖 |
| `ranging_bsp.ioc`、`EWARM/ranging_bsp.ewp` | CubeMX及IAR配置 |
| `tools/`、`tools/tests/` | 音频生成、角色构建、解码及主机回归 |
| `commit_logs/` | 功能实现、测试与交接记录 |

仓库还依赖上层目录`../../Drivers/BSP/STM32746G-Discovery`、`../../Drivers/BSP/Components`及`../../Utilities/Fonts`。单独克隆时须补齐匹配依赖，保留共享相对路径。

## 6. 工作交接规范

1. 先读当前状态、分支及`git status`，保留同伴未提交改动；明确源代码版本和板上固件版本。
2. 功能修改在`commit_logs/`记录目的、关键文件/接口、单位与时基、资源影响、验证、未验证项和下一步，并更新[历史索引](CHANGELOG.md)。源码与日志一起提交，文档改变也简述原因。
3. ISR只做必要搬运/计数；绘图、写卡和长计算放在主循环。修改DMA/SDRAM/Cache/MPU/时钟/中断时须核对连续采集和网络负载。
4. `LOCAL MIC L/R`均为本板麦克风。测距使用L；UI接口`MicScope_SetDistanceMm`接收毫米，仅在新结果到达时发布，不重复延长旧结果。接口须标明调用上下文、缓冲归属和生命周期。
5. 协议、采样率、模板或温度配置变化时两板保持一致。CubeMX生成后检查main用户区、`ethernetif.c`时间戳补丁、BSP句柄、SDDMA/IRQ和IAR源文件，不把生成覆盖当成无意义差异。
6. 固件修改至少编译相关A/B配置并做针对性验证；区分主机通过、编译通过、实机通过与完整验收。保存失败轮、音频版本、真实几何、播放设备/音量和温度。
7. 暂存前审阅差异；不无差别提交个人IDE状态、临时目录、构建产物或大体积原始录音。共享`.ioc/.ewp/.ewd`变更说明原因。推送、烧录和测试记录各自明确。

## 7. 命令行分别编译与烧录两板

在工程根目录的PowerShell运行。以下使用已验证的IAR 8.2安装路径和本项目探针映射，换电脑/探针后须核对路径与序列号。`.xcl`内也包含本机路径。

### 编译A/B

脚本一次运行会**分别创建A/B角色项目并依次编译**，无需手动改头文件；当前头文件默认B。本脚本没有单独`-Board`选项。必须带`-JointPeaks`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_range_profiles.ps1 -SampleRate 48000 -Profiles legacy -JointPeaks
if ($LASTEXITCODE -ne 0) { throw 'A/B build failed' }
```

对应产物位于`EWARM/ranging_bsp/Exe/`：

- A：`ranging_legacy_joint2_A_48000Hz.out` / `.hex`
- B：`ranging_legacy_joint2_B_48000Hz.out` / `.hex`

必须确认刚刚从目标分支构建成功，不直接使用切分支前留下的旧产物。

### 分别烧录A/B

两块板连接ST-LINK USB。下列两个调用按探针序列号分别下载，并在完成后运行；驱动配置已启用下载校验。仅阅读文档不会执行烧录。

```powershell
$cspy = 'C:/Program Files (x86)/IAR Systems/Embedded Workbench 8.2/common/bin/CSpyBat.exe'
$general = (Resolve-Path 'EWARM/settings/ranging_bsp.ranging_bsp.general.xcl').Path
$driver = (Resolve-Path 'EWARM/settings/ranging_bsp.ranging_bsp.driver.xcl').Path
function Flash-RangeBoard([string]$Board, [string]$Probe) {
    $firmware = (Resolve-Path "EWARM/ranging_bsp/Exe/ranging_legacy_joint2_${Board}_48000Hz.out").Path
    & $cspy -f $general "--debug_file=$firmware" --download_only --leave_target_running --silent --timeout 60000 --backend -f $driver "--drv_communication=USB:#$Probe"
    if ($LASTEXITCODE -ne 0) { throw "Board $Board download failed" }
}
Flash-RangeBoard 'A' '67230834'
Flash-RangeBoard 'B' '87134138'
```

既有探针对应：A完整序列号`0675FF514966504867230834`，B为`0667FF485153826687134138`；命令使用IAR识别的短号。A应为插SD卡的板，不要仅凭USB枚举次序分配角色。烧录后连接两板网络，检查角色、`NET ONLINE`、`SYNC READY`、`AT:OK`，再开始测试。
