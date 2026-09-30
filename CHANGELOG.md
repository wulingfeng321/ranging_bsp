# 提交历史与功能日志索引

更新日期：2026-09-30。由README迁出；当前状态见[项目状态](PROJECT_STATUS.md)，操作入口见[README](README.md)。

## 1. 关键Git提交

按Git作者日期倒序列出人工功能/文档提交，省略自动会话检查点；完整历史以`git log --all`为准。日期不是独立硬件验收日期。

| 日期 | 提交 | 简述 |
| --- | --- | --- |
| 2026-09-29 | `b77b034` | 整理根目录说明、历史索引及9月29日状态，为UI开发交接 |
| 2026-09-29 | `35390ba` | 从547e914建立UI功能分支，仅带入README和early1报告 |
| 2026-09-29 | `09a6611` | 补充early1归档报告，明确暂停回波开发、等待实验室测试 |
| 2026-09-29 | `b33a16a` | 归档early1早到选峰C实现、离线回放工具和新实测分析 |
| 2026-09-29 | `547e914` | 双板原始音频/诊断保存到A板SD卡，完成传输、清零及SD DMA修复 |
| 2026-09-28 | `dd73c22` | 保存同步整帧波形、联合选峰失败诊断及阶段测试检查点 |
| 2026-09-26 | `9a9885e` | 采集、模板、窗口和测试音迁移至48 kHz，保留16 kHz回归 |
| 2026-09-26 | `464cb35` | 保存16 kHz基线、D9同步检验脉冲及音频分析记录 |
| 2026-09-23 | `c5fc11d` | 加入JOINT2三脉冲候选容差及当时的项目状态文档 |
| 2026-09-21 | `bf197d0` | 改进重复测试音生成脚本 |
| 2026-09-19 | `8d51016` | TEST3声学测距、音频时间模型和诊断/统计改进 |
| 2026-09-18 | `3e1971f` | 实现并验证A/B双板UDP会话、心跳与业务通信 |
| 2026-09-18 | `010431b` | 记录任务书范围、验收指标及后续开发路线 |
| 2026-09-18 | `89abe36` | 启用LwIP，建立192.168.10.10网络基线 |
| 2026-09-17 | `be60e1a` | 保存TIM5 1 MHz配置，清理冗余工程备份 |
| 2026-09-17 | `d038661` | 波形覆盖扫描及候选测试音触发保持（历史显示方式） |
| 2026-09-17 | `50c5c4c` | 加入测距UI、状态与毫米结果接口 |
| 2026-09-16 | `475d1f9` | 加入扫频测试音生成脚本和WAV |
| 2026-09-16 | `c58d07f` | 双麦采集及波形绘制 |
| 2026-09-16 | `d95032b` | 修正调试器配置、驱动LCD网格 |
| 2026-09-16 | `cc1b82f` | 补充BSP与字体的IAR搜索路径 |
| 2026-09-16 | `4980543` | 初始化开发板工程及依赖配置 |

分支：`codex/ui-features`从`547e914`继续UI/其他功能，`codex/echo-early1`保存`b33a16a`实验代码及`09a6611`报告；归档分支上的提交不属于当前固件。

## 本次检查点：四选项卡触摸UI框架

四选项卡、两板联动模式/温度、同步计数与三脉冲到达时刻；完整内容和验证见[2026-09-29-touch-ui](commit_logs/2026-09-29-touch-ui.md)。A/B编译0错误0警告，主机显示/控制、算法及录制回归通过；已分别烧录A/B且下载校验成功；用户确认保存/切页/温度补偿正常，双板约13 cm测试均显示0.129 m、STAT、15/15、SPAN 1 mm。整体验收和连续负载验证待完成。

## 2. 功能日志索引

- [2026-09-30 release工程整理检查点](commit_logs/2026-09-30-release-structure.md)：汇总依赖内置、main初始化清理、CubeMX工具链配置及IDE状态取消跟踪，创建release分支保存当前改动。
- [2026-09-30 移除早期UI演示初始化开关](commit_logs/2026-09-30-main-init-cleanup.md)：删除LCD_GRID_DEMO_ONLY及旧分支，保留当前启动调用顺序，A/B构建0错误0警告。
- [2026-09-30 BSP与字体依赖收齐](commit_logs/2026-09-30-local-dependencies.md)：依赖纳入Drivers，更新IAR及LCD字体引用，当前与隔离目录A/B构建均0错误0警告。
- [2026-09-30 文档与工具整理](commit_logs/2026-09-30-docs-tools-cleanup.md)：重排用户手册、更新工程状态与README、引用页面预览、明确后续计划，清理并统一PC音频工具。
- [2026-09-30 音频生成工具统一](commit_logs/2026-09-30-audio-tools.md)：统一生成/模板导出入口与命名，重生成播放音和回归输入，PCM与固件模板一致，主机回归通过。

这些记录描述当时实现、验证和限制，不代表当前默认配置。文件名日期不一定等于最终提交日期。

| 记录 | 内容简述 |
| --- | --- |
| [2026-09-16_475d1f9](commit_logs/2026-09-16_475d1f9.md) | 475d1f9 · 生成重复扫频测试音 |
| [2026-09-16_4980543](commit_logs/2026-09-16_4980543.md) | 4980543 · 初始化开发板工程与依赖 |
| [2026-09-16_c58d07f](commit_logs/2026-09-16_c58d07f.md) | c58d07f · 双麦克风采集与波形显示 |
| [2026-09-16_cc1b82f](commit_logs/2026-09-16_cc1b82f.md) | cc1b82f · 补充 BSP 与字体搜索路径 |
| [2026-09-16_d95032b](commit_logs/2026-09-16_d95032b.md) | d95032b · 修正调试器并显示 LCD 网格 |
| [2026-09-17_50c5c4c](commit_logs/2026-09-17_50c5c4c.md) | 50c5c4c · 测距仪表界面与结果接口 |
| [2026-09-17_readme-handoff](commit_logs/2026-09-17_readme-handoff.md) | 完善项目 README 与交接规范 |
| [2026-09-17_tim5-backup-cleanup](commit_logs/2026-09-17_tim5-backup-cleanup.md) | 配置 TIM5 计时分频并移除冗余备份目录 |
| [2026-09-17_waveform-overwrite](commit_logs/2026-09-17_waveform-overwrite.md) | 波形更新改为覆盖扫描 |
| [2026-09-17_waveform-trigger-hold](commit_logs/2026-09-17_waveform-trigger-hold.md) | 移除扫描线，增加候选测试音触发保持 |
| [2026-09-18_chirp-ranging](commit_logs/2026-09-18_chirp-ranging.md) | 指定 Up–Down–Up 音频双板测距（实验版） |
| [2026-09-18_dual-board-udp](commit_logs/2026-09-18_dual-board-udp.md) | A/B 双板 UDP 通信首版 |
| [2026-09-18_ethernet-ping-test](commit_logs/2026-09-18_ethernet-ping-test.md) | STM32746G-Discovery 网口直连测试 |
| [2026-09-18_ltdc-bsp-irq-handle](commit_logs/2026-09-18_ltdc-bsp-irq-handle.md) | 修复模块拆分后 LTDC 中断使用错误句柄 |
| [2026-09-18_lwip-eth-init-build-fix](commit_logs/2026-09-18_lwip-eth-init-build-fix.md) | 修复启用 LwIP 后的 MX_ETH_Init 未定义错误 |
| [2026-09-18_project-roadmap](commit_logs/2026-09-18_project-roadmap.md) | 任务书范围确认与开发路线整理 |
| [2026-09-19_audio-time-model](commit_logs/2026-09-19_audio-time-model.md) | BOARD RANGE TEST3：抗音频回调抖动的时间映射 |
| [2026-09-19_board-distance-test](commit_logs/2026-09-19_board-distance-test.md) | BOARD RANGE TEST1：共线外侧声源测试 |
| [2026-09-19_range-diagnostics](commit_logs/2026-09-19_range-diagnostics.md) | DIAG1 测距链路诊断 |
| [2026-09-19_range-event-timeout](commit_logs/2026-09-19_range-event-timeout.md) | DIAG3 新事件/结果被错误判超时修复 |
| [2026-09-19_range-stability](commit_logs/2026-09-19_range-stability.md) | BOARD RANGE TEST2：到达位置与窗口时基一致性 |
| [2026-09-19_range-throughput](commit_logs/2026-09-19_range-throughput.md) | DIAG2 音频检测积压修复 |
| [2026-09-19_relaxed-detection](commit_logs/2026-09-19_relaxed-detection.md) | 测距检测宽松调试参数 |
| [2026-09-19_repeat-statistics](commit_logs/2026-09-19_repeat-statistics.md) | BOARD RANGE STAT1：重复签名的分组统计估计 |
| [2026-09-19_stat-preview](commit_logs/2026-09-19_stat-preview.md) | BOARD RANGE STAT2：恢复单次可见反馈 |
| [2026-09-21_joint-peaks](commit_logs/2026-09-21_joint-peaks.md) | JOINT1：旧音频的多候选峰联合配对 |
| [2026-09-21_joint2-tolerance](commit_logs/2026-09-21_joint2-tolerance.md) | JOINT2：修复候选峰提取过严 |
| [2026-09-21_repeat-cluster-fix](commit_logs/2026-09-21_repeat-cluster-fix.md) | BOARD RANGE STAT4：重复音频统计筛选修复 |
| [2026-09-21_wide-audio-profile](commit_logs/2026-09-21_wide-audio-profile.md) | 新旧音频与检测算法切换 |
| [2026-09-24-sync-pps](commit_logs/2026-09-24-sync-pps.md) | D9 同步时钟检验脉冲 |
| [2026-09-26-audio-48khz](commit_logs/2026-09-26-audio-48khz.md) | 48 kHz 采样与测距 |
| [2026-09-26-detector-diagnostics](commit_logs/2026-09-26-detector-diagnostics.md) | 1.5 m 漏检：检测阶段诊断与测试方案 |
| [2026-09-26-scope-sync-refresh](commit_logs/2026-09-26-scope-sync-refresh.md) | 48 kHz：低频整帧波形与固定诊断 |
| [2026-09-28-testing-summary](commit_logs/2026-09-28-testing-summary.md) | 2026-09-28 声学测距测试汇总与分析 |

## 3. early1分支专属资料

- 根目录[early1归档报告](EARLY1_ECHO_REPORT.md)已同步到当前分支。
- `commit_logs/2026-09-29-early1-testing-analysis.md`及相关曲线/回放脚本仅在`codex/echo-early1`中；该分支的9月28日汇总还追加了R1～R19与移植分析。
- 查看方式：`git show codex/echo-early1:commit_logs/2026-09-29-early1-testing-analysis.md`。

## 4. 前次文档整理（已提交为b77b034）

新增9月29日项目快照，重写README为当前工程入口，将历史索引迁入本文件，补齐分别构建与烧录A/B的命令。没有改变固件、没有编译或烧录；检查文档链接和命令语法。对应提交为`b77b034`。后续触摸UI框架由本次检查点提交保存。

## 5. 功能日志模板

```markdown
# 功能名称
- 日期 / 分支 / 开发基线 / 完成状态
## 目的与范围
## 修改内容
- 文件、接口、单位、时基、兼容性与资源影响
## 验证
- 工具和板卡版本、接线、复现命令、预期与实测结果
- 编译警告、未验证项、原始记录位置
## 已知问题与下一步
- 接手首步、验收条件、回退位置
```

## README参数说明补充

在UI框架检查点`b09767f`之后，补齐README第8节STANDARD与DETAILS全部字段、单位、A/B差异、历史快照及计数重置/显示截断规则，特别区分三种Q、两种N、DS/DT和同步不确定度。逐项核对当前显示与计算代码；仅文档修改，不改变固件、不重新烧录。

## 实时同步波形（2026-09-29）

本板L麦克风10 ms窗口、20 Hz公共时间呈现、A板约5秒单音周期估计并共享；保留移动引起的相位差，提供两板联动重新校准。详见[实现与验证](commit_logs/2026-09-29-live-wave.md)。

## 使用说明书整理（2026-09-29）

新增[USER_MANUAL.md](USER_MANUAL.md)，迁移README中的STANDARD及DETAILS完整参数说明，并整理同步波形操作、所有显示字段与常见问题。README保留索引及工程说明。用户确认波形修复版效果符合预期；本次仅文档整理，未修改固件、未重新编译或烧录。

## 四麦方向初版（2026-09-29）

POSITION按13×2cm矩形实现标准扫频方向估计、A摆放图/B方向箭头、已知位置校准；禁用定位页保存。专用音频生成器位于tools，验证与限制见[定位日志](commit_logs/2026-09-29-position.md)。

定位首轮实机修复：2Hz显示、静默粗筛/有声精搜索与限时处理、积压恢复保留校准、增加本地诊断，并对app_range.c单独启用IAR高优化。详细原因与未确认项见定位日志。

定位第二轮修复：针对高相关/E0反馈，改用正交粗筛+局部细搜索、2ms完成事件，保留跨窗口候选；G/B拆分音频缺口与消费积压。带背景噪声/受限调度及边界回归通过，A/B已编译烧录，待实测。

按实测修正定位L/R上下映射、消除前后镜像；校准改为正前方50cm并更新球面声程补偿。已编译、烧录两板。评估当前阵列的距离可辨识性后，暂不提供数值距离。

- POSITION背景检测降载：合并粗筛遍历，增加LAG/DSP/LCD诊断，保持音频、门限及2Hz刷新。A/B回归、构建及烧录完成，见[定位日志](commit_logs/2026-09-29-position.md)。

- POSITION负载诊断改用与旧行一致的Font12，压缩摆放图，完成两板编译及烧录。

- 用户确认POSITION粗方向功能符合预期，整理最终实测并建立Git检查点；接下来开发独立击掌测距。

- 新增CLAP独立击掌单次测距、厘米/声源侧显示及诊断，标准测距隔离；测试与验证边界见[击掌日志](commit_logs/2026-09-29-clap.md)。

- 2026-09-30：击掌首版收尾，用户确认已烧录；记录固件指纹和验证边界，待真实击掌验收。定位提交为`0e1319b`，CLAP暂保留工作区改动。

- 2026-09-30：用户确认击掌正常值满足要求；精简CLAP界面，增加最近6次/平均值、两板联动清空及10Hz显示目标，算法不变。

- 击掌测距检查点：`1bca14e`。
- [2026-09-30 显示调整汇总](commit_logs/2026-09-30-display.md)：B板定位图对齐、P2/P3本地峰间隔、标准/定位最终各板独立2Hz及检测优先保护；含最新实测记录。
- [2026-09-30 自动温度接口与提示同步](commit_logs/2026-09-30-auto-temperature.md)：AUTO预留入口、传感器接入约束及两板反馈同步，独立成篇。
