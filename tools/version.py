#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Kalipso's version, kept the same in every place that states it.

Versions go up one step at a time, 0.1, 0.2 ... 0.9, 1.0, 1.1 ... 1.9, 2.0, one step per update.
Every step is published for every system (owner's decision, 3 November 2026): when a new version
reaches main, the release workflow tags it, builds every package and publishes them, and AUR follows.

  version.py check [vX.Y]     every file states the same version (and the tag's, if given)
  version.py next             print the version after the current one
  version.py set X.Y [--stable YYYY-MM-DD]   write a version everywhere; --stable records the release date
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE = ROOT / 'CMakeLists.txt'
METAINFO = ROOT / 'packaging/io.github.sansevieria_ou.kalipso.metainfo.xml'
JSON_FILES = {ROOT / 'desktop/plasma/metadata.json': ('KPlugin', 'Version'),
              ROOT / 'packaging/kwin/metadata.json': ('KPlugin', 'Version'),
              ROOT / 'desktop/gnome/metadata.json': (None, 'version-name')}
FIRST = '0.1'
PATTERN = re.compile(r'^(0|[1-9][0-9]*)\.[0-9]$')  # One digit after the point: ten steps per major.


def parse(version):
    if not PATTERN.match(version):
        sys.exit(f'{version!r} is not a Kalipso version (X.Y, with Y from 0 to 9)')
    major, step = version.split('.')
    return int(major), int(step)


def read():
    found = {}
    found[CMAKE.name] = re.search(r'project\(Kalipso VERSION ([0-9.]+)', CMAKE.read_text()).group(1)
    found[METAINFO.name] = re.search(r'<release version="([^"]+)"', METAINFO.read_text()).group(1)
    for path, (section, key) in JSON_FILES.items():
        data = json.loads(path.read_text())
        found[str(path.relative_to(ROOT))] = (data[section] if section else data)[key]
    return found


def current():
    versions = set(read().values())
    if len(versions) != 1:
        sys.exit('Versions differ:\n' + '\n'.join(f'  {name}: {value}' for name, value in read().items()))
    return versions.pop()


def following(version):
    major, step = parse(version)
    return f'{major + 1}.0' if step == 9 else f'{major}.{step + 1}'


def write(version, stable_date=None):
    parse(version)
    text = CMAKE.read_text()
    CMAKE.write_text(re.sub(r'(project\(Kalipso VERSION )[0-9.]+', rf'\g<1>{version}', text, count=1))
    for path, (section, key) in JSON_FILES.items():
        raw = path.read_text()
        old = (json.loads(raw)[section] if section else json.loads(raw))[key]
        path.write_text(raw.replace(f'"{key}": "{old}"', f'"{key}": "{version}"', 1))
    # The newest release goes first in AppStream; a development entry is replaced, not stacked.
    metainfo = METAINFO.read_text()
    kind = 'stable' if stable_date else 'development'
    date = stable_date or re.search(r'<release version="[^"]+" date="([^"]+)"', metainfo).group(1)
    entry = f'<release version="{version}" date="{date}" type="{kind}">'
    development = re.compile(r'<release version="[^"]+" date="[^"]+" type="development">\s*'
                             r'<description>.*?</description>', re.S)
    same = re.search(rf'<release version="{re.escape(version)}" date="[^"]+" type="[^"]+">', metainfo)
    if same:
        # The same version again: its entry is updated and keeps its notes, never repeated.
        metainfo = metainfo.replace(same.group(0), entry, 1)
    elif development.search(metainfo):
        # A published version does not keep a development step's description: CHANGELOG.md has what it
        # brings; write a summary of it here before tagging.
        description = (f'<description><p>Kalipso {version}.</p></description>' if stable_date
                       else re.search(r'<description>.*?</description>', development.search(metainfo).group(0), re.S).group(0))
        metainfo = development.sub(lambda _: f'{entry}\n      {description}', metainfo, count=1)
    else:
        metainfo = metainfo.replace('<releases>\n    <release ', f'<releases>\n    {entry}\n      <description><p>Kalipso {version}.</p></description>\n    </release>\n    <release ', 1)
    METAINFO.write_text(metainfo)


def main(args):
    if not args or args[0] not in {'check', 'next', 'set'}:
        sys.exit(__doc__)
    command = args[0]
    if command == 'check':
        version = current()
        parse(version)
        if len(args) > 1 and args[1] != f'v{version}':
            sys.exit(f'Tag {args[1]} does not match the version {version}')
        print(f'Version {version} everywhere')
    elif command == 'next':
        print(following(current()))
    elif command == 'set':
        stable = args[args.index('--stable') + 1] if '--stable' in args else None
        write(args[1], stable)
        print(f'Version {current()}')


if __name__ == '__main__':
    main(sys.argv[1:])
