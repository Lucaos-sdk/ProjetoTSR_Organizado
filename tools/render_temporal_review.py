"""Render actual harness BMP outputs; no image generation or simulated GPU output."""
import argparse
import csv
import json
from pathlib import Path
from statistics import mean
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
args = parser.parse_args()
directory = args.directory
font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 18) if Path("C:/Windows/Fonts/segoeui.ttf").exists() else ImageFont.load_default()
names = [("truth", "Referencia analitica"), ("raw_cpu", "Bilinear sem compensacao (CPU)"),
         ("stable_spatial_cpu", "Bilinear compensado (CPU)"), ("temporal_gpu", "Temporal compensado (GPU)")]
def panel(frame, columns=4):
    tile_w, tile_h = 400, 255
    rows = (len(names) + columns - 1) // columns
    canvas = Image.new("RGB", (tile_w * columns, rows * tile_h + 38), "#101822")
    draw = ImageDraw.Draw(canvas)
    draw.text((12, 7), f"Quadro {frame:02d} | dados sinteticos 96x54 -> 192x108 | zoom 2x", font=font, fill="white")
    for i, (name, label) in enumerate(names):
        x, y = i % columns * tile_w, 38 + i // columns * tile_h
        draw.text((x+8, y+3), label, font=font, fill="#c8d7e8")
        with Image.open(directory / f"{frame}_{name}.bmp") as source:
            canvas.paste(source.resize((384, 216), Image.Resampling.NEAREST), (x+8, y+32))
    return canvas

contact = Image.new("RGB", (1600, 293*3), "#101822")
for row, frame in enumerate((0, 7, 15)):
    contact.paste(panel(frame), (0, row*293))
contact.save(directory / "comparacao.png")
frames = [panel(i, 2) for i in range(16)]
frames[0].save(directory / "sequencia.gif", save_all=True, append_images=frames[1:], duration=180, loop=0, disposal=2)
with (directory / "metrics.csv").open(newline="") as f:
    rows = list(csv.DictReader(f))
summary = {}
for name, label in names[1:]:
    selected = [row for row in rows if row["method"] == name]
    summary[name] = {
        "frames": len(selected),
        "mean_mse_linear_rgb": mean(float(row["mse_linear_rgb"]) for row in selected),
        "mean_residual_delta_mse_excluding_first": mean(float(row["residual_delta_mse"]) for row in selected if int(row["frame"]) > 0),
    }
(directory / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
