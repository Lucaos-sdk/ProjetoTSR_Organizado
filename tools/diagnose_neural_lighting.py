"""Frozen-model interventions and normal ablation. No training or installation."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import torch
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'_IA_Python'))
from bilateral_experiment import tensors
from neural_lighting_scene import render,correspondences,TRAIN_IDS
from neural_lighting_diagnostics import CASES,intervention,receiver_mask,response_metrics,normal_variant
from neural_lighting_model import LightingNet,PointLightingNet,predict_lighting

IDS=tuple(range(10000,10012))


def infer(model,scene):
    if model is None: return scene['source']
    return predict_lighting(model,tensors(scene))[0].permute(1,2,0).numpy()


def summarize(rows,weight=None):
    result={}
    for key in rows[0]:
        if key in ('seed','case','yaw','pair'): continue
        values=[r for r in rows if isinstance(r[key],(int,float)) and r[key] is not None]
        if not values: continue
        weights=[r.get(weight,1) if weight else 1 for r in values]
        result[key]=float(np.average([r[key] for r in values],weights=weights)) if sum(weights)>0 else None
    return result


def visualize(models,path):
    seed=IDS[0]; a,_=render(seed)
    canvas=Image.new('RGB',(96*6,84*3),'#18212b'); draw=ImageDraw.Draw(canvas)
    for row,case in enumerate(('remove_middle','move_middle_right','remove_all')):
        b,_=render(seed,spheres_override=intervention(seed,case))
        pa,pb=infer(models['spatial'],a),infer(models['spatial'],b)
        _,expected,actual,mask=response_metrics(pa,pb,a,b)
        values=[a['target'],b['target'],pa,pb,np.abs(expected)*5,np.abs(actual-expected)*5]
        for col,(label,value) in enumerate(zip(('Ref. original','Ref. alterada','IA original','IA alterada','Mudanca x5','Erro mud. x5'),values)):
            if col>=4: value=np.where(mask[...,None],value,0)
            value=np.maximum(value,0); value=(value/(1+value))**(1/2.2)
            tile=Image.fromarray(np.uint8(np.round(value*255)))
            canvas.paste(tile,(96*col,84*row+20)); draw.text((96*col+2,84*row+3),label,fill='white')
    canvas.resize((1152,504)).save(path)


def main():
    out=ROOT/'artifacts/neural-lighting-diagnostics-v1'
    out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    paths=dict(spatial=ROOT/'artifacts/neural-lighting-v1/spatial.pt',
        point=ROOT/'artifacts/neural-lighting-v1/point.pt',
        temporal=ROOT/'artifacts/neural-lighting-temporal-v1/temporal.pt')
    models={'identity':None,'spatial':LightingNet(),'point':PointLightingNet(),'temporal':LightingNet()}
    hashes={}
    for n,path in paths.items():
        models[n].load_state_dict(torch.load(path,map_location='cpu',weights_only=True)); models[n].eval()
        hashes[n]=hashlib.sha256(path.read_bytes()).hexdigest()
    protocol=dict(scene_ids=list(IDS),cases=list(CASES),checkpoint_hashes=hashes,
        receiver='same visible ground points, 2-pixel eroded joint visibility mask',
        metric='change of predicted RESIDUAL vs change of reference residual, not change of output RGB alone',
        changed_threshold_rgb_mae=.005,unchanged_threshold_rgb_max=1e-6,
        normal_ablation='fixed weights: reconstructed/analytic normals, clean/0.2-percent noisy depth; targets identical',
        diagnostic_gate='pooled residual-response MAE at least 20 percent below zero response; not a release gate',
        training=False,game_integration_allowed=False)
    (out/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n',encoding='utf-8')
    # Verify the renderer extension did not change any original training samples.
    original=json.loads((ROOT/'artifacts/neural-lighting-v1/metrics.json').read_text(encoding='utf-8'))
    digest=hashlib.sha256()
    for seed in TRAIN_IDS:
        for view,yaw in enumerate((-.1,0.,.1)):
            scene,_=render(seed,yaw=yaw,depth_noise=.0005)
            for key,value in sorted(scene.items()): digest.update(f'{seed}/{view}/{key}'.encode()); digest.update(value.tobytes())
    if digest.hexdigest()!=original['data_sha256']: raise RuntimeError('Original training fixture changed')
    intervention_rows={name:[] for name in models}
    normal_rows={name:{mode:[] for mode in ('reconstructed_clean','oracle_clean','reconstructed_noisy','oracle_noisy')} for name in models}
    motion_rows={name:{mode:[] for mode in normal_rows[name]} for name in models}
    geometry=[]
    with torch.inference_mode():
        for seed in IDS:
            a,_=render(seed); pred_a={n:infer(m,a) for n,m in models.items()}
            for case in CASES:
                b,_=render(seed,spheres_override=intervention(seed,case))
                mask=receiver_mask(a,b)
                if not np.array_equal(a['world'][mask],b['world'][mask]): raise RuntimeError('Receiver moved')
                for name,model in models.items():
                    row,*_=response_metrics(pred_a[name],infer(model,b),a,b)
                    intervention_rows[name].append(dict(seed=seed,case=case,**row))
            for noisy in (False,True):
                frames=[render(seed,yaw=y,depth_noise=.002 if noisy else 0.) for y in (-.08,-.04,0.,.04,.08)]
                for oracle in (False,True):
                    mode=('oracle' if oracle else 'reconstructed')+('_noisy' if noisy else '_clean')
                    scenes=[normal_variant(s,oracle) for s,_ in frames]
                    predictions={n:[infer(m,s) for s in scenes] for n,m in models.items()}
                    for frame,scene in enumerate(scenes):
                        for name in models:
                            error=predictions[name][frame]-scene['target']
                            valid=scene['valid']>0
                            normal_rows[name][mode].append(dict(seed=seed,pair=frame,
                                valid_mae=float(np.abs(error[valid]).mean())))
                    for i in range(4):
                        iy,ix,mask=correspondences(scenes[i],scenes[i+1],frames[i+1][1])
                        for name in models:
                            err_a=predictions[name][i]-scenes[i]['target']
                            err_b=predictions[name][i+1]-scenes[i+1]['target']
                            delta=err_a-err_b[iy,ix]
                            motion_rows[name][mode].append(dict(seed=seed,pair=i,samples=int(mask.sum()),
                                warped_error_delta=float(np.abs(delta[mask]).mean())))
                for frame,(scene,_) in enumerate(frames):
                    mask=(scene['valid']>0)&(scene['normal_valid']>0)
                    observed=scene['features'][...,3:6]
                    dot=np.sum(observed*scene['normal'],axis=-1).clip(-1,1)
                    angles=np.rad2deg(np.arccos(dot[mask]))
                    geometry.append(dict(seed=seed,pair=frame,noisy=noisy,median_angle=float(np.median(angles)),p95_angle=float(np.quantile(angles,.95))))
            print('diagnosed',seed,flush=True)
    interventions={}
    for name,rows in intervention_rows.items():
        # Pool errors by changed-pixel count; report drift separately, weighted by
        # unchanged pixels. Cosine/gain here are per-case weighted summaries.
        means=summarize(rows,'changed_pixels')
        eligible=[r for r in rows if r['unchanged_residual_drift'] is not None]
        drift=float(np.average([r['unchanged_residual_drift'] for r in eligible],weights=[r['unchanged_pixels'] for r in eligible]))
        means['unchanged_residual_drift']=drift
        interventions[name]=dict(pooled=means,passes_response_gate=means['response_mae']<=.8*means['zero_response_mae'],
            by_case={case:summarize([r for r in rows if r['case']==case],'changed_pixels') for case in CASES},pairs=rows)
    ablations={n:{mode:dict(image_mean=summarize(normal_rows[n][mode]),temporal_mean=summarize(motion_rows[n][mode],'samples'),
        images=normal_rows[n][mode],motion=motion_rows[n][mode]) for mode in normal_rows[n]} for n in models}
    result=dict(protocol=protocol,interventions=interventions,normal_ablation=ablations,normal_accuracy=geometry,
        original_training_data_hash_verified=True,data_sha256=digest.hexdigest(),game_integration_allowed=False,
        limitations=['Counterfactual response below zero-response error is necessary, not proof of general illumination understanding.',
                     'Oracle normals change input distribution and are unavailable in the game; this is not a retrained upper bound.',
                     'Nearest reprojection and low render resolution retain sampling error; this diagnostic does not separate all temporal sources.',
                     'Static three-sphere scene family, fixed light rigs; no complex materials, motion buffers, inference history or GPU benchmark.'])
    (out/'metrics.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    with torch.inference_mode(): visualize(models,out/'interventions.png')
    # Keep paired float evidence for the predetermined first scene, not screenshots alone.
    a,_=render(IDS[0]); b,_=render(IDS[0],spheres_override=intervention(IDS[0],'remove_middle'))
    np.savez_compressed(out/'example_pair.npz',**{'a_'+k:v for k,v in a.items()},**{'b_'+k:v for k,v in b.items()})
    files=['_IA_Python/neural_lighting_scene.py','_IA_Python/neural_lighting_model.py','_IA_Python/neural_lighting_diagnostics.py',
           'tools/diagnose_neural_lighting.py','tests/test_neural_lighting_diagnostics.py']
    (out/'source-manifest.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(interventions={n:v['pooled'] for n,v in interventions.items()},
        ablation={n:{mode:v['temporal_mean']['warped_error_delta'] for mode,v in modes.items()} for n,modes in ablations.items()}),indent=2),flush=True)


if __name__=='__main__': main()
