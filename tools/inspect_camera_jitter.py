"""Analyze saved camera/XeSS jitter pairs; sequence fits are not frame identity."""
import argparse
import json
import re
from pathlib import Path

import numpy as np


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def radical_inverse(count, base):
    indices = np.arange(count, dtype=np.int64)
    result = np.zeros(count, np.float64)
    factor = 1./base
    while np.any(indices):
        result += (indices % base)*factor
        indices //= base
        factor /= base
    return result


def inspect(log):
    lines = log.read_text(encoding='utf-8-sig').splitlines()
    inputs = {int(r['call']): r for r in map(fields, (l for l in lines if 'TSR relighting bridge:' in l))}
    pairs = []
    for line in lines:
        if 'TSR relighting camera candidate:' not in line:
            continue
        r = fields(line)
        if r['projection_valid'] != 'true':
            continue
        call = int(r['call'])
        xess = np.array([float(v) for v in inputs[call]['jitter'].split(',')])
        delta = np.array([float(v) for v in r['jitter_delta'].split(',')])
        pairs.append(dict(call=call, camera_frame=int(r['frame']), xess_jitter=xess.tolist(),
                          camera_jitter=(xess-delta).tolist(), delta=delta.tolist()))
    # Examine both axis order/sign conventions without silently choosing one.
    sequences = {base: radical_inverse(65536, base)-.5 for base in (2, 3)}
    fits = []
    for bx, by in ((2, 3), (3, 2)):
        for sx in (-1, 1):
            for sy in (-1, 1):
                sequence = np.stack([sx*sequences[bx], sy*sequences[by]], -1)
                matches = []
                for pair in pairs:
                    indices = []
                    for key in ('camera_jitter', 'xess_jitter'):
                        candidates = np.flatnonzero(np.max(np.abs(sequence-np.array(pair[key])), -1) < 2e-6)
                        indices.append(int(candidates[0]) if len(candidates) == 1 else None)
                    if all(i is not None for i in indices):
                        matches.append(dict(call=pair['call'], camera_index=indices[0], xess_index=indices[1],
                                            xess_minus_camera=indices[1]-indices[0]))
                if matches:
                    fits.append(dict(x_base=bx, y_base=by, x_sign=sx, y_sign=sy, matching_pairs=len(matches), matches=matches))
    fits.sort(key=lambda f: f['matching_pairs'], reverse=True)
    return dict(valid_projection_pairs=pairs, sequence_hypotheses=fits,
                tolerance=2e-6, tested_indices=[0, 65535],
                limitation='A Halton fit describes numeric phase only; it does not prove resource/frame/viewport identity or authorize inference.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.log)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(pair_count=len(result['valid_projection_pairs']),
                          hypotheses=[{k: v for k, v in f.items() if k != 'matches'} for f in result['sequence_hypotheses']],
                          best_matches=result['sequence_hypotheses'][0]['matches'] if result['sequence_hypotheses'] else []), indent=2))
