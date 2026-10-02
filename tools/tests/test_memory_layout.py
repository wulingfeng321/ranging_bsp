"""Compile the real SDRAM layout and ensure invalid allocations fail (MSVC environment)."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "Core/Inc/board_memory.h").read_text(encoding="utf-8")
CASES = [
    ("valid", None, None, None),
    ("lcd_overlap", "BOARD_LCD_FRAME_B", "0xC007F000U", "overlap"),
    ("capture_overlap", "BOARD_CAPTURE_BLOCKS", "1200U", "overlap"),
    ("outside_sdram", "BOARD_SD_BUFFER_BASE", "0xC0800000U", "exceed"),
    ("misaligned_dma", "BOARD_AUDIO_DMA_BASE", "0xC0100002U", "aligned"),
    ("wrong_sector", "BOARD_SD_BUFFER_BYTES", "1024U", "compatible"),
]

def main():
    with tempfile.TemporaryDirectory(prefix="ranging-layout-") as directory:
        work = Path(directory)
        (work / "check.c").write_text('#include "board_memory.h"\nint layout_check;\n')
        for name, symbol, value, diagnostic in CASES:
            header = HEADER
            if symbol:
                header, count = re.subn(r"(?m)^(#define " + symbol + r"\s+)[^\n]+$",
                                       lambda match: match[1] + value, header)
                assert count == 1, symbol
            (work / "board_memory.h").write_text(header)
            result = subprocess.run(["cl", "/nologo", "/utf-8", "/c", "/I" + str(ROOT / "Core/Inc"), "check.c"],
                                    cwd=work, capture_output=True)
            output = (result.stdout + result.stderr).decode(errors="replace")
            if diagnostic:
                assert result.returncode != 0 and diagnostic in output, (name, output)
            else:
                assert result.returncode == 0, output
    print("PASS: SDRAM layout compiles; overlap, bounds, alignment and format violations rejected")

if __name__ == "__main__":
    main()
