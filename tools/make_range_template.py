"""Extract the exact development WAV chirps, without third-party packages."""
from pathlib import Path
import wave, struct, hashlib
root = Path(__file__).resolve().parents[1]
path = root / 'tools/test_audio_chirp.wav'
with wave.open(str(path), 'rb') as w:
    assert (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (16000, 1, 2)
    pcm = struct.unpack('<' + 'h'*w.getnframes(), w.readframes(w.getnframes()))
parts = [pcm[:512], pcm[640:1152]]
out = ['/* Generated from tools/test_audio_chirp.wav; do not edit.',
       ' * SHA256: '+hashlib.sha256(path.read_bytes()).hexdigest()+' */',
       '#ifndef RANGE_TEMPLATE_H', '#define RANGE_TEMPLATE_H', '#include <stdint.h>']
for name, samples in zip(('Up', 'Down'), parts):
    mean = sum(samples)/len(samples)
    values = [round(x-mean) for x in samples]
    out.append(f'#define RANGE_{name.upper()}_ENERGY {float(sum(x*x for x in values)):.1f}f')
    out.append(f'static const int16_t range{name}[512] = {{')
    out.extend('  '+', '.join(map(str,values[i:i+16]))+',' for i in range(0,512,16))
    out.append('};')
out.append('#endif')
(root/'Core/Inc/range_template.h').write_text('\n'.join(out)+'\n', encoding='utf-8')
print('Generated range_template.h')
