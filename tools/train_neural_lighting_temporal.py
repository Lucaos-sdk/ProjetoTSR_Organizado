"""Equal-budget continuation control versus paired-view temporal supervision."""
import copy
import json
from pathlib import Path
import sys
import time
import numpy as np
import torch
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'_IA_Python'))
from bilateral_experiment import tensors
from neural_lighting_scene import render,correspondences,TRAIN_IDS
from neural_lighting_model import LightingNet,predict_lighting,lighting_loss
from neural_lighting_temporal import temporal_error_loss
from train_neural_lighting import evaluate,digest_file

FRESH_IDS=tuple(range(9000,9012))


def main():
    out=ROOT/'artifacts/neural-lighting-temporal-v1'
    out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    base=LightingNet()
    checkpoint=ROOT/'artifacts/neural-lighting-v1/spatial.pt'
    base.load_state_dict(torch.load(checkpoint,map_location='cpu',weights_only=True))
    models={'frozen':base,'continued':copy.deepcopy(base),'temporal':copy.deepcopy(base)}
    protocol=dict(base_sha256=digest_file(checkpoint),steps=600,learning_rate=.0005,seed=817,
        temporal_weight=.5,train_scenes=list(TRAIN_IDS),evaluation_scenes=list(FRESH_IDS),
        pairs=[[-.10,-.05],[0.,.05],[.05,.10]],batch='two pairs, four frames',
        comparison='continued and temporal start from identical checkpoint and receive identical pairs/steps',
        selection='fixed final step, no test-based checkpoint selection',
        gates=dict(temporal_reduction_vs_continued=.20,max_clean_mae_regression=.05,max_shadow_mae_regression=.05),
        limitation='Follow-up motivated by first experiment; new evaluation scene IDs; no game or GPU test')
    (out/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n',encoding='utf-8')
    pairs=[]
    keys=('features','source','target','valid','shadow_effect')
    for seed in TRAIN_IDS:
        for ya,yb in protocol['pairs']:
            a,_=render(seed,ya,depth_noise=.0005); b,cam=render(seed,yb,depth_noise=.0005)
            iy,ix,visible=correspondences(a,b,cam)
            pairs.append((tensors({k:a[k] for k in keys}),tensors({k:b[k] for k in keys}),
                torch.from_numpy((iy*ix.shape[1]+ix).copy())[None],torch.from_numpy(visible.astype(np.float32))[None,None]))
    training={}
    for name in ('continued','temporal'):
        model=models[name]; optimizer=torch.optim.Adam(model.parameters(),lr=.0005)
        rng=np.random.default_rng(817); start=time.perf_counter(); curve=[]
        for step in range(1,601):
            chosen=[pairs[i] for i in rng.choice(len(pairs),2,replace=False)]
            a={k:torch.cat([p[0][k] for p in chosen]) for k in keys}
            b={k:torch.cat([p[1][k] for p in chosen]) for k in keys}
            index=torch.cat([p[2] for p in chosen]); visible=torch.cat([p[3] for p in chosen])
            optimizer.zero_grad(set_to_none=True)
            pa,pb=predict_lighting(model,a),predict_lighting(model,b)
            loss=.5*(lighting_loss(pa,a)+lighting_loss(pb,b))
            tl=temporal_error_loss(pa,a['target'],pb,b['target'],index,visible)
            if name=='temporal': loss=loss+.5*tl
            if not torch.isfinite(loss): raise RuntimeError('Nonfinite loss')
            loss.backward(); optimizer.step()
            if step%200==0:
                curve.append(dict(step=step,loss=float(loss.detach()),temporal=float(tl.detach())))
                print(name,step,curve[-1],flush=True)
        model.eval(); path=out/(name+'.pt'); torch.save(model.state_dict(),path)
        training[name]=dict(cpu_train_seconds=time.perf_counter()-start,curve=curve,sha256=digest_file(path))
    evaluations=evaluate(models,torch.ones(1,3,1,1),FRESH_IDS)
    motion={n:[] for n in models}
    with torch.inference_mode():
        for seed in FRESH_IDS:
            frames=[render(seed,yaw) for yaw in (-.08,-.04,0.,.04,.08)]
            for i in range(4):
                a,_=frames[i]; b,cam=frames[i+1]
                iy,ix,visible=correspondences(a,b,cam)
                ba,bb=tensors(a),tensors(b)
                index=torch.from_numpy((iy*ix.shape[1]+ix).copy())[None]
                mask=torch.from_numpy(visible.astype(np.float32))[None,None]
                for name,model in models.items():
                    loss=temporal_error_loss(predict_lighting(model,ba),ba['target'],predict_lighting(model,bb),bb['target'],index,mask)
                    motion[name].append(dict(seed=seed,pair=i,error_delta=float(loss),samples=int(visible.sum())))
    temporal={n:dict(mean=float(np.mean([v['error_delta'] for v in rows])),pairs=rows) for n,rows in motion.items()}
    c=evaluations['continued']['clean']['mean']; t=evaluations['temporal']['clean']['mean']
    gates=dict(temporal_improves_20pct=temporal['temporal']['mean']<=.8*temporal['continued']['mean'],
        color_not_worse_5pct=t['mae']<=1.05*c['mae'],shadow_not_worse_5pct=t['shadow_mae']<=1.05*c['shadow_mae'],
        protected_exact=t['protected_max_change']==0)
    result=dict(protocol=protocol,training=training,held_out=evaluations,temporal=temporal,gates=gates,
        synthetic_gates_passed=all(gates.values()),game_integration_allowed=False,
        limitations=['Camera rotation/translation on three-sphere scenes only; no moving objects or learned materials.',
                     'Nearest correspondence samples have residual subpixel error; measure errors relative to each reference.',
                     'No inference history: temporal pairs supervise single-frame network weights only.',
                     'Cost, spatial artifacts, broader scene generalization and game input contract remain unvalidated.'])
    (out/'metrics.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    canvas=Image.new('RGB',(96*5,84*3),'#17202a'); draw=ImageDraw.Draw(canvas)
    with torch.inference_mode():
        for row,seed in enumerate(FRESH_IDS[:3]):
            b=tensors(render(seed)[0])
            values=[b['source'],b['target']]+[predict_lighting(models[n],b) for n in ('frozen','continued','temporal')]
            for col,(label,value) in enumerate(zip(('Entrada','Referencia','IA inicial','Mais treino','IA temporal'),values)):
                rgb=value[0].permute(1,2,0).numpy().clip(0); rgb=(rgb/(1+rgb))**(1/2.2)
                canvas.paste(Image.fromarray(np.uint8(np.round(rgb*255))),(col*96,row*84+20))
                draw.text((col*96+2,row*84+3),label,fill='white')
    canvas.resize((960,504)).save(out/'comparison.png')
    files=['_IA_Python/neural_lighting_scene.py','_IA_Python/neural_lighting_model.py','_IA_Python/neural_lighting_temporal.py',
           'tools/train_neural_lighting.py','tools/train_neural_lighting_temporal.py','tests/test_neural_lighting_temporal.py']
    (out/'source-manifest.json').write_text(json.dumps({f:digest_file(ROOT/f) for f in files},indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(clean={n:v['clean']['mean'] for n,v in evaluations.items()},temporal={n:v['mean'] for n,v in temporal.items()},gates=gates),indent=2),flush=True)


if __name__=='__main__': main()
