"""Validation-only selection with per-initialization quality constraints."""
import numpy as np

VARIANTS = {'paired': 0., 't025': .25, 't050': .5, 't100': 1.}
SEEDS = (20260911, 20260912)
VALID_IDS = tuple(range(19000, 19016))
TEST_IDS = tuple(range(20000, 20024))


def model_name(seed, variant):
    return f'{seed}_{variant}'


def fidelity_metrics(pred, scene):
    error = pred - scene['target']
    valid = scene['valid'] > 0
    ids = scene['object_id']
    boundary = np.zeros_like(valid)
    horizontal = ids[:, 1:] != ids[:, :-1]
    vertical = ids[1:] != ids[:-1]
    boundary[:, 1:] |= horizontal; boundary[:, :-1] |= horizontal
    boundary[1:] |= vertical; boundary[:-1] |= vertical
    boundary &= valid
    gradient_sum = 0.; samples = 0
    for axis in (0, 1):
        adjacent = (valid[1:] & valid[:-1]) if axis == 0 else (valid[:, 1:] & valid[:, :-1])
        delta = np.abs(np.diff(error, axis=axis))
        gradient_sum += float(delta[adjacent].sum())
        samples += int(adjacent.sum()) * 3
    return dict(edge_abs_sum=float(np.abs(error[boundary]).sum()), edge_samples=int(boundary.sum())*3,
                gradient_abs_sum=gradient_sum, gradient_samples=samples)


def comparisons(quality, motion, detail, variant, seeds=SEEDS):
    rows = []
    for seed in seeds:
        name, control = model_name(seed, variant), model_name(seed, 'paired')
        for condition in ('clean', 'noisy'):
            q, c = quality[name][condition], quality[control][condition]
            d, cd = detail[name][condition], detail[control][condition]
            def ratio(a, b):
                return float(a / max(b, 1e-12))
            # Values <= 1 pass. Each initialization and noise condition must pass;
            # averaging cannot conceal one initialization violating a constraint.
            limits = dict(
                response_zero=ratio(q['response']['response_mae'], .8*q['response']['zero_response_mae']),
                response=ratio(q['response']['response_mae'], 1.05*c['response']['response_mae']),
                image=ratio(q['image_mae'], 1.05*c['image_mae']),
                temporal=ratio(motion[name][condition]['mean'], .8*motion[control][condition]['mean']),
                unchanged=ratio(q['response']['unchanged_drift'], .003),
                protected=0. if q['protected_max_change'] == 0 else float('inf'),
                shadow=ratio(q['shadow_mae'], 1.05*c['shadow_mae']),
                edge=ratio(d['edge_mae'], 1.05*cd['edge_mae']),
                gradient=ratio(d['gradient_mae'], 1.05*cd['gradient_mae']))
            if not all(np.isfinite(v) for k, v in limits.items() if k != 'protected'):
                raise ValueError('Nonfinite validation metric')
            rows.append(dict(seed=seed, condition=condition, limits=limits,
                             image_ratio=ratio(q['image_mae'], c['image_mae'])))
    gates = {k: all(r['limits'][k] <= 1 for r in rows) for k in rows[0]['limits']}
    return dict(rows=rows, gates=gates, passed=all(gates.values()),
                worst_violation=max(v for r in rows for v in r['limits'].values()),
                mean_image_ratio=float(np.mean([r['image_ratio'] for r in rows])))


def select_validation(reports):
    eligible = [n for n, r in reports.items() if r['passed']]
    if eligible:
        chosen = min(eligible, key=lambda n: (reports[n]['mean_image_ratio'], n))
    else:
        # Still freeze one diagnostic candidate before the final test. Passing
        # that test cannot turn a failed validation selection into approval.
        chosen = min(reports, key=lambda n: (reports[n]['worst_violation'], reports[n]['mean_image_ratio'], n))
    return dict(variant=chosen, validation_eligible=bool(eligible), reports=reports,
                reason='lowest image error among eligible' if eligible else 'diagnostic only: smallest worst constraint violation')
