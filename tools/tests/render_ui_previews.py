"""Render previews from run_scope_tests.ps1 (actual LCD code, simulated data)."""
from pathlib import Path
import tempfile
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[2]
out = root / "commit_logs/assets/2026-09-29-touch-ui"
out.mkdir(parents=True, exist_ok=True)
canvas = Image.new("RGB", (1000, 920), "#101820")
draw = ImageDraw.Draw(canvas)
draw.text((12, 8), "HOST RENDER / SIMULATED DATA / 480 x 272 PER SCREEN", fill="white")
for index, name in enumerate(("standard", "diagnostics", "clap", "wave", "position")):
    source = Path(tempfile.gettempdir()) / "ranging-host-tests" / f"scope1-{name}.ppm"
    with Image.open(source) as screen:
        screen.save(out / f"{name}.png")
        x, y = 10 + index % 2 * 500, 44 + index // 2 * 300
        canvas.paste(screen, (x, y))
        draw.text((x, y - 16), name, fill="white")
canvas.save(out / "overview.png")
print(out / "overview.png")
