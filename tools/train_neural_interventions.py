"""Equal-budget varied-layout control versus counterfactual response supervision."""
import argparse
import copy
import hashlib
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
from neural_lighting_scene import correspondences
from neural_lighting_model import LightingNet,predict_lighting,lighting_loss
from neural_lighting_diagnostics import response_metrics
from neural_intervention_training import TRAIN_IDS,VALID_IDS,TEST_IDS,CASES,make_frame,masks,paired_loss


def infer(model,scene):
    if model is None: return scene['source']
    return predict_lighting(model,tensors(scene))[0].permute(1,2,0).numpy()


def response_summary(rows):
    changed=[r for r in rows if r['changed_pixels']>0]
    stable=[r for r in rows if r['unchanged_pixels']>0]
    def avg(rows,key,weight):
        return float(np.average([r[key] for r in rows],weights=[r[weight] for r in rows])) if rows else None
    return dict(changed_pixels=sum(r['changed_pixels'] for r in rows),
        unchanged_pixels=sum(r['unchanged_pixels'] for r in rows),
        response_mae=avg(changed,'response_mae','changed_pixels'),
        zero_response_mae=avg(changed,'zero_response_mae','changed_pixels'),
        unchanged_drift=avg(stable,'unchanged_residual_drift','unchanged_pixels'))


def evaluate(models,ids):
    results={n:{} for n in models}
    with torch.inference_mode():
        for noisy in (False,True):
            case_name='noisy' if noisy else 'clean'
            response={n:[] for n in models}; images={n:[] for n in models}
            for seed in ids:
                frames={case:make_frame(seed,case,yaw=.07,noise=.002 if noisy else 0.)[0] for case in ('base',)+CASES}
                preds={n:{c:infer(m,s) for c,s in frames.items()} for n,m in models.items()}
                for n in models:
                    for case,scene in frames.items():
                        pred=preds[n][case]; error=np.abs(pred-scene['target'])
                        protected=scene['valid']==0; shadow=scene['shadow_effect']>.005
                        images[n].append(dict(seed=seed,case=case,mae=float(error.mean()),
                            shadow_mae=float(error[shadow].mean()) if shadow.any() else None,
                            protected_change=float(np.abs(pred-scene['source'])[protected].max())))
                    for case in CASES:
                        row,*_=response_metrics(preds[n]['base'],preds[n][case],frames['base'],frames[case])
                        response[n].append(dict(seed=seed,case=case,**row))
            for n in models:
                results[n][case_name]=dict(response=response_summary(response[n]),
                    by_case={c:response_summary([r for r in response[n] if r['case']==c]) for c in CASES},
                    image_mae=float(np.mean([r['mae'] for r in images[n]])),
                    shadow_mae=float(np.mean([r['shadow_mae'] for r in images[n] if r['shadow_mae'] is not None])),
                    protected_max_change=max(r['protected_change'] for r in images[n]),pairs=response[n],images=images[n])
    return results


def temporal(models):
    result={n:{} for n in models}
    with torch.inference_mode():
        for noisy in (False,True):
            rows={n:[] for n in models}
            for seed in TEST_IDS:
                frames=[make_frame(seed,yaw=y,noise=.002 if noisy else 0.) for y in (-.08,-.04,0.,.04,.08)]
                errors={n:[infer(m,s)-s['target'] for s,_ in frames] for n,m in models.items()}
                for i in range(4):
                    iy,ix,mask=correspondences(frames[i][0],frames[i+1][0],frames[i+1][1])
                    for n in models:
                        value=float(np.abs(errors[n][i]-errors[n][i+1][iy,ix])[mask].mean())
                        rows[n].append(dict(seed=seed,pair=i,samples=int(mask.sum()),error=value))
            for n,rs in rows.items():
                result[n]['noisy' if noisy else 'clean']=dict(mean=float(np.average([r['error'] for r in rs],weights=[r['samples'] for r in rs])),pairs=rs)
    return result


def visual(models,out):
    labels=('Entrada','Referencia','IA anterior','Dados novos','Dados + pares')
    canvas=Image.new('RGB',(96*5,84*4),'#18212b'); draw=ImageDraw.Draw(canvas)
    with torch.inference_mode():
        for row,case in enumerate(('base',)+CASES):
            scene,_=make_frame(TEST_IDS[0],case,yaw=.07)
            values=[scene['source'],scene['target']]+[infer(models[n],scene) for n in ('old_fitted','control','paired')]
            for col,(label,value) in enumerate(zip(labels,values)):
                rgb=value.clip(0); rgb=(rgb/(1+rgb))**(1/2.2)
                canvas.paste(Image.fromarray(np.uint8(np.round(rgb*255))),(col*96,row*84+20))
                draw.text((col*96+2,row*84+3),label,fill='white')
    canvas.resize((960,672)).save(out/'comparison.png')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/neural-intervention-training-v1')
    parser.add_argument('--steps',type=int,default=1600)
    args=parser.parse_args()
    if args.steps<1: parser.error('steps must be positive')
    args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True); torch.manual_seed(20260909)
    initial=LightingNet(); old=LightingNet()
    ckpt=ROOT/'artifacts/neural-lighting-v1/spatial.pt'
    old.load_state_dict(torch.load(ckpt,map_location='cpu',weights_only=True)); old.eval()
    models={'identity':None,'old_fitted':old,'control':initial,'paired':copy.deepcopy(initial)}
    protocol=dict(train=list(TRAIN_IDS),validation=list(VALID_IDS),test=list(TEST_IDS),steps=args.steps,
        learning_rate=.002,batch='two intervention pairs = four frames',seed=20260909,batch_seed=436,
        initial_weights='identical fresh weights for control and paired; old model frozen for reference only',
        geometry='one to four nonintersecting spheres, randomized x/z/radius/color; removal and relocation; zero objects after removal',
        normals='5x5 inverse-depth fit with plane-residual acceptance, computed from observed depth',
        train_yaw=[-.1,.1],test_yaw=.07,train_depth_noise=.0005,test_depth_noise=[0.,.002],
        paired_loss='0.5 changed-receiver response MAE + 0.1 unchanged-receiver response MAE, added to image loss',
        checkpoint='fixed final step, no validation/test-based selection',
        gates=dict(response_vs_zero=.8,response_vs_control=.9,max_image_regression=1.05,max_temporal_regression=1.05,max_unchanged_drift=.003),
        game_integration_allowed=False,old_checkpoint_sha256=hashlib.sha256(ckpt.read_bytes()).hexdigest())
    (args.out/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n',encoding='utf-8')
    keys=('features','source','target','valid','shadow_effect')
    pairs=[]; digest=hashlib.sha256()
    for seed in TRAIN_IDS:
        for yaw in (-.1,.1):
            a,_=make_frame(seed,yaw=yaw,noise=.0005); ta=tensors({k:a[k] for k in keys})
            for case in CASES:
                b,_=make_frame(seed,case,yaw=yaw,noise=.0005); tb=tensors({k:b[k] for k in keys})
                change,stable=masks(a,b)
                pairs.append((ta,tb,torch.from_numpy(change)[None,None],torch.from_numpy(stable)[None,None]))
                digest.update(f'{seed}/{yaw}/{case}'.encode())
                for s in (a,b):
                    for k in keys: digest.update(s[k].tobytes())
                digest.update(change.tobytes()); digest.update(stable.tobytes())
    training={}
    for name in ('control','paired'):
        model=models[name]; opt=torch.optim.Adam(model.parameters(),lr=.002)
        rng=np.random.default_rng(436); start=time.perf_counter(); curve=[]
        for step in range(1,args.steps+1):
            chosen=[pairs[i] for i in rng.choice(len(pairs),2,replace=False)]
            a={k:torch.cat([p[0][k] for p in chosen]) for k in keys}; b={k:torch.cat([p[1][k] for p in chosen]) for k in keys}
            change=torch.cat([p[2] for p in chosen]); stable=torch.cat([p[3] for p in chosen])
            opt.zero_grad(set_to_none=True)
            pa,pb=predict_lighting(model,a),predict_lighting(model,b)
            image_loss=.5*(lighting_loss(pa,a)+lighting_loss(pb,b)); response_loss=paired_loss(pa,pb,a,b,change,stable)
            loss=image_loss+response_loss if name=='paired' else image_loss
            if not torch.isfinite(loss): raise RuntimeError('Nonfinite training loss')
            loss.backward(); opt.step()
            if step==1 or step%400==0:
                curve.append(dict(step=step,image_loss=float(image_loss.detach()),response_loss=float(response_loss.detach())))
                print(name,step,curve[-1],flush=True)
        model.eval(); path=args.out/(name+'.pt'); torch.save(model.state_dict(),path)
        training[name]=dict(parameters=sum(p.numel() for p in model.parameters()),cpu_seconds=time.perf_counter()-start,
            curve=curve,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    validation=evaluate(models,VALID_IDS); heldout=evaluate(models,TEST_IDS); motion=temporal(models)
    p,c=heldout['paired'],heldout['control']
    gates=dict(response_beats_zero_20pct=all(p[k]['response']['response_mae']<=.8*p[k]['response']['zero_response_mae'] for k in ('clean','noisy')),
        response_beats_control_10pct=all(p[k]['response']['response_mae']<=.9*c[k]['response']['response_mae'] for k in ('clean','noisy')),
        image_preserved=all(p[k]['image_mae']<=1.05*c[k]['image_mae'] for k in ('clean','noisy')),
        temporal_preserved=all(motion['paired'][k]['mean']<=1.05*motion['control'][k]['mean'] for k in ('clean','noisy')),
        unchanged_drift_below_003=all(p[k]['response']['unchanged_drift']<=.003 for k in ('clean','noisy')),
        protected_exact=all(p[k]['protected_max_change']==0 for k in ('clean','noisy')))
    result=dict(protocol=protocol,training_data_sha256=digest.hexdigest(),training=training,
        validation=validation,held_out=heldout,temporal=motion,gates=gates,synthetic_gates_passed=all(gates.values()),
        game_integration_allowed=False,torch=torch.__version__,
        limitations=['Same diffuse sphere/plane family, fixed lights; no new materials or game images.',
            'Interventions are paired static renders, not animated object sequences or motion-vector validation.',
            'Old model uses new fitted normal inputs and had different training budget; only control/paired isolate the response loss.',
            'Paired loss uses oracle masks in training only. Inference remains single-frame without history.',
            'Nearest temporal reprojection retains subpixel error. No GPU timing/VRAM, export or DLL installation.'])
    (args.out/'metrics.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    visual(models,args.out)
    files=['_IA_Python/neural_lighting_scene.py','_IA_Python/neural_lighting_model.py','_IA_Python/neural_lighting_diagnostics.py',
        '_IA_Python/neural_intervention_training.py','tools/train_neural_interventions.py','tests/test_neural_intervention_training.py']
    (args.out/'source-manifest.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(summary={n:{k:dict(response=v['response'],image=v['image_mae']) for k,v in cases.items()} for n,cases in heldout.items()},
        temporal={n:{k:v['mean'] for k,v in cases.items()} for n,cases in motion.items()},gates=gates),indent=2),flush=True)


if __name__=='__main__': main()
