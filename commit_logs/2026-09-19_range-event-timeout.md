# DIAG3 新事件/结果被错误判超时修复

## 现场证据

DIAG2 照片中两板 EVT=4，A EQ=593、B EQ=555，两板 O=0。
B TX=0、ACK=0，A RX=0。检测已产生事件，但尚未尝试发送。
B 另有 G=2，属于累计音频异常，不能仅凭照片确定其发生时间。

## 原因与修复

`AppRange_Process()` 入口缓存 `now`，随后调用 `AudioProcess()`。
检测成功后 `DetectionReady()` 用较新的 HAL tick 设置 `eventStart`。
当检测期间 tick 前进时，`now-eventStart` 的无符号减法下溢，被误认为超过
1000 ms，清除 `pendingEventId`，因此 EVT 增长却没有任何 TX 尝试。

A 的 `PairEvents()` / `DisplayResult()` 同样可能生成比入口 now 更晚的
`resultStart` / `lastResultMs`，导致新结果取消发送或 valid 被提前清除。

修复为在音频处理结束、配对结束以及最终有效期检查之前刷新 now。
保留原重试、超时和距离限制，保留 DIAG2 的吞吐改进与宽松检测参数。
屏幕标题为 RANGING DIAG3。

## 验证

主机测试让 HAL tick 在单次处理调用内部前进，覆盖真实模板检测到事件发送、
A 配对到结果发送及有效标记，并覆盖毫秒计数回绕。
修复前 A/B 分别复现新结果/新事件在发送前过期；修复后 A/B 全部通过。
原主机时钟在函数调用内保持不变，未覆盖此条件。
两角色 IAR 编译链接均为 0 错误、0 警告；构建后恢复原配置文件。

固件输出：`EWARM/ranging_bsp/Exe/ranging_diag3_A.hex/.out`、
`ranging_diag3_B.hex/.out`。两板均须更新，避免 A 保留结果发送超时问题。
硬件验收先确认 B TX/ACK、A RX 增长，再看 R 和距离；不宣称已完成现场精度验证。
