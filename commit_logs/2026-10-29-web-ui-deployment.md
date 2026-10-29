# 网页界面检查点与服务器部署（2026-10-29）

## 同日补丁：分析标签页动画

子仓库提交 `eac45e91aaecbefeb5eb637d75695dd588a12927` 已部署至 `/opt/ranging-studio/releases/20261029-rhine-eac45e9`。信号分析、采集诊断、录音与记录、AI研究报告归为同一页面，互相切换时直接更新内容并保持滚动位置；跨页面导航仍使用原有动画。

前端9项测试及生产构建通过，服务器48项后端测试通过。切换后服务 active、Nginx配置检查及公网HTTPS健康检查通过，公网HTML和主JS与发布构建一致。未额外完成浏览器交互复核。

本次备份 `/var/backups/ranging-studio/backup-20261029T070645-1874538.tar.gz` 已验证；发布前数据库完整性检查通过，15条实验记录（14条可见）与6条AI记录保留，AI和管理配置校验一致。前一个版本 `/opt/ranging-studio/releases/20261029-rhine-87789f3` 保留，可使用下文同样的回退流程并将目标目录替换为此路径。以下内容为当日首次发布记录。

本次按用户要求保存当前界面工作并部署至腾讯云。主工程与网页子仓库均继续使用 `wlf/rhine-ui` 分支，不合并到稳定分支，不修改板端固件。

## 版本与改动

- 网页子仓库提交：`87789f3e3d814e6e98989224b6a59caf698e27e8`，`feat(ui): finalize translucent archive interface for deployment`。
- 首页档案卡增加半透明材质，选中时显示暖金色边缘、柔和光晕及编号；保持原有卡片间距，不额外抬高全部真实档案。
- “进入分析”位于摘要右侧，保留清晰边框、双行文字和箭头。
- 包含此前的档案滚轮循环选择、页面过渡、分析工作台布局与状态配色调整。
- 本地启动入口通过源文件指纹判断前端是否需要重建，并核对实际服务的页面版本。
- 本地界面截图与设计说明见子仓库 `docs/rhine-ui.md` 和 `docs/screenshots/rhine-glass-glow-2026-10-29.jpg`。

## 线上发布

- 地址：<https://ranging.wulingfeng.top/>。
- 主机：腾讯云 Ubuntu 24.04，`43.142.15.59`。
- 服务：`ranging-studio`；入口链接：`/opt/ranging-studio/current`。
- 当前版本目录：`/opt/ranging-studio/releases/20261029-rhine-87789f3`，其中 `REVISION` 保存完整提交号。
- 保留的旧版本：`/opt/ranging-studio/releases/20261025-admin`。
- 发布包 SHA256：`d865a3b31046d2d324bae87d30a944055c90804e5accb8c4300c88c3a726ab37`。

使用项目发布打包脚本打包已跟踪的运行文件与前端构建产物；未包含本地实验数据、密钥及私有配置。在独立发布目录创建 Linux Python 环境、安装固定版本依赖并执行测试，随后原子切换入口链接、重启服务。切换脚本带失败回退，发布成功，未触发回退。

## 数据与备份

- 已验证备份包：`/var/backups/ranging-studio/backup-20261028T173725-1690599.tar.gz`（文件名采用 UTC，操作日期北京时间为10月29日）。
- 数据库完整性检查通过；发布前无正在解析的实验及正在运行的AI任务。
- 发布后保留11条实验记录，其中10条可见、1条在回收站；保留6条AI记录。
- AI配置与管理密码配置的发布前后校验值一致，未更改密钥或密码。
- 保留既有微信验证文件及服务器数据目录。本次未重新上传本地实验数据。

## 验证结果与边界

- 前端9项测试通过，生产构建通过；构建仍提示 Plotly 等资源体积较大，未作为本次界面发布的阻断项。
- 服务器 Linux 环境48项后端测试通过；存在一条 Starlette/httpx 弃用提示。
- 服务处于 active，Nginx 配置检查通过，最近服务日志未发现错误级别记录。
- 公网 HTTPS 健康检查通过；首页HTML、主要JS/CSS及档案场景资源与本机构建文件逐字节一致。
- 实验列表10条可见记录，抽样报告包含配对数据；主时钟与本地时钟波形均返回曲线。
- A/B双板WAV分段请求返回206且具有RIFF文件头；CSV导出及微信验证文件访问成功。
- 浏览器已加载新版首页，并显示档案编号、实验摘要和“进入分析 ACCESS FILE”入口；随后截图工具超时，未保存本次线上截图，也未完成额外的分析页交互复核。
- 未执行付费AI调用、上传或删除实验。网页检查不构成新的硬件验收结论。

## 必要时回退

以下命令仅用于需要回退时在服务器执行，本次未执行。旧版本与新版本使用相同的持久化数据和配置路径；本次没有数据库结构迁移。代码回退无需恢复数据库，不应为了回退界面覆盖发布后的新数据。

```sh
sudo systemctl stop ranging-studio
sudo ln -sfn /opt/ranging-studio/releases/20261025-admin /opt/ranging-studio/rollback-next
sudo mv -Tf /opt/ranging-studio/rollback-next /opt/ranging-studio/current
sudo systemctl start ranging-studio
curl -fsS http://127.0.0.1:8000/api/health
sudo systemctl is-active ranging-studio
```

回退后再检查公网首页、实验列表及波形。备份恢复属于单独的数据恢复操作，需要先保护发布后新增数据。
