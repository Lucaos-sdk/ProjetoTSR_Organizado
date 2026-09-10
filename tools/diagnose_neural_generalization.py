"""Frozen checkpoints across independently varied domains and raster sizes."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import numpy as np
import torch
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'_IA_Python'))
from bilateral_experiment import tensors
from neural_lighting_model import LightingNet,predict_lighting
from neural_generalization_scene import SCENE_IDS,FAMILIES,RESOLUTIONS,YAWS,make_frame
from neural_lighting_scene import correspondences
from neural_lighting_diagnostics import response_metrics
from neural_balance_selection import SEEDS,model_name,fidelity_metrics
from train_neural_interventions import response_summary


def write_json(path,data):
    path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def aggregate(rows):
    images=rows['images']; temporal=rows['temporal']; response=rows['response']
    def pooled(total,count):
        n=sum(r[count] for r in images)
        return sum(r[total] for r in images)/n if n else None
    summary=dict(image_mae=float(np.mean([r['image_mae'] for r in images])),
        shadow_mae=pooled('shadow_abs_sum','shadow_samples'),
        edge_mae=pooled('edge_abs_sum','edge_samples'),
        gradient_mae=pooled('gradient_abs_sum','gradient_samples'),
        protected_max_change=max(r['protected_change'] for r in images),
        response=response_summary(response),
        affected_scenes=sum(r['changed_pixels']>0 for r in response),
        temporal=float(np.average([r['error'] for r in temporal],weights=[r['samples'] for r in temporal])))
    return summary


def gates(candidate,control):
    """No causal-response pass without adequate changed-receiver coverage."""
    r,c=candidate['response'],control['response']
    reliable=r['changed_pixels']>=100 and candidate['affected_scenes']>=2
    def relative(a,b,limit):
        return None if a is None or b is None else bool(a<=limit*b)
    checks=dict(image=relative(candidate['image_mae'],control['image_mae'],1.05),
        temporal=relative(candidate['temporal'],control['temporal'],.8),
        response_zero=relative(r['response_mae'],r['zero_response_mae'],.8) if reliable else None,
        response=relative(r['response_mae'],c['response_mae'],1.05) if reliable else None,
        unchanged=None if r['unchanged_drift'] is None else bool(r['unchanged_drift']<=.003),
        protected=candidate['protected_max_change']==0,
        shadow=relative(candidate['shadow_mae'],control['shadow_mae'],1.05),
        edge=relative(candidate['edge_mae'],control['edge_mae'],1.05),
        gradient=relative(candidate['gradient_mae'],control['gradient_mae'],1.05))
    return dict(checks=checks,response_reliable=reliable,
                status='failed' if any(v is False for v in checks.values()) else
                       'inconclusive' if any(v is None for v in checks.values()) else 'passed')


def image_row(canvas,draw,row,scene,predictions,width=192,height=128):
    values=[scene['source'],scene['target']]+[predictions[model_name(s,v)] for s in SEEDS for v in ('paired','t025')]
    labels=('Entrada','Referencia','Pares A','t025 A','Pares B','t025 B')
    for col,(label,value) in enumerate(zip(labels,values)):
        rgb=value.clip(0); rgb=(rgb/(1+rgb))**(1/2.2)
        tile=Image.fromarray(np.uint8(np.round(rgb*255))).resize((width,height),Image.Resampling.NEAREST)
        canvas.paste(tile,(col*width,row*(height+24)+24))
        draw.text((col*width+3,row*(height+24)+4),label,fill='white')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/neural-generalization-v1')
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    models={'identity':None}; hashes={}
    prior=ROOT/'artifacts/neural-balance-v1'
    expected=json.loads((prior/'training.json').read_text(encoding='utf-8'))
    for seed in SEEDS:
        for variant in ('paired','t025'):
            name=model_name(seed,variant); path=prior/(name+'.pt')
            digest=hashlib.sha256(path.read_bytes()).hexdigest()
            if digest!=expected[name]['sha256']: raise RuntimeError('Checkpoint hash mismatch')
            model=LightingNet(); model.load_state_dict(torch.load(path,map_location='cpu',weights_only=True)); model.eval()
            models[name]=model; hashes[name]=digest
    protocol=dict(scene_ids=SCENE_IDS,families=FAMILIES,resolutions=RESOLUTIONS,yaws=YAWS,
        checkpoints=hashes,training=False,selection=False,depth_noise=0.,
        geometry_states=['base','remove_all'],static_metrics='middle camera pose only',
        camera_metrics='both static geometry states, consecutive poses; nearest visible correspondence',
        input='original 8 channels with normals reconstructed from observed depth; direct inference at each raster size',
        target='same fixed target lighting; source_light changes only the input light rig',
        factors='baseline geometry; replace alternating spheres with boxes; move spheres +4 in z; alter source light',
        minimum_response=dict(changed_pixels=100,affected_scenes=2),
        gates=dict(image_max=1.05,temporal_max=.8,response_max=1.05,response_vs_zero=.8,
                   unchanged_max=.003,protected_exact=True,shadow_max=1.05,edge_max=1.05,gradient_max=1.05),
        warning='diagnostic with four scenes only; ratios within same family/resolution, never across different raster sizes',
        game_integration_allowed=False)
    write_json(args.out/'protocol.json',protocol)
    paths=['tools/diagnose_neural_generalization.py','_IA_Python/neural_generalization_scene.py',
           '_IA_Python/neural_scene_geometry.py','_IA_Python/neural_lighting_scene.py',
           '_IA_Python/neural_lighting_diagnostics.py','_IA_Python/neural_lighting_model.py',
           '_IA_Python/neural_intervention_training.py','_IA_Python/neural_balance_selection.py',
           '_IA_Python/bilateral_experiment.py','tools/train_neural_interventions.py','tests/test_neural_generalization.py']
    write_json(args.out/'source-manifest.json',{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    preview=Image.new('RGB',(192*6,152*4),'#18212b'); draw=ImageDraw.Draw(preview)
    scales=Image.new('RGB',(192*6,152*3),'#18212b'); scale_draw=ImageDraw.Draw(scales)
    groups={}; input_digest=hashlib.sha256(); start=time.perf_counter()
    with torch.inference_mode():
        for family_index,family in enumerate(FAMILIES):
            for size_index,(width,height) in enumerate(RESOLUTIONS):
                group=f'{family}_{width}x{height}'
                rows={n:dict(images=[],response=[],temporal=[]) for n in models}
                for seed in SCENE_IDS:
                    scene_frames=[]; all_predictions=[]
                    for remove in (False,True):
                        frames=[]; predictions={n:[] for n in models}
                        for yaw in YAWS:
                            scene,cam=make_frame(seed,family,width,height,yaw,remove)
                            input_digest.update(str((seed,family,width,height,yaw,remove)).encode())
                            for key in ('features','source','target','depth','object_id'):
                                input_digest.update(scene[key].tobytes())
                            # Only inference inputs, not oracle target or analytic normals.
                            batch=tensors({k:scene[k] for k in ('features','source','valid')})
                            for name,model in models.items():
                                pred=scene['source'] if model is None else predict_lighting(model,batch)[0].permute(1,2,0).numpy()
                                predictions[name].append(pred)
                                if yaw==0.:
                                    shadow=scene['shadow_effect']>.005
                                    error=np.abs(pred-scene['target'])
                                    rows[name]['images'].append(dict(seed=seed,remove=remove,image_mae=float(error.mean()),
                                        protected_change=float(np.abs(pred-scene['source'])[scene['valid']==0].max()),
                                        shadow_abs_sum=float(error[shadow].sum()),shadow_samples=int(shadow.sum())*3,
                                        **fidelity_metrics(pred,scene)))
                            frames.append((scene,cam))
                        for i in range(len(YAWS)-1):
                            iy,ix,visible=correspondences(frames[i][0],frames[i+1][0],frames[i+1][1])
                            if not visible.any(): raise RuntimeError('No temporal correspondences')
                            for name in models:
                                ea=predictions[name][i]-frames[i][0]['target']
                                eb=predictions[name][i+1]-frames[i+1][0]['target']
                                rows[name]['temporal'].append(dict(seed=seed,remove=remove,pair=i,samples=int(visible.sum()),
                                    error=float(np.abs(ea-eb[iy,ix])[visible].mean())))
                        scene_frames.append(frames[1][0]); all_predictions.append({n:p[1] for n,p in predictions.items()})
                    for name in models:
                        response,*_=response_metrics(all_predictions[0][name],all_predictions[1][name],*scene_frames)
                        rows[name]['response'].append(dict(seed=seed,**response))
                    if seed==SCENE_IDS[0]:
                        if width==192:
                            image_row(preview,draw,family_index,scene_frames[0],all_predictions[0])
                        if family=='baseline':
                            image_row(scales,scale_draw,size_index,scene_frames[0],all_predictions[0])
                    print(group,'scene',seed,'complete',flush=True)
                summary={n:aggregate(r) for n,r in rows.items()}
                assessment={str(seed):gates(summary[model_name(seed,'t025')],summary[model_name(seed,'paired')]) for seed in SEEDS}
                groups[group]=dict(summary=summary,assessment=assessment,rows=rows)
                write_json(args.out/(group+'.json'),groups[group])
                print(group,'status',{s:a['status'] for s,a in assessment.items()},flush=True)
    result=dict(protocol=protocol,groups=groups,inputs_sha256=input_digest.hexdigest(),cpu_wall_seconds=time.perf_counter()-start,
        game_integration_allowed=False,torch=torch.__version__,
        limitations=['Four scenes per domain, no training or model selection, no statistical generalization claim.',
                     'Four-sample direct-light reference, diffuse surfaces, no indirect light or game materials.',
                     'Clean depth only; these diagnostics do not replace earlier noise testing.',
                     'Larger images still below game internal resolution; no FP16, GPU timing or VRAM measurements.',
                     'Camera sequences have static objects; removal is a counterfactual pair, not an animation.',
                     'Pixel thresholds and normal-fit footprint stay fixed; cross-resolution metrics have different sampled coverage.'])
    write_json(args.out/'metrics.json',result)
    preview.save(args.out/'domains.png'); scales.save(args.out/'resolutions.png')
    print(json.dumps({g:v['assessment'] for g,v in groups.items()},indent=2),flush=True)


if __name__=='__main__': main()
