# SPDX-License-Identifier: AGPL-3.0-or-later
"""The version scheme: ten steps between public releases, and every file states the same version."""
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
assert [step for step in steps if version.is_public(step)] == ['0.1', '1.0', '2.0']
for wrong in ['1.10', '1', '1.0.0', 'v1.0', '01.0']:
    try:
        version.parse(wrong)
    except SystemExit:
        continue
    raise AssertionError(f'{wrong} accepted')
version.current()  # Every file states the same version.

# Writing a version, in a copy of the files: a published version (2.0, straight from 0.1) gets its own
# release entry, not the development step's description.
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
    version.write('2.0', '2026-11-03')
    assert version.current() == '2.0'
    metainfo = version.METAINFO.read_text()
    assert '<release version="2.0" date="2026-11-03" type="stable">' in metainfo, metainfo
    assert 'Development preview' not in metainfo, metainfo
    assert metainfo.count('<release version="2.0"') == 1, metainfo
    # Setting the same version again updates its entry and keeps its notes.
    version.write('2.0', '2026-11-04')
    again = version.METAINFO.read_text()
    assert again.count('<release version="2.0"') == 1, again
    assert '<release version="2.0" date="2026-11-04" type="stable">' in again, again
print('version scheme: steps, public releases, consistency and writing a published version PASS')
