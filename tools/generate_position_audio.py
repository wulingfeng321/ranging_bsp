"""Generate the dedicated POSITION chirp (standard library only).
Usage: python tools/generate_position_audio.py [--output tools/position_48k.wav] [--seconds 30]
48 kHz / mono / signed 16-bit PCM; 12 ms 2-6 kHz Hann chirp every 500 ms.
"""
import argparse, math, struct, wave
from pathlib import Path

def tone(t):
    if not 0 <= t < .012: return 0.0
    return (.5-.5*math.cos(2*math.pi*t/.012))*math.sin(2*math.pi*(2000*t+.5*(4000/.012)*t*t))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('position_48k.wav'))
    parser.add_argument('--seconds', type=int, default=30)
    args=parser.parse_args()
    if not 1 <= args.seconds <= 600: parser.error('seconds must be 1..600')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    samples=[round(0.65*32767*tone(i/48000)) for i in range(24000)]
    block=struct.pack('<'+'h'*len(samples),*samples)
    with wave.open(str(args.output),'wb') as out:
        out.setparams((1,2,48000,0,'NONE','not compressed'))
        for _ in range(args.seconds*2): out.writeframesraw(block)
    print(args.output.resolve())
if __name__=='__main__': main()
