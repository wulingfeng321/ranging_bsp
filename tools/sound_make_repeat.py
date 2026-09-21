"""将 sound_make.py 生成的完整音频重复多轮，用于重复测距。


默认：每轮保留原有 2.5 秒音频（5 个签名），播放 15 轮，轮间额外
插入 1.5 秒静音，总长 37.5 秒。原音频自身的尾部静音也完整保留。

用法：
    python tools/sound_make_repeat.py
    python tools/sound_make_repeat.py --repeat 5 --gap-seconds 3
    python tools/sound_make_repeat.py --output my_measurements.wav

依赖与原脚本相同：numpy、scipy、soundfile。
本脚本在临时目录运行原生成器，复用其参数和波形，不覆盖原 WAV。
重复采集可供测距程序做统计；异常剔除、平均等处理仍由程序实现。
"""

import argparse
import math
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf


DEFAULT_REPEAT = 15
DEFAULT_GAP_SECONDS = 1.5
TOOLS_DIR = Path(__file__).resolve().parent


def positive_count(value):
    count = int(value)
    if count < 1:
        raise argparse.ArgumentTypeError("repeat must be at least 1")
    return count


def nonnegative_seconds(value):
    seconds = float(value)
    if not math.isfinite(seconds) or seconds < 0:
        raise argparse.ArgumentTypeError("gap-seconds must be finite and >= 0")
    return seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repeat", type=positive_count, default=DEFAULT_REPEAT)
    parser.add_argument("--gap-seconds", type=nonnegative_seconds,
                        default=DEFAULT_GAP_SECONDS)
    parser.add_argument("--output", type=Path,
                        default=TOOLS_DIR / "test_audio_chirp_repeat.wav")
    args = parser.parse_args()

    output = args.output.resolve()
    if output in {(TOOLS_DIR / "test_audio_chirp.wav").resolve(),
                  (TOOLS_DIR / "sound_make.py").resolve(),
                  Path(__file__).resolve()}:
        parser.error("output must not overwrite the original audio or scripts")
    if not output.parent.is_dir():
        parser.error("output directory does not exist")

    # 原生成器在模块顶层写文件；使用独立进程和临时工作目录隔离该行为。
    with tempfile.TemporaryDirectory(prefix="ranging_audio_") as temp_dir:
        subprocess.run([sys.executable, str(TOOLS_DIR / "sound_make.py")],
                       cwd=temp_dir, check=True, stdout=subprocess.PIPE)
        source = Path(temp_dir) / "test_audio_chirp.wav"
        info = sf.info(source)
        if info.channels != 1 or info.subtype != "PCM_16":
            raise ValueError("Expected mono PCM_16 output from sound_make.py")
        audio, sample_rate = sf.read(source, dtype="int16")

    gap_samples = round(sample_rate * args.gap_seconds)
    silence = np.zeros(gap_samples, dtype=np.int16)
    # 流式逐轮写出，不为所有重复音频分配一个大数组；末轮不额外加间隔。
    with sf.SoundFile(output, mode="w", samplerate=sample_rate, channels=1,
                      subtype="PCM_16", format="WAV") as wav:
        for index in range(args.repeat):
            wav.write(audio)
            if index + 1 < args.repeat:
                wav.write(silence)

    total_samples = len(audio) * args.repeat + gap_samples * (args.repeat - 1)
    starts = [(len(audio) + gap_samples) * i / sample_rate
              for i in range(args.repeat)]
    print(f"Saved: {output}")
    print(f"Format: {sample_rate} Hz, mono, PCM_16")
    print(f"Rounds: {args.repeat}; source duration: {len(audio)/sample_rate:.3f} s")
    print(f"Extra gap between rounds: {gap_samples/sample_rate:.3f} s")
    print("Round starts (s): " + ", ".join(f"{t:.3f}" for t in starts))
    print(f"Total: {total_samples} samples, {total_samples/sample_rate:.3f} s")


if __name__ == "__main__":
    main()
