import wave
import math
import struct
from pathlib import Path


# ===================== 参数设置 =====================

SAMPLE_RATE = 48000      # 采样率：48 kHz
BIT_DEPTH = 16           # 位深：16 bit
CHANNELS = 1             # 声道数：1 = 单声道

FREQUENCY = 500          # 正弦波频率：500 Hz
DURATION = 60.0          # 音频时长：秒
AMPLITUDE = 0.8          # 振幅：0.0 ~ 1.0

OUTPUT_FILENAME = "500Hz.wav"

# ==================================================


def generate_wav():
    # 脚本所在目录
    script_dir = Path(__file__).resolve().parent
    output_path = script_dir / OUTPUT_FILENAME

    # 16 bit PCM 每个采样点占 2 字节
    sample_width = BIT_DEPTH // 8

    # 总采样点数
    total_samples = int(SAMPLE_RATE * DURATION)

    # 16 位有符号 PCM 最大幅值
    max_pcm_value = (2 ** (BIT_DEPTH - 1)) - 1

    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(CHANNELS)
        wav_file.setsampwidth(sample_width)
        wav_file.setframerate(SAMPLE_RATE)

        # 分块生成，避免长时间音频占用大量内存
        buffer = bytearray()

        for n in range(total_samples):
            sample = (
                AMPLITUDE
                * max_pcm_value
                * math.sin(2 * math.pi * FREQUENCY * n / SAMPLE_RATE)
            )

            # 16位有符号小端 PCM
            buffer.extend(struct.pack("<h", int(sample)))

            # 每4096个采样点写一次文件
            if len(buffer) >= 4096 * sample_width:
                wav_file.writeframesraw(buffer)
                buffer.clear()

        # 写入最后剩余的数据
        if buffer:
            wav_file.writeframesraw(buffer)

    print(f"WAV 生成完成：{output_path}")


if __name__ == "__main__":
    generate_wav()