"""Post-hoc localization of temporal error; no selection or claimed causal fix."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'_IA_Python'))
from bilateral_experiment import tensors
from neural_balance_selection import SEEDS
from neural_regional_training import TEST_IDS,FAMILIES,edge_mask
from neural_generalization_scene import make_frame,YAWS
from neural_lighting_scene import correspondences
from neural_lighting_model import LightingNet
from neural_regional_inference import predict_regional
from diagnose_neural_generalization import write_json


def band(scene):
    edge=edge_mask(scene)>0
    return np.lib.stride_tricks.sliding_window_view(np.pad(edge,2),(5,5)).any(axis=(-1,-2))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/neural-regional-temporal-regions-v1')
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); models={}; hashes={}
    prior=json.loads((ROOT/'artifacts/neural-balance-v1/training.json').read_text())
    trained=json.loads((ROOT/'artifacts/neural-regional-training-v1/training.json').read_text())
    for seed in SEEDS:
        for variant in ('frozen','joint'):
            name=f'{seed}_{variant}'
            path=ROOT/f'artifacts/neural-balance-v1/{seed}_t025.pt' if variant=='frozen' else ROOT/f'artifacts/neural-regional-training-v1/{name}.pt'
            digest=hashlib.sha256(path.read_bytes()).hexdigest()
            expected=prior[f'{seed}_t025']['sha256'] if variant=='frozen' else trained[name]['sha256']
            if digest!=expected: raise RuntimeError('Checkpoint mismatch')
            model=LightingNet(); model.load_state_dict(torch.load(path,map_location='cpu',weights_only=True)); model.eval()
            models[name]=model; hashes[name]=digest
    protocol=dict(ids=TEST_IDS,families=FAMILIES,resolution=[384,256],depth_noise=0.,yaws=YAWS,checkpoints=hashes,
        nature='post-hoc diagnostic on already evaluated scenes, not an independent test or model selection',
        partition='visible correspondences: union of 2-pixel dilated geometry boundary in both frames; remaining ground; remaining objects',
        limitations='Location alone does not identify the source of error or predict effects of a new reconstruction operator.')
    write_json(args.out/'protocol.json',protocol)
    rows={n:[] for n in models}
    with torch.inference_mode():
        for family in FAMILIES:
            for seed in TEST_IDS:
                for remove in (False,True):
                    frames=[]; errors={n:[] for n in models}
                    for yaw in YAWS:
                        scene,cam=make_frame(seed,family,384,256,yaw,remove)
                        batch=tensors({k:scene[k] for k in ('features','source','valid')})
                        for n,model in models.items():
                            pred=predict_regional(model,batch)[0].permute(1,2,0).numpy()
                            errors[n].append(pred-scene['target'])
                        frames.append((scene,cam,band(scene)))
                    for i in range(2):
                        a,_,edge_a=frames[i]; b,cam_b,edge_b=frames[i+1]
                        iy,ix,visible=correspondences(a,b,cam_b)
                        boundary=visible & (edge_a | edge_b[iy,ix])
                        ground=visible & ~boundary & (a['object_id']==0)
                        objects=visible & ~boundary & (a['object_id']>0)
                        regions=dict(boundary=boundary,ground=ground,objects=objects)
                        if sum(int(m.sum()) for m in regions.values())!=int(visible.sum()): raise RuntimeError('Incomplete partition')
                        for n in models:
                            delta=np.abs(errors[n][i]-errors[n][i+1][iy,ix])
                            row=dict(seed=seed,family=family,remove=remove,pair=i,regions={})
                            for region,mask in regions.items():
                                row['regions'][region]=dict(samples=int(mask.sum())*3,error_sum=float(delta[mask].sum()))
                            partition_sum=sum(r['error_sum'] for r in row['regions'].values())
                            if not np.isclose(partition_sum,float(delta[visible].sum()),rtol=1e-5,atol=1e-5): raise RuntimeError('Partition sum mismatch')
                            rows[n].append(row)
                print(family,seed,'localized',flush=True)
    summary={}
    for n,values in rows.items():
        by_region={}
        for region in ('boundary','ground','objects'):
            samples=sum(r['regions'][region]['samples'] for r in values)
            error=sum(r['regions'][region]['error_sum'] for r in values)
            by_region[region]=dict(samples=samples,error_sum=error,mean=error/samples if samples else None)
        total_error=sum(r['error_sum'] for r in by_region.values()); total_samples=sum(r['samples'] for r in by_region.values())
        for r in by_region.values():
            r['error_fraction']=r['error_sum']/total_error if total_error else 0.
            r['sample_fraction']=r['samples']/total_samples
        summary[n]=by_region
    write_json(args.out/'metrics.json',dict(protocol=protocol,summary=summary,rows=rows,game_integration_allowed=False))
    sources=['tools/diagnose_regional_temporal_regions.py','_IA_Python/neural_regional_training.py',
        '_IA_Python/neural_regional_inference.py','_IA_Python/neural_generalization_scene.py',
        '_IA_Python/neural_lighting_scene.py','_IA_Python/neural_lighting_model.py']
    write_json(args.out/'source-manifest.json',{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources})
    print(json.dumps(summary,indent=2),flush=True)


if __name__=='__main__': main()
