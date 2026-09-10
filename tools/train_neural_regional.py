"""Equal-budget training through the regional chain, with frozen references."""
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
from neural_regional_training import TRAIN_IDS,VALID_IDS,TEST_IDS,TRAIN_SIZES,EVAL_SIZES,FAMILIES,quartet,regional_losses
from neural_regional_inference import predict_regional
from neural_generalization_scene import make_frame,YAWS
from neural_lighting_model import LightingNet
from neural_lighting_scene import correspondences
from neural_lighting_diagnostics import response_metrics
from neural_balance_selection import SEEDS,fidelity_metrics
from diagnose_neural_generalization import aggregate,gates,write_json
from train_neural_joint import hash_tensors


def evaluate(models,ids,out,label,preview=False):
    groups={}; digest=hashlib.sha256()
    canvas=Image.new('RGB',(6*192,2*152),'#18212b'); draw=ImageDraw.Draw(canvas)
    with torch.inference_mode():
        for fi,family in enumerate(FAMILIES):
            for width,height in EVAL_SIZES:
                for condition,noise in (('clean',0.),('noisy',.002)):
                    key=f'{family}_{width}x{height}_{condition}'
                    rows={n:dict(images=[],response=[],temporal=[]) for n in models}
                    for scene_id in ids:
                        centers=[]; center_predictions=[]
                        for remove in (False,True):
                            frames=[]; preds={n:[] for n in models}
                            for yaw in YAWS:
                                scene,cam=make_frame(scene_id,family,width,height,yaw,remove,noise=noise)
                                digest.update(str((scene_id,family,width,height,yaw,remove,noise)).encode())
                                for k in ('features','source','target','depth','object_id'): digest.update(scene[k].tobytes())
                                batch=tensors({k:scene[k] for k in ('features','source','valid')})
                                for name,model in models.items():
                                    pred=scene['source'] if model is None else predict_regional(model,batch)[0].permute(1,2,0).numpy()
                                    preds[name].append(pred)
                                    if yaw==0.:
                                        error=np.abs(pred-scene['target']); shadow=scene['shadow_effect']>.005
                                        rows[name]['images'].append(dict(seed=scene_id,remove=remove,image_mae=float(error.mean()),
                                            protected_change=float(np.abs(pred-scene['source'])[scene['valid']==0].max()),
                                            shadow_abs_sum=float(error[shadow].sum()),shadow_samples=int(shadow.sum())*3,
                                            **fidelity_metrics(pred,scene)))
                                frames.append((scene,cam))
                            for i in range(2):
                                iy,ix,mask=correspondences(frames[i][0],frames[i+1][0],frames[i+1][1])
                                if not mask.any(): raise RuntimeError('No temporal correspondence')
                                for name in models:
                                    ea=preds[name][i]-frames[i][0]['target']; eb=preds[name][i+1]-frames[i+1][0]['target']
                                    rows[name]['temporal'].append(dict(seed=scene_id,remove=remove,pair=i,samples=int(mask.sum()),
                                        error=float(np.abs(ea-eb[iy,ix])[mask].mean())))
                            centers.append(frames[1][0]); center_predictions.append({n:p[1] for n,p in preds.items()})
                        for name in models:
                            row,*_=response_metrics(center_predictions[0][name],center_predictions[1][name],*centers)
                            rows[name]['response'].append(dict(seed=scene_id,**row))
                        if preview and scene_id==ids[0] and width==384 and condition=='clean':
                            values=[centers[0]['source'],centers[0]['target']]+[
                                center_predictions[0][f'{s}_{v}'] for s in SEEDS for v in ('frozen','joint')]
                            for col,(title,value) in enumerate(zip(('Entrada','Referencia','Anterior A','Treinada A','Anterior B','Treinada B'),values)):
                                rgb=value.clip(0); rgb=(rgb/(1+rgb))**(1/2.2)
                                tile=Image.fromarray(np.uint8(np.round(rgb*255))).resize((192,128),Image.Resampling.NEAREST)
                                canvas.paste(tile,(col*192,fi*152+24)); draw.text((col*192+2,fi*152+4),title,fill='white')
                    summary={n:aggregate(r) for n,r in rows.items()}
                    assessment={str(s):{v:gates(summary[f'{s}_joint'],summary[f'{s}_{v}']) for v in ('frozen','control')} for s in SEEDS}
                    groups[key]=dict(summary=summary,assessment=assessment,rows=rows)
                    write_json(out/(label+'_'+key+'.json'),groups[key])
                    print(label,key,{s:{v:a['status'] for v,a in refs.items()} for s,refs in assessment.items()},flush=True)
    passed=all(a['status']=='passed' for g in groups.values() for refs in g['assessment'].values() for a in refs.values())
    result=dict(groups=groups,inputs_sha256=digest.hexdigest(),all_checks_passed=passed)
    write_json(out/(label+'.json'),result)
    if preview: canvas.save(out/'comparison.png')
    return result


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/neural-regional-training-v1')
    parser.add_argument('--steps',type=int,default=1200)
    args=parser.parse_args()
    if args.steps<1: parser.error('steps must be positive')
    args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    source=ROOT/'artifacts/neural-balance-v1'
    expected=json.loads((source/'training.json').read_text(encoding='utf-8'))
    models={'identity':None}; checkpoint_hashes={}
    for seed in SEEDS:
        path=source/f'{seed}_t025.pt'; digest=hashlib.sha256(path.read_bytes()).hexdigest()
        if digest!=expected[f'{seed}_t025']['sha256']: raise RuntimeError('Checkpoint mismatch')
        model=LightingNet(); model.load_state_dict(torch.load(path,map_location='cpu',weights_only=True)); model.eval()
        checkpoint_hashes[str(seed)]=digest
        models[f'{seed}_frozen']=model
        for variant in ('control','joint'): models[f'{seed}_{variant}']=copy.deepcopy(model)
    protocol=dict(train=list(TRAIN_IDS),validation=list(VALID_IDS),test=list(TEST_IDS),families=FAMILIES,
        train_sizes=TRAIN_SIZES,evaluation_sizes=EVAL_SIZES,train_depth_noise=.0005,evaluation_depth_noise=[0.,.002],
        starting_checkpoints=checkpoint_hashes,steps=args.steps,learning_rate=.0005,batch_seed=984,
        batch='one quartet, four full raster frames; same schedule for all four trained models',
        camera_pairs='(-.10,-.05) for even scene IDs; (.05,.10) for odd scene IDs; paired views within each static geometry',
        intervention='remove_all only; no animated object or motion-vector supervision',
        reconstruction='unchanged predict_regional: internal height 64, bilinear residual on original high resolution color',
        common_loss='original image + response + 0.1 * geometry-boundary MAE + 0.2 * stable receiver error difference',
        joint_loss='common loss + 0.5 * camera reprojection error; control has coefficient zero',
        references='frozen t025 using same regional path; equal-budget control isolates added camera loss',
        selection='fixed final step, fixed coefficients, no checkpoint/hyperparameter selection using validation or test',
        gates='all generalization gates versus BOTH frozen and control, for each family/size/noise/initialization; all validation and test groups required',
        limits=dict(image=1.05,response=1.05,response_zero=.8,temporal=.8,unchanged=.003,shadow=1.05,edge=1.05,gradient=1.05),
        game_integration_allowed=False)
    write_json(args.out/'protocol.json',protocol)
    paths=['tools/train_neural_regional.py','_IA_Python/neural_regional_training.py','_IA_Python/neural_regional_inference.py',
           '_IA_Python/neural_generalization_scene.py','_IA_Python/neural_scene_geometry.py','_IA_Python/neural_lighting_scene.py',
           '_IA_Python/neural_lighting_model.py','_IA_Python/neural_joint_training.py','_IA_Python/neural_lighting_temporal.py',
           '_IA_Python/neural_intervention_training.py','_IA_Python/neural_lighting_diagnostics.py','_IA_Python/neural_balance_selection.py',
           '_IA_Python/bilateral_experiment.py','tools/diagnose_neural_generalization.py','tools/train_neural_interventions.py',
           'tools/train_neural_joint.py','tests/test_neural_regional_training.py']
    write_json(args.out/'source-manifest.json',{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    examples=[]; digest=hashlib.sha256()
    for count,seed in enumerate(TRAIN_IDS,1):
        for family in FAMILIES:
            for width,height in TRAIN_SIZES:
                example=quartet(seed,family,width,height); examples.append(example)
                digest.update(str((seed,family,width,height)).encode()); hash_tensors(digest,example)
        if count%8==0: print('dataset',count,'/',len(TRAIN_IDS),flush=True)
    schedule=np.random.default_rng(984).integers(len(examples),size=args.steps)
    write_json(args.out/'dataset.json',dict(quartets=len(examples),sha256=digest.hexdigest(),schedule_sha256=hashlib.sha256(schedule.tobytes()).hexdigest()))
    training={}
    for seed in SEEDS:
        for variant in ('control','joint'):
            name=f'{seed}_{variant}'; model=models[name]; model.train()
            optimizer=torch.optim.Adam(model.parameters(),lr=.0005)
            start=time.perf_counter(); curve=[]
            for step,index in enumerate(schedule,1):
                example=examples[index]; optimizer.zero_grad(set_to_none=True)
                pred=predict_regional(model,example[0])
                image,response,temporal,edge,stable=regional_losses(pred,example)
                objective=image+response+.1*edge+.2*stable+(temporal*.5 if variant=='joint' else 0.)
                if not torch.isfinite(objective): raise RuntimeError('Nonfinite objective')
                objective.backward(); optimizer.step()
                if step==1 or step%300==0:
                    curve.append(dict(step=step,image=float(image.detach()),response=float(response.detach()),
                        temporal=float(temporal.detach()),edge=float(edge.detach()),stable=float(stable.detach())))
                    print(name,curve[-1],flush=True)
            model.eval(); path=args.out/(name+'.pt'); torch.save(model.state_dict(),path)
            training[name]=dict(cpu_seconds=time.perf_counter()-start,curve=curve,parameters=sum(p.numel() for p in model.parameters()),
                                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            write_json(args.out/'training.json',training)
    del examples
    validation=evaluate(models,VALID_IDS,args.out,'validation')
    heldout=evaluate(models,TEST_IDS,args.out,'held_out',preview=True)
    result=dict(protocol=protocol,training=training,training_data_sha256=digest.hexdigest(),
        schedule_sha256=hashlib.sha256(schedule.tobytes()).hexdigest(),
        validation_passed=validation['all_checks_passed'],held_out_passed=heldout['all_checks_passed'],
        synthetic_gates_passed=validation['all_checks_passed'] and heldout['all_checks_passed'],game_integration_allowed=False,
        torch=torch.__version__,limitations=['32 new training scenes, six validation and eight test scenes; restricted diffuse lights/materials.',
            'Remove-all supervision only; single-object relocation and variable source illumination/distance remain untested here.',
            'Bilinear reconstruction unchanged; no geometry-aware interpolation, inference history or game integration.',
            '384x256 extrapolates beyond trained raster sizes but remains below actual game input resolution.',
            'CPU FP32 training/evaluation; no FP16 or GPU latency/VRAM validation.'])
    write_json(args.out/'metrics.json',result)
    print(json.dumps(dict(validation_passed=result['validation_passed'],held_out_passed=result['held_out_passed']),indent=2),flush=True)


if __name__=='__main__': main()
