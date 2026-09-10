"""Compare two actual GPU reconstruction exports on the same synthetic sequence."""
import argparse
import csv
import json
from pathlib import Path
from statistics import mean
from PIL import Image, ImageDraw, ImageFont

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("bilinear",type=Path)
parser.add_argument("cubic",type=Path)
parser.add_argument("output",type=Path)
parser.add_argument("--candidate-key",default="cubic")
parser.add_argument("--candidate-title",default="Temporal + cubico limitado (GPU)")
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
font=ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf",18) if Path("C:/Windows/Fonts/segoeui.ttf").exists() else ImageFont.load_default()
def frame(t):
    if (args.bilinear/f"{t}_truth.bmp").read_bytes() != (args.cubic/f"{t}_truth.bmp").read_bytes():
        raise ValueError("Reference frames differ; comparison is not paired")
    canvas=Image.new("RGB",(1200,290),"#101822")
    draw=ImageDraw.Draw(canvas)
    draw.text((8,5),f"Quadro {t:02d} | sequencia sintetica | GPU RX 7600 | zoom 2x",font=font,fill="white")
    for i,(directory,suffix,title) in enumerate(((args.bilinear,"truth","Referencia analitica"),
             (args.bilinear,"temporal_gpu","Temporal + bilinear (GPU)"),
             (args.cubic,"temporal_gpu",args.candidate_title))):
        draw.text((i*400+8,36),title,font=font,fill="#cedceb")
        with Image.open(directory/f"{t}_{suffix}.bmp") as image:
            canvas.paste(image.resize((384,216),Image.Resampling.NEAREST),(i*400+8,66))
    return canvas
images=[frame(i) for i in range(16)]
sheet=Image.new("RGB",(1200,870))
for i,t in enumerate((0,7,15)): sheet.paste(images[t],(0,i*290))
sheet.save(args.output/"comparacao.png")
images[0].save(args.output/"sequencia.gif",save_all=True,append_images=images[1:],duration=180,loop=0,disposal=2)
summary={}
for name,directory in (("bilinear",args.bilinear),(args.candidate_key,args.cubic)):
    with (directory/"metrics.csv").open(newline="") as stream:
        rows=[r for r in csv.DictReader(stream) if r["method"]=="temporal_gpu"]
    summary[name]={"frames":len(rows),"mean_mse":mean(float(r["mse_linear_rgb"]) for r in rows),
        "residual_delta_mse":mean(float(r["residual_delta_mse"]) for r in rows if int(r["frame"])>0)}
summary["mse_reduction_percent"]=100*(1-summary[args.candidate_key]["mean_mse"]/summary["bilinear"]["mean_mse"])
summary["residual_delta_reduction_percent"]=100*(1-summary[args.candidate_key]["residual_delta_mse"]/summary["bilinear"]["residual_delta_mse"])
(args.output/"summary.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
print(json.dumps(summary,indent=2))
