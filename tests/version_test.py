# SPDX-License-Identifier: AGPL-3.0-or-later
"""The version scheme: one step at a time, and every file states the same version."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('version', Path(__file__).resolve().parent.parent / 'tools/version.py')
version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version)

steps = ['0.1']
while steps[-1] != '2.1':
    steps.append(version.following(steps[-1]))
assert steps == ['0.1', '0.2', '0.3', '0.4', '0.5', '0.6', '0.7', '0.8', '0.9', '1.0', '1.1', '1.2', '1.3', '1.4',
                 '1.5', '1.6', '1.7', '1.8', '1.9', '2.0', '2.1'], steps
for wrong in ['1.10', '1', '1.0.0', 'v1.0', '01.0']:
    try:
        version.parse(wrong)
    except SystemExit:
        continue
    raise AssertionError(f'{wrong} accepted')
version.current()  # Every file states the same version.

# Writing versions, in a copy of the files: the current one again, the next step and the next X.0.
import shutil
import tempfile
with tempfile.TemporaryDirectory() as root:
    root = Path(root)
    for path in [version.CMAKE, version.METAINFO, *version.JSON_FILES]:
        target = root / path.relative_to(version.ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(path, target)
    version.CMAKE, version.METAINFO = root / 'CMakeLists.txt', root / version.METAINFO.relative_to(version.ROOT)
    version.JSON_FILES = {root / path.relative_to(version.ROOT): key for path, key in version.JSON_FILES.items()}
    version.ROOT = root
    now = version.current()
    major = f'{version.parse(now)[0] + 1}.0'
    # The current version written again: its entry is updated and keeps its notes, never repeated.
    version.write(now, '2026-11-03')
    metainfo = version.METAINFO.read_text()
    assert version.current() == now
    assert metainfo.count(f'<release version="{now}"') == 1, metainfo
    assert 'Development preview' not in metainfo, metainfo
    # The next step gets its own entry, first, with its date.
    step = version.following(now)
    version.write(step, '2026-11-04')
    metainfo = version.METAINFO.read_text()
    assert version.current() == step
    assert f'<release version="{step}" date="2026-11-04" type="stable">' in metainfo, metainfo
    assert metainfo.index(f'<release version="{step}"') < metainfo.index(f'<release version="{now}"'), metainfo
    version.write(major, '2026-11-05')
    assert version.current() == major
print('version scheme: steps, consistency and writing versions PASS')
