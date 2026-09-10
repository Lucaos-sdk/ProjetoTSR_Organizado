"""Frozen CNN ablation: direct derivatives vs fitted inverse depth vs oracle."""
import hashlib
import argparse
import json
from pathlib import Path
import sys
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'_IA_Python'))
from bilateral_experiment import tensors
from neural_lighting_scene import render,correspondences
from neural_lighting_diagnostics import fitted_depth_normals
from neural_lighting_model import LightingNet,predict_lighting
from diagnose_neural_lighting import infer,summarize


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--plane-residual',action='store_true')
    args=parser.parse_args()
    out=ROOT/('artifacts/neural-plane-normals-v1' if args.plane_residual else 'artifacts/neural-fitted-normals-v1'); out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    model=LightingNet(); ckpt=ROOT/'artifacts/neural-lighting-v1/spatial.pt'
    model.load_state_dict(torch.load(ckpt,map_location='cpu',weights_only=True)); model.eval()
    ids=list(range(12000,12012) if args.plane_residual else range(11000,11012))
    protocol=dict(ids=ids,checkpoint_sha256=hashlib.sha256(ckpt.read_bytes()).hexdigest(),training=False,
        modes=['direct','fitted','plane_residual','oracle'] if args.plane_residual else ['direct','fitted','oracle'],depth_noise=[0.,.002],
        comparison='all modes use identical color/depth/targets and fixed weights, changing normal channels only',
        gate='at least 20% lower noisy temporal error, with clean image MAE no more than 5% worse than direct',
        limitations='Input-distribution intervention, not a retrained upper bound or game test; nearest reprojection retains sampling error.')
    (out/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n',encoding='utf-8')
    results={}
    with torch.inference_mode():
        for noise in (0.,.002):
            rows={n:[] for n in protocol['modes']}; motion={n:[] for n in rows}
            for seed in ids:
                frames=[render(seed,yaw=y,depth_noise=noise) for y in (-.08,-.04,0.,.04,.08)]
                errors={n:[] for n in rows}
                for index,(scene,cam) in enumerate(frames):
                    fitted,fit_mask=fitted_depth_normals(scene['features'][...,6]*30,cam)
                    if args.plane_residual:
                        robust,robust_mask=fitted_depth_normals(scene['features'][...,6]*30,cam,plane_residual=True)
                    for name in rows:
                        batch={k:v.copy() for k,v in scene.items()}
                        if name=='fitted': batch['features'][...,3:6]=fitted
                        if name=='plane_residual': batch['features'][...,3:6]=robust
                        if name=='oracle': batch['features'][...,3:6]=scene['normal']
                        pred=infer(model,batch); error=pred-scene['target']; errors[name].append(error)
                        mask=(scene['valid']>0)&(scene['normal_valid']>0)
                        dot=np.sum(batch['features'][...,3:6]*scene['normal'],axis=-1).clip(-1,1)
                        angle=np.rad2deg(np.arccos(dot[mask]))
                        rows[name].append(dict(seed=seed,pair=index,mae=float(np.abs(error[scene['valid']>0]).mean()),
                            angle_median=float(np.median(angle)),angle_p95=float(np.quantile(angle,.95)),
                            fit_fraction=float((robust_mask if name=='plane_residual' else fit_mask)[scene['valid']>0].mean())))
                for i in range(4):
                    iy,ix,mask=correspondences(frames[i][0],frames[i+1][0],frames[i+1][1])
                    for name in rows:
                        diff=errors[name][i]-errors[name][i+1][iy,ix]
                        motion[name].append(dict(seed=seed,pair=i,samples=int(mask.sum()),error_delta=float(np.abs(diff[mask]).mean())))
            results['clean' if noise==0 else 'noisy']={n:dict(image=summarize(rows[n]),temporal=summarize(motion[n],'samples'),
                frames=rows[n],pairs=motion[n]) for n in rows}
    candidate='plane_residual' if args.plane_residual else 'fitted'
    gates=dict(noisy_motion_improves_20pct=results['noisy'][candidate]['temporal']['error_delta']<=.8*results['noisy']['direct']['temporal']['error_delta'],
        clean_mae_preserved=results['clean'][candidate]['image']['mae']<=1.05*results['clean']['direct']['image']['mae'])
    result=dict(protocol=protocol,results=results,gates=gates,game_integration_allowed=False)
    (out/'metrics.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    files=['_IA_Python/neural_lighting_scene.py','_IA_Python/neural_lighting_diagnostics.py','tools/diagnose_fitted_normals.py',
           'tools/diagnose_neural_lighting.py','tests/test_neural_lighting_diagnostics.py']
    (out/'source-manifest.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(summary={case:{n:dict(image=v['image'],temporal=v['temporal']) for n,v in modes.items()} for case,modes in results.items()},gates=gates),indent=2))


if __name__=='__main__': main()
