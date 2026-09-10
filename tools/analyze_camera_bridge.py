"""Summarize sampled camera metadata without treating it as frame association."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


def graphical_interruptions(rows):
    """Active-state changes are logged even between the periodic samples.

    Count completed interruptions within monotonic call counters. The reason is
    the one at entry; metadata does not prove what the user saw on screen.
    """
    previous = None
    start = None
    episodes = []
    restarts = 0
    for row in rows:
        call = int(row.get('call', 0))
        if previous and call <= int(previous.get('call', 0)):
            restarts += 1
            start = None
            previous = None
        active = row.get('active') == 'true'
        if previous and previous.get('active') == 'true' and not active:
            start = row
        elif active and start is not None:
            episodes.append(dict(reason=start.get('reason', 'missing'),
                                 first_call=int(start['call']),
                                 resumed_call=call,
                                 bypass_calls=call-int(start['call'])))
            start = None
        previous = row
    automatic = [r for r in episodes if r['reason'] != 'disabled']
    return dict(completed_episodes=len(episodes), counter_restarts=restarts,
                control_disabled_episodes=sum(r['reason'] == 'disabled' for r in episodes),
                automatic_episodes=len(automatic),
                automatic_bypass_calls=sum(r['bypass_calls'] for r in automatic),
                automatic_lengths=dict(Counter(r['bypass_calls'] for r in automatic)),
                automatic_reasons=dict(Counter(r['reason'] for r in automatic)),
                episodes=episodes,
                limitation='Entry reason and call-count duration; not visual artifact attribution. Initial/final unmatched intervals excluded.')


def analyze(text):
    lines = text.splitlines()
    rows = lambda tag: [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in lines if tag in line]
    cameras = rows('TSR camera observed:')
    inputs = rows('TSR relighting bridge:')
    candidates = rows('TSR relighting camera candidate:')
    hooks = rows('TSR camera hooks committed:')
    history = rows('TSR camera history:')
    graphical = rows('TSR graphical relighting:')
    submission_hooks = rows('TSR submission hooks:')
    active = [r for r in graphical if r.get('active') == 'true']
    gpu_ms = [float(r['gpu_ms']) for r in active if float(r.get('gpu_ms', -1)) >= 0]
    ages = [int(c['age_ms']) for c in candidates if 'age_ms' in c]
    matched_ages = [int(c['age_ms']) for c in history if c.get('status') == 'unique_candidate' and 'age_ms' in c]
    return dict(camera_log_samples=len(cameras), input_log_samples=len(inputs), candidate_log_samples=len(candidates),
                hook_commit_records=hooks,
                history_log_samples=len(history),
                history_status=dict(Counter(c.get('status', 'missing') for c in history)),
                unique_history_serial_lags=dict(Counter(c.get('serial_lag', 'missing') for c in history
                                                       if c.get('status') == 'unique_candidate')),
                median_matched_history_age_ms=statistics.median(matched_ages) if matched_ages else None,
                max_matched_history_age_ms=max(matched_ages) if matched_ages else None,
                camera_sources=dict(Counter(c.get('source', 'missing') for c in cameras)),
                camera_viewports=sorted({c.get('viewport', 'missing') for c in cameras}),
                projection_status=dict(Counter(c.get('projection_valid', 'missing') for c in candidates)),
                basis_status=dict(Counter(c.get('basis_valid', 'missing') for c in candidates)),
                median_candidate_age_ms=statistics.median(ages) if ages else None,
                max_candidate_age_ms=max(ages) if ages else None,
                errors=[line for line in lines if '[E]' in line],
                neural_active=bool(active),
                graphical_log_samples=len(graphical),
                submission_hook_records=submission_hooks,
                graphical_active_samples=len(active),
                graphical_reasons=dict(Counter(r.get('reason', 'missing') for r in graphical)),
                recorded_frames=max((int(r.get('recorded_frames', 0)) for r in graphical), default=0),
                sampled_gpu_pass_median_ms=statistics.median(gpu_ms) if gpu_ms else None,
                sampled_gpu_pass_p95_ms=sorted(gpu_ms)[math.ceil(len(gpu_ms)*.95)-1] if gpu_ms else None,
                sampled_gpu_pass_max_ms=max(gpu_ms) if gpu_ms else None,
                interruptions=graphical_interruptions(graphical),
                gpu_timing_scope='Last completed own pre-FSR pass; sparse samples, not total frame time or input latency.',
                limitation='Sampled metadata only: freshness/jitter agreement does not prove frame or viewport association.',
                next_check=('Compare F8 on/off visually; inspect pass activity, fallback churn, errors and completed GPU timing. Camera association remains experimental.'
                            if graphical else 'Inspect camera projection/basis, timestamp ordering and jitter against XeSS input; establish an explicit association before inference.'
                            if candidates else ('Hooks were reported committed but no camera candidate was sampled; verify callbacks and game path.'
                                                if hooks else 'No hook-commit record; verify hook installation before concluding that the game omits camera data.')))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    data = args.log.read_bytes()
    report = analyze(data.decode('utf-8-sig', errors='replace'))
    report['sha256'] = hashlib.sha256(data).hexdigest()
    result = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(result, encoding='utf-8')
    print(result)
