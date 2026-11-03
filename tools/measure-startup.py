#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""How long Kalipso takes to start: from launching the program until its window is up and its event
loop runs (`--demo --quit-after 1` quits at the first turn of the loop), over several runs, with a
private profile. Reports the median (p50) and the 95th percentile (p95) in milliseconds as JSON.

  measure-startup.py <kalipso binary> [--runs N] [--budget-p95 MS]

With --budget-p95, exits 1 when p95 is over the budget (a regression check for CI).
The first run warms the disk cache and is not counted. Uses QT_QPA_PLATFORM=offscreen unless set.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time


def percentile(values, fraction):
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('binary')
    parser.add_argument('--runs', type=int, default=20)
    parser.add_argument('--budget-p95', type=float)
    args = parser.parse_args()
    times = []
    with tempfile.TemporaryDirectory() as home:
        env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=f'{home}/config', XDG_DATA_HOME=f'{home}/data',
                   XDG_CACHE_HOME=f'{home}/cache')
        env.setdefault('QT_QPA_PLATFORM', 'offscreen')
        for run in range(args.runs + 1):
            start = time.perf_counter()
            subprocess.run([args.binary, '--demo', '--quit-after', '1'], env=env, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
            if run:
                times.append((time.perf_counter() - start) * 1000)
    report = {'runs': len(times), 'p50_ms': round(statistics.median(times), 1),
              'p95_ms': round(percentile(times, 0.95), 1), 'min_ms': round(min(times), 1),
              'max_ms': round(max(times), 1), 'platform': os.environ.get('QT_QPA_PLATFORM', 'offscreen')}
    if args.budget_p95 is not None:
        report['budget_p95_ms'] = args.budget_p95
        report['within_budget'] = report['p95_ms'] <= args.budget_p95
    print(json.dumps(report, indent=2))
    return 0 if report.get('within_budget', True) else 1


if __name__ == '__main__':
    sys.exit(main())
