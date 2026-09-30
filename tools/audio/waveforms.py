"""Pure PCM builders shared by the audio CLI; Python standard library only."""

from array import array
import math
from pathlib import Path
import sys
import wave

GENERATED_DIR = Path(__file__).resolve().parent / "generated"


def number_token(value):
    """Use a stable, filename-safe decimal without timestamps."""
    return format(value, ".12g").replace(".", "p")


def audio_name(kind, rate, frames, frequency=500):
    if kind == "sine":
        kind = f"sine_{number_token(frequency)}hz"
    return f"{kind}_{rate}hz_{number_token(frames / rate)}s.wav"


def chirp_pulses(profile, rate, amplitude):
    """Preserve the LEGACY/WIDE physical durations, phase and quantization."""
    size = rate * 32 // 1000
    fade = rate // 1000
    pulses = []
    frequencies = (2000, 6000) if profile == "standard" else (1500, 6500)
    for f0, f1 in (frequencies, frequencies[::-1]):
        pulse = array("h")
        for i in range(size):
            t = i / rate
            if profile == "standard":
                if i < fade:
                    envelope = 0.5 - 0.5 * math.cos(2 * math.pi * i / (2 * fade - 1))
                elif i >= size - fade:
                    envelope = 0.5 - 0.5 * math.cos(
                        2 * math.pi * (i - size + 2 * fade) / (2 * fade - 1)
                    )
                else:
                    envelope = 1.0
            else:
                edge = min(i, size - 1 - i)
                ramp = rate * 4 // 1000
                envelope = 0.5 - 0.5 * math.cos(math.pi * edge / ramp) if edge < ramp else 1.0
            phase = 2 * math.pi * (f0 * t + (f1 - f0) * t * t / (2 * size / rate))
            value = amplitude * 32767 * envelope * math.cos(phase)
            # LEGACY previously used numpy.astype(int16); WIDE used round().
            pulse.append(int(value) if profile == "standard" else round(value))
        pulses.append(pulse)
    return pulses


def chirp_group(profile, rate, amplitude, signatures=5, interval=0.5):
    up, down = chirp_pulses(profile, rate, amplitude)
    gap = array("h", [0]) * (rate * (8 if profile == "standard" else 4) // 1000)
    signature = up + gap + down + gap + up
    period_frames = round(rate * interval)
    if period_frames < len(signature):
        raise ValueError("interval must cover the entire three-pulse signature")
    period = signature + array("h", [0]) * (period_frames - len(signature))
    return period * signatures


def repeat_blocks(group, rate, rounds=3, gap_seconds=1.5, tail_seconds=5):
    gap = array("h", [0]) * round(rate * gap_seconds)
    for index in range(rounds):
        yield group
        if index + 1 < rounds:
            yield gap
    yield array("h", [0]) * round(rate * tail_seconds)


def position_blocks(rate, frames, amplitude=0.65, interval=0.5):
    period_frames = round(rate * interval)
    pulse_frames = round(rate * 0.012)
    if period_frames < pulse_frames:
        raise ValueError("interval must be at least 0.012 seconds")
    period = array("h")
    for i in range(pulse_frames):
        t = i / rate
        envelope = 0.5 - 0.5 * math.cos(2 * math.pi * t / 0.012)
        phase = 2 * math.pi * (2000 * t + 0.5 * (4000 / 0.012) * t * t)
        period.append(round(amplitude * 32767 * envelope * math.sin(phase)))
    period.extend(array("h", [0]) * (period_frames - pulse_frames))
    while frames:
        count = min(frames, period_frames)
        yield period[:count]
        frames -= count


def sine_blocks(rate, frames, frequency=500, amplitude=0.8):
    for start in range(0, frames, 4096):
        yield array("h", (
            int(amplitude * 32767 * math.sin(2 * math.pi * frequency * i / rate))
            for i in range(start, min(start + 4096, frames))
        ))


def write_wav(path, rate, blocks):
    """Write mono PCM16, in chunks; never write C headers as a side effect."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = 0
    with wave.open(str(path), "wb") as wav:
        wav.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        for block in blocks:
            if sys.byteorder != "little":
                block = array("h", block)
                block.byteswap()
            wav.writeframesraw(block.tobytes())
            frames += len(block)
    return frames
