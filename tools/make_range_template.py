"""Extract the exact development WAV chirps, without third-party packages."""
from pathlib import Path
import wave, struct, hashlib, argparse
parser=argparse.ArgumentParser()
parser.add_argument("--sample-rate",type=int,choices=(16000,48000),default=48000)
rate=parser.parse_args().sample_rate
scale=rate//16000
pulse=512*scale
step=640*scale
suffix="_48k" if rate==48000 else ""
root = Path(__file__).resolve().parents[1]
path = root / f'tools/test_audio_chirp{suffix}.wav'
with wave.open(str(path), 'rb') as w:
    assert (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (rate, 1, 2)
    pcm = struct.unpack('<' + 'h'*w.getnframes(), w.readframes(w.getnframes()))
parts = [pcm[:pulse], pcm[step:step+pulse]]
out = [f'/* Generated from tools/test_audio_chirp{suffix}.wav; do not edit.',
       ' * SHA256: '+hashlib.sha256(path.read_bytes()).hexdigest()+' */',
       '#ifndef RANGE_TEMPLATE_H', '#define RANGE_TEMPLATE_H', '#include <stdint.h>']
for name, samples in zip(('Up', 'Down'), parts):
    mean = sum(samples)/len(samples)
    values = [round(x-mean) for x in samples]
    out.append(f'#define RANGE_{name.upper()}_ENERGY {float(sum(x*x for x in values)):.1f}f')
    out.append(f'#define RANGE_{name.upper()}_COARSE_ENERGY {float(sum(x*x for x in values[::scale])):.1f}f')
    out.append(f'static const int16_t range{name}[{pulse}] = {{')
    out.extend('  '+', '.join(map(str,values[i:i+16]))+',' for i in range(0,pulse,16))
    out.append('};')
out.append('#endif')
(root/f'Core/Inc/range_template{suffix}.h').write_text('\n'.join(out)+'\n', encoding='utf-8')
print(f'Generated range_template{suffix}.h')
