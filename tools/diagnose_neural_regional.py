"""One fixed scale-handling hypothesis on new scenes; no retraining/search."""
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
from neural_lighting_model import LightingNet,predict_lighting
from neural_regional_inference import predict_regional
from neural_generalization_scene import make_frame,YAWS
from neural_lighting_scene import correspondences
from neural_lighting_diagnostics import response_metrics
from neural_balance_selection import SEEDS,fidelity_metrics
from diagnose_neural_generalization import aggregate,gates,write_json

IDS=tuple(range(22000,22004))
FAMILIES=('baseline','boxes')
SIZES=((192,128),(384,256))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/neural-regional-inference-v1')
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    models={}; hashes={}; expected=json.loads((ROOT/'artifacts/neural-balance-v1/training.json').read_text())
    for seed in SEEDS:
        path=ROOT/f'artifacts/neural-balance-v1/{seed}_t025.pt'
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        if digest!=expected[f'{seed}_t025']['sha256']: raise RuntimeError('Checkpoint mismatch')
        model=LightingNet(); model.load_state_dict(torch.load(path,map_location='cpu',weights_only=True)); model.eval()
        models[str(seed)]=model; hashes[str(seed)]=digest
    protocol=dict(ids=IDS,families=FAMILIES,resolutions=SIZES,yaws=YAWS,checkpoints=hashes,
        hypothesis='internal height 64 with aspect ratio retained; area color, nearest-center geometry, bilinear RGB residual',
        comparison='same selected weights, direct versus regional inference; no training, no model selection',
        gates='same relative checks as generalization; regional versus direct, response also versus zero, both initializations',
        internal_height=64,depth_noise=0.,game_integration_allowed=False)
    write_json(args.out/'protocol.json',protocol)
    paths=['tools/diagnose_neural_regional.py','tools/diagnose_neural_generalization.py',
           '_IA_Python/neural_regional_inference.py','_IA_Python/neural_generalization_scene.py',
           '_IA_Python/neural_lighting_model.py','_IA_Python/neural_lighting_scene.py',
           '_IA_Python/neural_lighting_diagnostics.py','_IA_Python/neural_scene_geometry.py',
           '_IA_Python/neural_intervention_training.py','_IA_Python/neural_balance_selection.py',
           '_IA_Python/bilateral_experiment.py','tools/train_neural_interventions.py','tests/test_neural_regional_inference.py']
    write_json(args.out/'source-manifest.json',{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    names=['identity']+[f'{s}_{mode}' for s in SEEDS for mode in ('direct','regional')]
    canvas=Image.new('RGB',(6*192,4*152),'#18212b'); draw=ImageDraw.Draw(canvas)
    groups={}; input_hash=hashlib.sha256(); start=time.perf_counter()
    with torch.inference_mode():
        for fi,family in enumerate(FAMILIES):
            for si,(width,height) in enumerate(SIZES):
                key=f'{family}_{width}x{height}'; rows={n:dict(images=[],temporal=[],response=[]) for n in names}
                for scene_id in IDS:
                    centers=[]; center_predictions=[]
                    for remove in (False,True):
                        frames=[]; preds={n:[] for n in names}
                        for yaw in YAWS:
                            scene,cam=make_frame(scene_id,family,width,height,yaw,remove)
                            input_hash.update(str((scene_id,family,width,height,yaw,remove)).encode())
                            for k in ('features','source','target','depth','object_id'): input_hash.update(scene[k].tobytes())
                            batch=tensors({k:scene[k] for k in ('features','source','valid')})
                            predictions={'identity':scene['source']}
                            for s,model in models.items():
                                for mode,fn in (('direct',predict_lighting),('regional',predict_regional)):
                                    predictions[f'{s}_{mode}']=fn(model,batch)[0].permute(1,2,0).numpy()
                            for n,pred in predictions.items():
                                preds[n].append(pred)
                                if yaw==0.:
                                    error=np.abs(pred-scene['target']); shadow=scene['shadow_effect']>.005
                                    rows[n]['images'].append(dict(seed=scene_id,remove=remove,image_mae=float(error.mean()),
                                        protected_change=float(np.abs(pred-scene['source'])[scene['valid']==0].max()),
                                        shadow_abs_sum=float(error[shadow].sum()),shadow_samples=int(shadow.sum())*3,
                                        **fidelity_metrics(pred,scene)))
                            frames.append((scene,cam))
                        for i in range(2):
                            iy,ix,mask=correspondences(frames[i][0],frames[i+1][0],frames[i+1][1])
                            if not mask.any(): raise RuntimeError('No temporal correspondences')
                            for n in names:
                                ea=preds[n][i]-frames[i][0]['target']; eb=preds[n][i+1]-frames[i+1][0]['target']
                                rows[n]['temporal'].append(dict(seed=scene_id,remove=remove,pair=i,samples=int(mask.sum()),error=float(np.abs(ea-eb[iy,ix])[mask].mean())))
                        centers.append(frames[1][0]); center_predictions.append({n:p[1] for n,p in preds.items()})
                    for n in names:
                        row,*_=response_metrics(center_predictions[0][n],center_predictions[1][n],*centers)
                        rows[n]['response'].append(dict(seed=scene_id,**row))
                    if scene_id==IDS[0]:
                        values=[centers[0]['source'],centers[0]['target']]+[center_predictions[0][n] for n in names[1:]]
                        for col,(label,value) in enumerate(zip(('Entrada','Referencia','Direta A','Regional A','Direta B','Regional B'),values)):
                            rgb=value.clip(0); rgb=(rgb/(1+rgb))**(1/2.2)
                            tile=Image.fromarray(np.uint8(np.round(rgb*255))).resize((192,128),Image.Resampling.NEAREST)
                            row=fi*2+si; canvas.paste(tile,(col*192,row*152+24)); draw.text((col*192+2,row*152+4),label,fill='white')
                    print(key,scene_id,'complete',flush=True)
                summary={n:aggregate(r) for n,r in rows.items()}
                assessment={str(s):gates(summary[f'{s}_regional'],summary[f'{s}_direct']) for s in SEEDS}
                groups[key]=dict(summary=summary,assessment=assessment,rows=rows)
                write_json(args.out/(key+'.json'),groups[key])
                print(key,{s:a['status'] for s,a in assessment.items()},flush=True)
    result=dict(protocol=protocol,groups=groups,inputs_sha256=input_hash.hexdigest(),cpu_wall_seconds=time.perf_counter()-start,
        all_checks_passed=all(a['status']=='passed' for g in groups.values() for a in g['assessment'].values()),
        game_integration_allowed=False,
        limitations=['Four new scenes, two families and clean depth only; no light-shift or distance coverage in this follow-up.',
                     'Bilinear correction can cross silhouettes; no geometry-aware upsampling or history.',
                     'Inputs remain fully shaded RGB/depth/normal context; no inferred ground-truth material buffers.',
                     'No training, FP16 export, GPU latency/VRAM measurement or DLL installation.'])
    write_json(args.out/'metrics.json',result); canvas.save(args.out/'comparison.png')
    print(json.dumps({k:g['assessment'] for k,g in groups.items()},indent=2),flush=True)


if __name__=='__main__': main()
