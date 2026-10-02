"""Audio CLI regression: formats, schedules, firmware templates and bad inputs."""

from array import array
from contextlib import redirect_stdout
import io
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[2]
AUDIO = ROOT / "tools/audio"
sys.path.insert(0, str(AUDIO))
import generate
from export_template import export_template
from waveforms import audio_name, chirp_group


def read_wav(path):
    with wave.open(str(path), "rb") as wav:
        metadata = (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes())
        pcm = array("h", wav.readframes(wav.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    return metadata, pcm


def template_arrays(path):
    text = path.read_text(encoding="utf-8")
    return [[int(value) for value in re.findall(r"-?\d+", body)]
            for body in re.findall(r"range(?:Up|Down)\[\d+\] = \{(.*?)\};", text, re.S)]


class AudioToolsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="ranging-audio-tests-")
        cls.output = Path(cls.temporary.name)
        with redirect_stdout(io.StringIO()):
            generate.main(["all", "--regression", "--output-dir", str(cls.output)])

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_default_formats_and_schedules(self):
        self.assertEqual(len(list(self.output.glob("*.wav"))), 5)
        for rate in (48000,):
            for profile, amplitude in (("standard", 0.9), ("wide", 0.75)):
                filename = audio_name(f"{profile}_repeat", rate, rate * 31 // 2)
                metadata, pcm = read_wav(self.output / filename)
                self.assertEqual(metadata, (rate, 1, 2, rate * 31 // 2))
                group = chirp_group(profile, rate, amplitude)
                for start in (0, 4 * rate, 8 * rate):
                    self.assertEqual(pcm[start:start + len(group)], group)
                self.assertFalse(any(pcm[len(group):4 * rate]))
                self.assertFalse(any(pcm[8 * rate + len(group):]))

    def test_firmware_templates_match_exactly(self):
        for rate in (48000,):
            for profile in ("standard", "wide"):
                mode, frames = ("group", rate * 5 // 2) if profile == "standard" else ("repeat", rate * 31 // 2)
                source = self.output / audio_name(f"{profile}_{mode}", rate, frames)
                exported = self.output / f"{profile}_{rate}.h"
                export_template(source, exported, profile, rate)
                suffix = "_48k"
                firmware = ROOT / f"Core/Inc/range_template{'_wide' if profile == 'wide' else ''}{suffix}.h"
                self.assertEqual(template_arrays(exported), template_arrays(firmware))

    def test_position_and_sine(self):
        metadata, pcm = read_wav(self.output / "position_48000hz_30s.wav")
        self.assertEqual(metadata, (48000, 1, 2, 1440000))
        period = pcm[:24000]
        self.assertFalse(any(period[576:]))
        self.assertGreater(max(period), 10000)
        for index in range(1, 60):
            self.assertEqual(pcm[index * 24000:(index + 1) * 24000], period)
        metadata, pcm = read_wav(self.output / "sine_500hz_48000hz_60s.wav")
        self.assertEqual(metadata, (48000, 1, 2, 2880000))
        # 500 Hz at 48 kHz is a 96-sample cycle; verify chunk boundaries too.
        for start in (96, 4096, 48000, len(pcm) - 96):
            self.assertLessEqual(max(abs(pcm[start + i] - pcm[(start + i) % 96])
                                     for i in range(96)), 1)

    def test_parameter_injection_and_output_path(self):
        custom = self.output / "custom.wav"
        with redirect_stdout(io.StringIO()):
            generate.main(["standard", "--sample-rate", "48000", "--rounds", "2",
                           "--signatures", "2", "--interval", "0.6", "--gap-seconds", "0.2",
                           "--tail-seconds", "0.3", "--amplitude", "0.4", "--output", str(custom)])
        metadata, pcm = read_wav(custom)
        self.assertEqual(metadata, (48000, 1, 2, 139200))
        self.assertLessEqual(max(map(abs, pcm)), round(0.4 * 32767))
        with redirect_stdout(io.StringIO()):
            generate.main(["position", "--duration", "0.025", "--interval", "0.02",
                           "--output-dir", str(self.output / "nested")])
        self.assertEqual(read_wav(self.output / "nested/position_48000hz_0p025s.wav")[0][-1], 1200)

    def test_invalid_inputs_do_not_create_files(self):
        cases = [["standard", "--sample-rate", "16000"], ["wide", "--sample-rate", "16000"], ["sine", "--frequency", "24000"], ["sine", "--duration", "nan"],
                 ["standard", "--rounds", "0"], ["standard", "--interval", "0.01"],
                 ["position", "--sample-rate", "16000"], ["position", "--interval", "0.001"],
                 ["wide", "--amplitude", "1.1"], ["sine", "--output", str(self.output / "invalid.py")]]
        with tempfile.TemporaryDirectory(prefix="ranging-audio-invalid-") as temporary:
            for arguments in cases:
                result = subprocess.run([sys.executable, str(AUDIO / "generate.py"), *arguments,
                                         "--output-dir", temporary], capture_output=True)
                self.assertNotEqual(result.returncode, 0, arguments)
                self.assertEqual(list(Path(temporary).iterdir()), [])
        self.assertFalse((self.output / "invalid.py").exists())

    def test_import_is_side_effect_free(self):
        with tempfile.TemporaryDirectory(prefix="ranging-audio-import-") as temporary:
            code = (f"import sys; sys.path.insert(0, {str(AUDIO)!r}); "
                    "import generate, export_template, waveforms")
            subprocess.run([sys.executable, "-B", "-c", code], cwd=temporary, check=True)
            self.assertEqual(list(Path(temporary).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
