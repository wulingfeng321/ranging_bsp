# 工具目录

更新日期：2026-10-26。

本目录收录固件构建、保存数据解码、网页实验分析、音频生成及主机测试工具。下列路径均相对于本目录；除另有说明，示例命令在工程根目录执行。

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
| [tests/run_scope_tests.ps1](tests/run_scope_tests.ps1) | 构建并运行音频板级适配、内存布局编译拒绝、独立波形/击掌/定位模块、测量协议编解码与收发故障、字节编码、网络、界面、触摸、击掌、定位、波形、公共时基及DSP诊断测试，生成模拟页面图像 |
| [tests/run_capture_tests.ps1](tests/run_capture_tests.ps1) | 检查SD驱动与A/B保存事务，并调用解码脚本验证模拟保存数据 |
| [tests/test_audio_tools.py](tests/test_audio_tools.py) | 检查音频格式、时序、模板一致性、自定义参数、异常输入和导入副作用 |
| [tests/test_48k.py](tests/test_48k.py) | 检查48 kHz检测器的窗口对齐、分数延迟、完整播放及噪声/单音拒绝 |
| [tests/test_wide_48k.py](tests/test_wide_48k.py) | 检查48 kHz WIDE检测器的音频窗口对齐 |
| [tests/measure_dsp_load.py](tests/measure_dsp_load.py) | 统计48 kHz检测处理量、总乘加计数及最重切片 |
| [tests/render_ui_previews.py](tests/render_ui_previews.py) | 将界面测试产生的PPM转为PNG并拼接预览，输出到`commit_logs/assets/2026-09-29-touch-ui/`；依赖Pillow，先运行界面测试 |
| [tests/dsp_types.py](tests/dsp_types.py) | C检测器和候选配对接口的Python ctypes结构声明 |
| [tests/test_remote_temperature.c](tests/test_remote_temperature.c) | 双角色远程AUTO，读取去重/重传、同温度保留结果、旧设置拒绝、反馈乱序、故障、取消与断线恢复 |
| [tests/test_temperature.c](tests/test_temperature.c) | DHT11响应时序/小数/负温与校验，实际板级状态机的超时、预读、缓存失效、计时回绕及B板不启动硬件；使用temperature_stubs寄存器模型 |
| [tests/test_board_audio.c](tests/test_board_audio.c) | 音频初始化、DMA半缓冲派发、超时及错误通知回归 |
| [tests/test_memory_layout.py](tests/test_memory_layout.py) | 在MSVC环境编译真实布局头文件及故意冲突的副本，确认重叠/越界/未对齐/格式尺寸错误被拒绝 |
| [tests/test_wave_module.c](tests/test_wave_module.c) | 独立链接波形模块，检查插值、历史数据覆盖/epoch变化、时钟包验证与序号回绕 |
| [tests/test_position_module.c](tests/test_position_module.c) | 独立链接A/B定位模块，检查分片搜索/时刻换算、缓冲竞争、载荷和重传边界、校准及恢复 |
| [tests/test_clap_module.c](tests/test_clap_module.c) | 独立链接A/B击掌模块，检查滚动历史、载荷校验、重复/旧包、序号回绕及重传/过期时间边界 |
| [tests/test_range_protocol.c](tests/test_range_protocol.c) | 独立RAN2固定字节向量、非对齐缓冲、全位宽载荷及包头/会话拒绝 |
| [tests/test_range_transport.c](tests/test_range_transport.c) | A/B测量协调层ARP/TX时间戳准入、收发故障、来源/会话/长度检查及pbuf生命周期 |
| [tests/test_wire.c](tests/test_wire.c) | 大小端编码的独立已知字节向量与非对齐访问回归 |
| [tests/audio_paths.py](tests/audio_paths.py) | 统一提供音频回归输入路径，供测试脚本导入 |

```powershell
./tools/tests/run_host_tests.ps1
./tools/tests/run_scope_tests.ps1
./tools/tests/run_capture_tests.ps1
```

`run_host_tests.ps1`与`run_scope_tests.ps1`共用临时目录`%TEMP%/ranging-host-tests`，应依次执行，避免中间文件互相覆盖。

DSP音频分析测试依赖NumPy；图像转换脚本依赖Pillow。可用`python -m pip install numpy pillow`准备这两项依赖，生成音频和解码RNG仍只需标准库。

部分独立Python测试需要传入具有对应导出接口的主机动态库，具体参数见各脚本开头。`run_host_tests.ps1`不会自动执行表内所有分析或历史回归脚本。

`tests/`中的C文件是测试程序及动态库适配代码，覆盖测距/批次/时钟、击掌/定位/波形、网络/界面和保存逻辑；`*_stubs/`为主机模拟硬件接口。`__pycache__/`为Python运行缓存，不属于工具源文件。

## 4. 可视化工具

[`web_tools/`](web_tools/README.md) 为 Ranging Studio 网页工具的独立仓库，提供RNG导入与校验、配对和波形联动、检出事件标记、时钟/采集状态、解析记录浏览、双板音频播放、多轮实验对比、AI报告和管理页面。操作流程见[用户手册第6章](../USER_MANUAL.md#6-网页实验分析工具)。仅需拆分WAV/CSV时，可继续使用下文的`decode_capture.py`。

### 获取与启动

子模块远程仓库为[ranging_web_tool](https://github.com/wulingfeng321/ranging_web_tool)，SSH地址为`git@github.com:wulingfeng321/ranging_web_tool.git`，开发分支为`wlf/local-analysis`。主工程`release`分支记录固定子模块提交号；电脑需具备两个仓库的SSH访问权限。

```powershell
# 已有主工程，在工程根目录初始化或同步所记录的网页工具版本
git submodule sync -- tools/web_tools
git submodule update --init -- tools/web_tools
```

首次克隆主工程的完整命令见[根目录README](../README.md#31-获取网页工具子模块)。初始化只获取源码，不包含依赖、构建产物、实验数据或私有配置。本机已准备运行环境，可直接双击`tools\web_tools\start-local.cmd`，浏览器打开[127.0.0.1:8000](http://127.0.0.1:8000)。

新Windows电脑先安装Git、Python和Node.js/npm；本地已验证Python 3.13、Node.js 24。准备依赖和前端构建：

```powershell
# 从主工程根目录开始，执行后停留在网页工具根目录
Set-Location tools/web_tools
py -3.13 -m venv backend/.venv
.\backend\.venv\Scripts\python.exe -m pip install -r backend/requirements.txt
Push-Location frontend
npm.cmd ci
npm.cmd run build
Pop-Location
.\start-local.cmd
```

保持启动窗口运行，按Ctrl+C停止。应用使用React/TypeScript/Plotly、FastAPI和SQLite；普通本地分析无需额外数据库服务或AI Key。需要管理功能时，在网页工具根目录运行`.\backend\.venv\Scripts\python.exe scripts/set_admin_password.py`设置管理密码，然后重启服务。

### 数据、配置与部署

| 路径（相对web_tools） | 用途 | Git管理 |
| --- | --- | --- |
| `backend/app/`、`frontend/src/` | 后端分析/API及网页源码 | 是 |
| `config/prompts/`、`config/ai.example.toml` | AI提示词和不含密钥的示例配置 | 是 |
| `data/` | SQLite实验记录、上传文件、音频缓存等；可由`RANGING_DATA_DIR`改写位置 | 否，单独备份 |
| `config/ai.local.toml`、`config/admin.local.json` | 本机AI配置和管理密码哈希 | 否，私有配置 |
| `backend/.venv/`、`frontend/node_modules/`、`frontend/dist/` | 本机依赖和前端构建 | 否，在目标电脑准备 |
| `deploy/`、`scripts/package_release.py` | Linux部署配置及发布打包入口 | 是 |

公开展示地址：[ranging.wulingfeng.top](https://ranging.wulingfeng.top)。云端运行于腾讯云Ubuntu 24.04，普通页面公开，AI配置和删除操作要求管理登录。本地与云端数据独立，Git推送不会自动同步实验或部署网站；详细发布、HTTPS、备份和恢复步骤见[服务器维护说明](web_tools/docs/server-deployment.md)，AI接入见[AI说明](web_tools/docs/ai-analysis.md)。原迁移时的本机裸仓库、清单及`local-backup`配置已清理，获取代码直接使用GitHub。

### 开发、验证与提交

子模块初始化后可能处于分离HEAD状态，这是按主工程固定提交检出的正常结果。准备编辑时先检查子仓库状态，再切换到已有开发分支；不要在未核对改动时强制更新或重置子模块。前后端开发服务的启动方式见[网页工具README](web_tools/README.md)。

以下在网页工具根目录执行，后端测试使用隔离的临时数据库：

```powershell
Push-Location backend
.\.venv\Scripts\python.exe -m pytest tests -q
Pop-Location
Push-Location frontend
npm.cmd test
npm.cmd run build
Pop-Location
```

修改后先在子仓库审阅、提交并推送，再提交主工程的子模块指针。以下示例仅更新网页工具版本；其他文档按实际修改单独暂存：

```powershell
# 网页工具根目录：先用git status/git diff检查，再暂存具体修改文件并提交
git push origin wlf/local-analysis
# 返回主工程根目录，记录已推送的子仓库提交
Set-Location ../..
git add -- tools/web_tools
git commit -m "chore: update web tool submodule"
git push origin release
```

主仓库记录子仓库提交号，不将网页源码重复纳入自身历史；仅推送主仓库不能传输未推送的子仓库提交。其他电脑拉取主仓库后执行`git submodule update --init -- tools/web_tools`获取该固定版本，无需用`--remote`追随分支最新提交。

## 5. 维护约定

新增、移动或删除工具时同步更新本目录；专用工具的详细参数保留在对应子目录说明中。生成音频与模板时核对固件采样率和方案；测试产物、模拟图像与原始实验数据分别保存，避免混淆验证来源。

定位集成测试`test_position.c`保留直接检查峰值、校准内部状态的边界用例，在测试内包含`app_position.c`，该条编译不再另链接同一源文件；其他集成及独立模块测试正常链接app_position。生产接口不为白盒测试暴露内部状态。
