import numpy as np
from scipy import signal
import soundfile as sf

# ================= 3.1 基础参数 =================
FS = 16000                # 采样率 16 kHz
DURATION_MS = 32          # 单段 chirp 32 ms
DURATION_SAMPLES = int(FS * DURATION_MS / 1000)  # 512 点
F_UP_START, F_UP_END = 2000, 6000   # 上扫频 2k -> 6k
F_DOWN_START, F_DOWN_END = 6000, 2000 # 下扫频 6k -> 2k
FADE_MS = 1               # 淡入淡出 1 ms
FADE_SAMPLES = int(FS * FADE_MS / 1000) # 16 点
GAP_MS = 8                # 签名内静音 8 ms
GAP_SAMPLES = int(FS * GAP_MS / 1000)   # 128 点

# ================= 3.2 签名与重复周期 =================
PERIOD_MS = 500           # 两个签名起点之间相隔 500 ms
PERIOD_SAMPLES = int(FS * PERIOD_MS / 1000) # 8000 点
SILENCE_BETWEEN_SIG_MS = 388 # 相邻签名之间的静音 388 ms
SILENCE_BETWEEN_SIG_SAMPLES = int(FS * SILENCE_BETWEEN_SIG_MS / 1000) # 6208 点
NUM_SIGNATURES = 5        # 每轮播放 5 个签名

# 生成时间轴 (endpoint=False 确保正好是 512 个点)
t = np.linspace(0, DURATION_MS / 1000, DURATION_SAMPLES, endpoint=False)

# ================= 1. 生成 Chirp 信号 =================
# 使用 scipy.signal.chirp 生成，默认 phi=0 (相位从 0 开始，符合固定相位约定)
up_chirp = signal.chirp(t, f0=F_UP_START, t1=DURATION_MS/1000, f1=F_UP_END, method='linear')
down_chirp = signal.chirp(t, f0=F_DOWN_START, t1=DURATION_MS/1000, f1=F_DOWN_END, method='linear')

# ================= 2. 生成 1ms 半 Hann 包络 =================
# 生成半 Hann 窗 (16个点)
half_hann = np.hanning(2 * FADE_SAMPLES) 
fade_in = half_hann[:FADE_SAMPLES]   # 上升沿 0 -> 1
fade_out = half_hann[FADE_SAMPLES:]  # 下降沿 1 -> 0

# 构造包络：两端 1ms 渐变，中间保持恒定 (不加全段 Hann 窗)
envelope = np.ones(DURATION_SAMPLES)
envelope[:FADE_SAMPLES] = fade_in
envelope[-FADE_SAMPLES:] = fade_out

# 将包络应用到 Chirp 信号上 (固定幅度)
up_chirp = up_chirp * envelope
down_chirp = down_chirp * envelope

# ================= 3. 构建单个签名 (112ms / 1792点) =================
# 结构: Up 32ms + 静音 8ms + Down 32ms + 静音 8ms + Up 32ms
silence_8ms = np.zeros(GAP_SAMPLES)
signature = np.concatenate([
    up_chirp,
    silence_8ms,
    down_chirp,
    silence_8ms,
    up_chirp
])
# 验证签名长度 (应为 1792 点)
assert len(signature) == 1792, f"签名长度错误: {len(signature)}"

# ================= 4. 构建重复周期 (500ms / 8000点) =================
silence_388ms = np.zeros(SILENCE_BETWEEN_SIG_SAMPLES)
period = np.concatenate([signature, silence_388ms])
assert len(period) == PERIOD_SAMPLES, f"周期长度错误: {len(period)}"

# ================= 5. 生成完整测量音频 =================
# 每轮测量播放 5 个签名，起点为 0, 500, 1000, 1500, 2000 ms
full_audio = np.tile(period, NUM_SIGNATURES)

# 根据总时长 2000ms + 112ms = 2112ms，可选择性补充尾部静音到 2500ms
total_samples = int(FS * 2.5) # 2500ms
if len(full_audio) < total_samples:
    full_audio = np.pad(full_audio, (0, total_samples - len(full_audio)), 'constant')

# ================= 6. 导出为 16-bit PCM 单声道 WAV =================
# 归一化到 int16 范围 (-32768 到 32767)，保持固定幅度
full_audio_int16 = (full_audio * 32767 * 0.9).astype(np.int16) # 乘 0.9 留一点余量防削波

sf.write('test_audio_chirp.wav', full_audio_int16, FS, subtype='PCM_16')
print("音频生成完毕，保存为 test_audio_chirp.wav")