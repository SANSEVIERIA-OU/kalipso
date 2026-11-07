#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Counterchecks: absent/invalid measurements and deliberate regressions cannot pass."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('performance', Path(__file__).resolve().parents[1] / 'tools/performance-report.py')
performance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(performance)


class PerformanceMetricsTests(unittest.TestCase):
    def complete_run(self, folder):
        names = ['startup']
        names += [f'ui-{mode}-{i}' for mode in ['editor', 'reader', 'steady', 'interaction'] for i in range(3)]
        names += [f'speech-{backend}-{mode}-{i}' for backend in ['vosk', 'moonshine']
                  for mode in ['batch', 'paced'] for i in range(3)]
        (folder / 'manifest.json').write_text(json.dumps({
            'finished_utc': '2026-11-07T00:00:00Z', 'repeats': 3,
            'runs': [{'name': name, 'returncode': 0} for name in names],
            'machine': {'os': 'macOS-test'}, 'commit': 'test', 'omitted': []}))
        for name in names:
            group = 'speech' if name.startswith('speech-') else name.rsplit('-', 1)[0] if name != 'startup' else name
            row = {key: ceiling / 2 for key, ceiling in performance.BUDGETS[group].items()}
            row['passed'] = True
            if name == 'startup':
                row['p95_ms'] = 2500
            (folder / (name + '.json')).write_text(json.dumps(row))

    def test_explicit_startup_exception_preserves_violation(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.complete_run(folder)
            self.assertFalse(performance.summarize(folder)['accepted'])
            report = performance.summarize(folder, True)
            self.assertTrue(report['accepted'])
            self.assertFalse(report['within_budgets'])
            self.assertEqual(report['budget_errors'], report['waived_budget_errors'])
            self.assertEqual(len(report['waived_budget_errors']), 1)

    def test_startup_exception_cannot_hide_other_failures(self):
        cases = [('startup.json', 'p95_ms', float('nan')),
                 ('startup.json', 'p95_ms', None),
                 ('startup.json', 'passed', False),
                 ('ui-editor-0.json', 'cpu_percent_one_core', 6),
                 ('speech-moonshine-paced-0.json', 'passed', False)]
        for filename, key, value in cases:
            with self.subTest(filename=filename, key=key, value=value), tempfile.TemporaryDirectory() as temporary:
                folder = Path(temporary)
                self.complete_run(folder)
                path = folder / filename
                row = json.loads(path.read_text())
                row[key] = value
                path.write_text(json.dumps(row))
                self.assertFalse(performance.summarize(folder, True)['accepted'])

    def test_startup_exception_rejects_failed_commands_and_incomplete_runs(self):
        for kind in ['command', 'missing', 'unfinished', 'other-platform']:
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                folder = Path(temporary)
                self.complete_run(folder)
                path = folder / 'manifest.json'
                manifest = json.loads(path.read_text())
                if kind == 'command':
                    manifest['runs'][0]['returncode'] = 1
                elif kind == 'missing':
                    (folder / 'ui-reader-0.json').unlink()
                elif kind == 'unfinished':
                    del manifest['finished_utc']
                else:
                    manifest['machine']['os'] = 'Linux'
                path.write_text(json.dumps(manifest))
                self.assertFalse(performance.summarize(folder, True)['accepted'])

    def test_real_time_capacity_and_regression(self):
        self.assertEqual(performance.check_metrics('speech', {'decode_rtf': .3}, {'decode_rtf': .9}), [])
        self.assertTrue(performance.check_metrics('speech', {'decode_rtf': 1.2}, {'decode_rtf': .9}))

    def test_invalid_or_missing_is_not_zero(self):
        for value in [None, -1, float('nan'), float('inf'), True, 'fast']:
            with self.subTest(value=value):
                self.assertTrue(performance.check_metrics('ui', {'p95': value}, {'p95': 100}))
        self.assertTrue(performance.check_metrics('ui', {}, {'p95': 100}))

    def test_failed_workload_is_not_fast(self):
        self.assertTrue(performance.check_metrics('colors', {'passed': False, 'cpu': 0}, {'cpu': 5}))

    def test_partial_run_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / 'manifest.json').write_text(json.dumps({
                'repeats': 1, 'runs': [], 'machine': {'os': 'Linux'}, 'commit': 'test', 'omitted': []}))
            report = performance.summarize(folder)
            self.assertFalse(report['within_budgets'])
            self.assertTrue(any('missing/invalid' in error for error in report['budget_errors']))


if __name__ == '__main__':
    unittest.main()
