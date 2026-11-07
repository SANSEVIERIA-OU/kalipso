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
