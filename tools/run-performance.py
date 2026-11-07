#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Sequential native benchmarks. Models, audio, logs and results stay outside Git.

Generate speech with tools/make-speech-fixtures.py first. Each repetition is a fresh process;
downloads and one warm-up per recognizer are outside measurements. OS caches are not flushed.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def output(command):
    return subprocess.check_output(command, cwd=ROOT, text=True, timeout=30).strip()


def machine():
    if sys.platform == 'linux':
        hardware = json.loads(output(['lscpu', '-J']))
    elif sys.platform == 'darwin':
        hardware = output(['sysctl', '-n', 'machdep.cpu.brand_string', 'hw.memsize', 'hw.ncpu'])
    else:
        hardware = json.loads(output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json']))
    return {'os': platform.platform(), 'architecture': platform.machine(), 'logical_cpus': os.cpu_count(),
            'hardware': hardware, 'python': platform.python_version(),
            'runner_os': os.getenv('RUNNER_OS'), 'runner_arch': os.getenv('RUNNER_ARCH'),
            'runner_image': os.getenv('ImageVersion'), 'qt_platform_override': os.getenv('QT_QPA_PLATFORM'),
            'load_average_before': list(os.getloadavg()) if hasattr(os, 'getloadavg') else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build'))
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-root', type=Path, default=Path('artifacts/performance-models'))
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--seconds', type=int, default=10)
    parser.add_argument('--skip-speech', action='store_true')
    args = parser.parse_args()
    if args.repeats < 1 or args.seconds < 5:
        parser.error('positive repeats and at least five seconds are required')
    args.output.mkdir(parents=True, exist_ok=True)
    if (args.output / 'manifest.json').exists():
        parser.error('use a fresh output directory; results from different runs must not be mixed')
    build = args.build.resolve()
    binary_dir = build / 'Release' if (build / 'Release').exists() else build
    suffix = '.exe' if sys.platform == 'win32' else ''
    binary = lambda name: str(binary_dir / (name + suffix))
    app = str(build / 'kalipso.app/Contents/MacOS/kalipso') if sys.platform == 'darwin' else binary('kalipso')
    diff = subprocess.check_output(['git', 'diff', 'HEAD'], cwd=ROOT)
    manifest = {'schema': 1, 'started_utc': datetime.now(timezone.utc).isoformat(),
                'commit': output(['git', 'rev-parse', 'HEAD']), 'tracked_diff_sha256': hashlib.sha256(diff).hexdigest(),
                'working_tree_status': output(['git', 'status', '--short']), 'machine': machine(),
                'repeats': args.repeats, 'seconds_per_resource_run': args.seconds,
                'audio': 'synthetic espeak-ng; PCM16 mono 16 kHz; no physical microphone',
                'runs': [], 'omitted': ['Physical microphone and compositor/display latency',
                                      'Plasma/GNOME shell cost, GPU/Wayland, multi-monitor and energy']}
    cache = build / 'CMakeCache.txt'
    if cache.exists():
        manifest['build'] = [line for line in cache.read_text().splitlines()
                             if line.startswith(('CMAKE_BUILD_TYPE:', 'CMAKE_CXX_COMPILER:', 'CMAKE_CXX_FLAGS', 'Qt6_DIR:'))]
    manifest_path = args.output / 'manifest.json'

    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')

    def run(name, command, timeout=180, json_output=True):
        print(name, flush=True)
        with (args.output / (name + '.stderr.log')).open('w', encoding='utf-8') as errors:
            result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                    stderr=errors, timeout=timeout)
        (args.output / (name + '.json' if json_output else name + '.txt')).write_text(result.stdout, encoding='utf-8')
        manifest['runs'].append({'name': name, 'command': command, 'returncode': result.returncode})
        save()
        result.check_returncode()
        return json.loads(result.stdout) if json_output else None

    save()
    # Native platform by default; Linux caller explicitly supplies Xvfb/xcb.
    env_platform = 'windows' if sys.platform == 'win32' else 'cocoa' if sys.platform == 'darwin' else 'xcb'
    os.environ.setdefault('QT_QPA_PLATFORM', env_platform)
    run('startup', [sys.executable, str(ROOT / 'tools/measure-startup.py'), app, '--runs', '20', '--budget-p95', '2000'])
    for repeat in range(args.repeats):
        run(f'tracker-{repeat}', [binary('tracker_benchmark')])
        modes = ['editor', 'reader', 'steady', 'interaction']
        if sys.platform != 'darwin':
            modes.append('colors')
        for mode in modes:
            command = [binary('ui_benchmark'), mode, str(args.seconds)]
            if repeat == 0:
                command.append(str((args.output / (mode + '.png')).resolve()))
            run(f'ui-{mode}-{repeat}', command)
    if sys.platform == 'darwin':
        manifest['omitted'].append('macOS color capture: requires interactive screen-recording permission')
    if args.skip_speech:
        manifest['omitted'].append('Speech explicitly skipped')
    else:
        for backend, language in [('vosk', 'it'), ('moonshine', 'en')]:
            command = [binary('speech_benchmark'), backend, language, str(args.fixtures.resolve()), str(args.runtime_root.resolve())]
            run(f'{backend}-prepare', command + ['prepare'], timeout=360, json_output=False)
            run(f'{backend}-warmup', command + ['batch'])
            for repeat in range(args.repeats):
                for mode in ['batch', 'paced']:
                    run(f'speech-{backend}-{mode}-{repeat}', command + [mode])
    manifest['finished_utc'] = datetime.now(timezone.utc).isoformat()
    save()


if __name__ == '__main__':
    main()
