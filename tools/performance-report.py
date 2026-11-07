#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Summarize repeated measurements; --check enforces conservative regression ceilings.

Budgets are laboratory guards, not performance promises for arbitrary hardware. See docs/performance.md.
An incomplete run, missing repetition or non-finite measurement must never appear as a pass.
"""
import argparse
import json
import math
from pathlib import Path
import statistics
import sys

# Ceilings are deliberately wider than the measured Linux baseline; validate each native lane.
BUDGETS = {
    'startup': {'p95_ms': 2000},
    'ui-interaction': {'interaction_p95_ms': 100},
    'ui-editor': {'cpu_percent_one_core': 5, 'median_rss_kib': 256 * 1024},
    'ui-reader': {'cpu_percent_one_core': 5, 'median_rss_kib': 256 * 1024},
    'ui-steady': {'cpu_percent_one_core': 15, 'median_rss_kib': 256 * 1024},
    'ui-colors': {'cpu_percent_one_core': 10, 'median_rss_kib': 256 * 1024},
    'speech': {'model_load_ms': 10000, 'peak_rss_kib': 1024 * 1024, 'decode_rtf': .9},
}


def check_metrics(name, row, ceilings):
    errors = []
    if row.get('passed') is False:
        errors.append(f'{name}: workload did not succeed')
    for key, ceiling in ceilings.items():
        value = row.get(key)
        if not isinstance(value, (float, int)) or isinstance(value, bool) or not math.isfinite(value) or value < 0:
            errors.append(f'{name}: invalid or missing {key}: {value}')
        elif value > ceiling:
            errors.append(f'{name}: {key}={value:.3f} exceeds {ceiling}')
    return errors


def summarize(folder):
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    errors = []
    if not manifest.get('finished_utc') or manifest['repeats'] < 3:
        errors.append('A completed run with at least three repetitions is required')
    if any(run['returncode'] for run in manifest['runs']):
        errors.append('One or more commands failed')
    groups = {'startup': ['startup']}
    repeats = manifest['repeats']
    modes = ['editor', 'reader', 'steady', 'interaction']
    if manifest['machine']['os'].startswith('macOS'):
        pass  # Permission-dependent capture is explicitly omitted on macOS runners.
    else:
        modes.append('colors')
    for mode in modes:
        groups[f'ui-{mode}'] = [f'ui-{mode}-{i}' for i in range(repeats)]
    for backend in ['vosk', 'moonshine']:
        for mode in ['batch', 'paced']:
            groups[f'speech-{backend}-{mode}'] = [f'speech-{backend}-{mode}-{i}' for i in range(repeats)]
    results = {}
    for group, names in groups.items():
        rows = []
        for name in names:
            try:
                row = json.loads((folder / (name + '.json')).read_text(encoding='utf-8'))
            except (OSError, ValueError) as exc:
                errors.append(f'{name}: missing/invalid result ({exc})')
                continue
            ceilings = BUDGETS['speech'] if group.startswith('speech-') else BUDGETS[group]
            errors.extend(check_metrics(name, row, ceilings))
            if group.startswith('speech-') and row.get('passed') is not True:
                errors.append(f'{name}: successful speech recognition required')
            rows.append(row)
        if not rows:
            continue
        keys = [key for key, value in rows[0].items()
                if isinstance(value, (int, float)) and not isinstance(value, bool)]
        results[group] = {}
        for key in keys:
            values = [row.get(key) for row in rows]
            if any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
                errors.append(f'{group}: invalid {key}')
                continue
            results[group][key] = {'min': min(values), 'median': statistics.median(values), 'max': max(values)}
    return {'commit': manifest['commit'], 'machine': manifest['machine'], 'groups': results,
            'omitted': manifest['omitted'], 'budget_errors': errors, 'within_budgets': not errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    report = summarize(args.folder)
    print(json.dumps(report, indent=2))
    return int(args.check and not report['within_budgets'])


if __name__ == '__main__':
    sys.exit(main())
