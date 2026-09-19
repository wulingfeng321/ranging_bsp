# BOARD RANGE TEST1：共线外侧声源测试

用户确认声源与两板共线，且在两板连线外侧。在该几何条件下，
两板声波到达时间差乘声速可估计两个 L 麦克风开孔之间的距离。
程序无法从这两个接收时刻自动验证声源摆放条件。

## 界面

- 标题 BOARD RANGE TEST1；距离标注 A(L) - B(L) DISTANCE。
- SOURCE: A SIDE 和 SOUND >>> A --- B 表示声源位于 A 外侧。
- SOURCE: B SIDE 和 A --- B <<< SOUND 表示声源位于 B 外侧。
- SOURCE: UNKNOWN 表示方向不确定，不强行选择侧别。
- A 显示 RAW B-A，单位 us，保存的是当前有效结果对应的原始时间差，
  不是诊断 DT 中可能属于不同事件的候选比较值。该字段不传到 B。
- 有效结果保留 15 秒；断线、失锁、音频错误仍清除。诊断计数保留。
- 顶部不再显示 WAVE HOLD 状态文字；原波形保持逻辑保留。

## 测试步骤

分别烧录 `EWARM/ranging_bsp/Exe/ranging_board_test1_A.hex/.out`
和 `ranging_board_test1_B.hex/.out`，确认角色和标题，等待两板 READY。
将两个 L 麦克风开孔相距 0.5 m，声源位于近板外侧约 0.5 至 1 m。
先 SOUND--A--B，记录距离、SOURCE、A 的 RAW B-A 和 Q。
保持两板位置不变，再 A--B--SOUND，记录同样的数据，重复几轮。
此阶段没有依据实测数据修改 APP_RANGE_BIAS_NS；读数为未完成现场标定的估计值。

## 验证

A/B 主机状态机测试通过，保留吞吐与处理期间 tick 前进/回绕回归。
A/B IAR 编译链接均 0 错误、0 警告，角色配置文件恢复构建前内容。
尚未进行实物界面和距离精度验证。
