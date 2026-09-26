"""Generate a 2.5 s legacy chirp group, preserving physical durations."""
import argparse
from pathlib import Path
import numpy as np
from scipy import signal
import soundfile as sf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sample-rate', type=int, choices=(16000, 48000), default=48000)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    fs = args.sample_rate
    pulse, gap, fade = fs * 32 // 1000, fs * 8 // 1000, fs // 1000
    t = np.arange(pulse) / fs
    envelope = np.ones(pulse)
    hann = np.hanning(2 * fade)
    envelope[:fade], envelope[-fade:] = hann[:fade], hann[fade:]
    up = signal.chirp(t, f0=2000, t1=.032, f1=6000) * envelope
    down = signal.chirp(t, f0=6000, t1=.032, f1=2000) * envelope
    signature = np.concatenate((up, np.zeros(gap), down, np.zeros(gap), up))
    period = np.zeros(fs // 2)
    period[:len(signature)] = signature
    pcm = (np.tile(period, 5) * 32767 * .9).astype(np.int16)
    output = args.output or Path('test_audio_chirp_48k.wav' if fs == 48000 else 'test_audio_chirp.wav')
    sf.write(output, pcm, fs, subtype='PCM_16')
    print(f'Saved {output}: {fs} Hz, 2.5 s, 5 signatures')


if __name__ == '__main__':
    main()
