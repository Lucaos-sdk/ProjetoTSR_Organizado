"""Train against ray-tested direct-light references, not a tone-curve teacher."""
import argparse
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
from neural_lighting_scene import TRAIN_IDS,VALID_IDS,TEST_IDS,render,correspondences
from neural_lighting_model import LightingNet,PointLightingNet,predict_lighting,lighting_loss


def digest_file(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def prediction(name,model,batch,gain):
    if name=='input': return batch['source']
    if name=='global_rgb': return torch.where(batch['valid']>0,batch['source']*gain,batch['source'])
    return predict_lighting(model,batch)


def evaluate(models,gain,ids):
    results={}
    with torch.inference_mode():
        for name,model in models.items():
            cases={}
            for noise in (0.,.002):
                rows=[]
                for seed in ids:
                    for yaw in (-.08,.08):
                        scene,_=render(seed,yaw=yaw,depth_noise=noise)
                        batch=tensors(scene)
                        pred=prediction(name,model,batch,gain)
                        err=pred-batch['target']
                        shadow=(batch['shadow_effect']>.005).expand_as(err)
                        sphere=(batch['object_id']>0).expand_as(err)
                        protected=(batch['valid']==0).expand_as(err)
                        rows.append(dict(seed=seed,yaw=yaw,mae=float(err.abs().mean()),
                            shadow_mae=float(err[shadow].abs().mean()) if shadow.any() else None,
                            sphere_mae=float(err[sphere].abs().mean()),
                            gradient_mae=float((err[:,:,1:]-err[:,:,:-1]).abs().mean()+(err[:,:,:,1:]-err[:,:,:,:-1]).abs().mean()),
                            protected_max_change=float((pred-batch['source'])[protected].abs().max())))
                means={k:float(np.mean([r[k] for r in rows if r[k] is not None])) for k in rows[0] if k not in ('seed','yaw')}
                cases['clean' if noise==0 else 'noisy_depth']=dict(mean=means,frames=rows)
            results[name]=cases
    return results


def temporal(models,gain):
    rows={n:[] for n in models}
    with torch.inference_mode():
        for seed in TEST_IDS:
            frames=[render(seed,yaw=y) for y in (-.08,-.04,0.,.04,.08)]
            errors={}
            for name,model in models.items():
                errors[name]=[]
                for scene,_ in frames:
                    batch=tensors(scene)
                    pred=prediction(name,model,batch,gain)
                    errors[name].append((pred-batch['target'])[0].permute(1,2,0).numpy())
            for i in range(4):
                a,_=frames[i]; b,cam=frames[i+1]
                iy,ix,mask=correspondences(a,b,cam)
                if not mask.any(): raise RuntimeError('No temporal correspondences')
                for name in models:
                    delta=errors[name][i]-errors[name][i+1][iy,ix]
                    rows[name].append(dict(seed=seed,pair=i,samples=int(mask.sum()),
                        warped_error_delta=float(np.abs(delta[mask]).mean())))
    return {n:dict(mean=float(np.mean([r['warped_error_delta'] for r in v])),
                   max_pair=max(r['warped_error_delta'] for r in v),pairs=v) for n,v in rows.items()}


def visual(models,gain,out):
    labels=['Entrada','Referencia 3D','Ganho de cor','IA sem vizinhos','IA espacial','Erro IA x4']
    names=['input','global_rgb','point','spatial']
    def tile(value):
        value=value[0].permute(1,2,0).numpy()
        # Common exposure + display mapping, no per-image normalization.
        value=np.maximum(value,0); value=value/(1+value)
        return Image.fromarray(np.uint8(np.round(value**(1/2.2)*255)))
    def row(seed,yaw=0.):
        batch=tensors(render(seed,yaw=yaw)[0])
        pred={n:prediction(n,models[n],batch,gain) for n in names}
        values=[pred['input'],batch['target'],pred['global_rgb'],pred['point'],pred['spatial'],4*(pred['spatial']-batch['target']).abs()]
        strip=Image.new('RGB',(96*6,84),'#17202a'); draw=ImageDraw.Draw(strip)
        for i,(label,v) in enumerate(zip(labels,values)):
            strip.paste(tile(v),(96*i,20)); draw.text((96*i+2,3),label,fill='white')
        return strip
    with torch.inference_mode():
        canvas=Image.new('RGB',(96*6,84*3))
        for i,seed in enumerate(TEST_IDS[:3]): canvas.paste(row(seed),(0,84*i))
        canvas.resize((1152,504)).save(out/'comparison.png')
        frames=[row(TEST_IDS[0],yaw).resize((1152,168)) for yaw in (-.08,-.04,0.,.04,.08,.04,0.,-.04)]
        frames[0].save(out/'camera_rotation.gif',save_all=True,append_images=frames[1:],duration=180,loop=0)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/neural-lighting-v1')
    parser.add_argument('--steps',type=int,default=1000)
    args=parser.parse_args()
    if args.steps<1: parser.error('steps must be positive')
    args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True); torch.manual_seed(20260909)
    protocol=dict(train=list(TRAIN_IDS),validation=list(VALID_IDS),test=list(TEST_IDS),
        train_yaw=[-.10,0.,.10],test_yaw=[-.08,.08],steps=args.steps,seed=20260909,batch_seed=817,
        resolution=[96,64],batch=4,learning_rate=.002,checkpoint='fixed final step, no test-based selection',
        teacher='Lambertian direct area-light samples with analytic visibility rays; two fixed world-space light rigs; fog composed after lighting',
        inputs='RGB compressed, world normals reconstructed from observed depth, observed camera Z / 30, known valid mask',
        excluded_inputs=['albedo','light visibility','shadow mask','analytic normals','occluded geometry'],
        input_noise='0.05 percent relative depth in training; clean and 0.2 percent in held-out evaluation',
        target_limitations='Only spheres and ground, diffuse materials, fixed rigs, four light samples; no indirect lighting, specular materials, game images or generative prior',
        candidate_gates=dict(min_mae_gain_vs_point=.10,min_shadow_gain_vs_point=.10,
            min_mae_gain_vs_global_rgb=.15,max_gradient_regression_vs_point=.05,max_temporal_regression_vs_point=.05),
        game_integration_allowed=False,gpu_latency_ms=None)
    (args.out/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n',encoding='utf-8')
    data={}; digest=hashlib.sha256()
    for seed in TRAIN_IDS:
        for view,yaw in enumerate(protocol['train_yaw']):
            scene,_=render(seed,yaw=yaw,depth_noise=.0005)
            data[(seed,view)]=tensors(scene)
            for key,value in sorted(scene.items()): digest.update(f'{seed}/{view}/{key}'.encode()); digest.update(value.tobytes())
    # Fit a three-channel constant gain on training data only.
    numerator=torch.zeros(1,3,1,1); denominator=torch.zeros_like(numerator)
    for b in data.values():
        numerator+=(b['source']*b['target']*b['valid']).sum((0,2,3),keepdim=True)
        denominator+=(b['source'].square()*b['valid']).sum((0,2,3),keepdim=True)
    gain=numerator/denominator.clamp_min(1e-8)
    models={'input':None,'global_rgb':None,'point':PointLightingNet(),'spatial':LightingNet()}
    training={}
    keys=list(data)
    for name in ('spatial','point'):
        model=models[name]; optimizer=torch.optim.Adam(model.parameters(),lr=.002)
        rng=np.random.default_rng(817); start=time.perf_counter(); curve=[]
        for step in range(1,args.steps+1):
            sampled=[keys[i] for i in rng.choice(len(keys),4,replace=False)]
            batch={k:torch.cat([data[s][k] for s in sampled]) for k in data[sampled[0]]}
            optimizer.zero_grad(set_to_none=True)
            loss=lighting_loss(predict_lighting(model,batch),batch)
            if not torch.isfinite(loss): raise RuntimeError('Nonfinite training loss')
            loss.backward(); optimizer.step()
            if step==1 or step%200==0:
                curve.append(dict(step=step,loss=float(loss.detach())))
                print(name,step,round(float(loss.detach()),7),flush=True)
        model.eval(); path=args.out/(name+'.pt'); torch.save(model.state_dict(),path)
        training[name]=dict(parameters=sum(p.numel() for p in model.parameters()),cpu_train_seconds=time.perf_counter()-start,
                            curve=curve,sha256=digest_file(path))
    validation=evaluate(models,gain,VALID_IDS)
    tests=evaluate(models,gain,TEST_IDS)
    movement=temporal(models,gain)
    c=tests['spatial']['clean']['mean']; p=tests['point']['clean']['mean']; g=tests['global_rgb']['clean']['mean']
    gates=dict(beats_point_mae_10pct=c['mae']<=.9*p['mae'],beats_point_shadow_10pct=c['shadow_mae']<=.9*p['shadow_mae'],
        beats_global_gain_15pct=c['mae']<=.85*g['mae'],edges_not_worse_5pct=c['gradient_mae']<=1.05*p['gradient_mae'],
        temporal_not_worse_5pct=movement['spatial']['mean']<=1.05*movement['point']['mean'],
        exact_protection=all(tests['spatial'][k]['mean']['protected_max_change']==0 for k in tests['spatial']))
    result=dict(protocol=protocol,torch=torch.__version__,data_sha256=digest.hexdigest(),global_rgb_gain=gain.flatten().tolist(),
        training=training,validation=validation,held_out=tests,temporal=movement,gates=gates,
        synthetic_gates_passed=all(gates.values()),game_integration_allowed=False,
        temporal_scope='48 perspective camera-rotation/translation pairs; nearest reprojection, matching primitive/depth, subpixel distance <0.35; error vs own target, no history or moving objects',
        next_gate='Generalization beyond synthetic spheres, temporal behavior, complete D3D12 GPU timing/memory and available game inputs.')
    (args.out/'metrics.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    files=['_IA_Python/neural_lighting_scene.py','_IA_Python/neural_lighting_model.py','tools/train_neural_lighting.py','tests/test_neural_lighting.py']
    (args.out/'source-manifest.json').write_text(json.dumps({f:digest_file(ROOT/f) for f in files},indent=2)+'\n',encoding='utf-8')
    visual(models,gain,args.out)
    print(json.dumps(dict(clean={n:v['clean']['mean'] for n,v in tests.items()},
        temporal={n:v['mean'] for n,v in movement.items()},gates=gates),indent=2),flush=True)


if __name__=='__main__': main()
