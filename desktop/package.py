#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Build the Plasma widget, GNOME extension and KWin script archives from source.

Archives contain only frontend source and notices: no companion binary,
speech model or recording. Output: artifacts/packages/. File timestamps are
fixed so identical sources produce identical archives.

With --tree DIR the same files are written unpacked, in DIR/plasma, DIR/gnome
and DIR/kwin: what `cmake --install` puts where each desktop looks for add-ons
installed system-wide, so every Linux package carries them.
"""
from pathlib import Path
import argparse
import ast
import hashlib
import struct
import subprocess
import sys
import zipfile

BASE = Path(__file__).resolve().parent
ROOT = BASE.parent
OUT = ROOT / 'artifacts/packages'
SOURCE_SUFFIXES = {'.qml', '.mjs', '.js', '.css', '.json', '.xml'}
NOTICES = [(ROOT / 'LICENSES/Unicode-3.0.txt', 'Unicode-LICENSE.txt'),
           (ROOT / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')]
PLASMA_DOMAIN = 'plasma_applet_io.github.sansevieria_ou.kalipso'
GNOME_DOMAIN = 'kalipso'


def compile_po(path):
    """Compile a .po file (plurals included, no contexts) to .mo bytes."""
    messages = {}
    entry, field = {}, None
    fuzzy = False  # "#, fuzzy" translations are guesses from msgmerge: never ship them.

    def finish():
        if 'msgid' not in entry or 'msgstr' not in entry and 'msgstr[0]' not in entry:
            return
        if fuzzy and entry['msgid']:
            return
        if 'msgid_plural' in entry:  # .mo plural: "singular\0plural" -> "form0\0form1..."
            forms = [entry[f'msgstr[{n}]'] for n in range(10) if f'msgstr[{n}]' in entry]
            if any(forms):
                messages[entry['msgid'] + '\0' + entry['msgid_plural']] = '\0'.join(forms)
        elif entry['msgstr'] or not entry['msgid']:
            messages[entry['msgid']] = entry['msgstr']

    for line in path.read_text(encoding='utf-8').splitlines() + ['']:
        line = line.strip()
        if line.startswith('#,') and 'fuzzy' in line:
            finish()
            entry, field, fuzzy = {}, None, True
            continue
        if line.startswith('msgid '):
            if field is not None:
                finish()
                fuzzy = False
            entry, field = {}, 'msgid'
        elif line.startswith(('msgid_plural ', 'msgstr ', 'msgstr[')):
            field = line.split(' ', 1)[0]
        elif not line.startswith('"'):
            continue
        entry[field] = entry.get(field, '') + ast.literal_eval(line[line.index('"'):])
    finish()
    keys = sorted(messages)
    ids = [key.encode() for key in keys]
    strs = [messages[key].encode() for key in keys]
    header_size = 7 * 4
    table = header_size + 16 * len(keys)
    ids_data = b''.join(item + b'\0' for item in ids)
    offsets, position = [], table
    for item in ids:
        offsets.append((len(item), position))
        position += len(item) + 1
    str_offsets = []
    for item in strs:
        str_offsets.append((len(item), position))
        position += len(item) + 1
    output = struct.pack('<7I', 0x950412de, 0, len(keys), header_size, header_size + 8 * len(keys), 0, 0)
    output += b''.join(struct.pack('<2I', *pair) for pair in offsets)
    output += b''.join(struct.pack('<2I', *pair) for pair in str_offsets)
    return output + ids_data + b''.join(item + b'\0' for item in strs)


def translations(prefix, domain, catalogs=BASE / 'po'):
    return [(path, f'{prefix}{path.stem}/LC_MESSAGES/{domain}.mo', compile_po(path))
            for path in sorted(catalogs.glob('*.po'))]


def plasma_files():
    package = BASE / 'plasma'
    # The widget draws its text in the app's typeface, embedded like the app (src/fonts, OFL).
    fonts = [(path, 'contents/fonts/' + path.name) for path in sorted((ROOT / 'src/fonts').iterdir())
             if path.suffix in {'.ttf', '.otf', '.txt'}]
    # Kalipso's icon, shown in a panel: the widget may be installed without the app and its icon.
    icon = [(ROOT / 'packaging/io.github.sansevieria_ou.kalipso.svg', 'contents/images/kalipso.svg')]
    return [(path, path.relative_to(package).as_posix()) for path in sorted(package.rglob('*'))
            if path.is_file() and path.suffix in SOURCE_SUFFIXES] + fonts + icon


def gnome_files():
    extension = BASE / 'gnome'
    own = [(path, path.name) for path in sorted(extension.iterdir()) if path.suffix in SOURCE_SUFFIXES]
    # GNOME has its own GPL-compatible snapshots; never relicense shared changes on export.
    return own


def kwin_files():
    package = ROOT / 'packaging/kwin'
    return [(path, path.relative_to(package).as_posix()) for path in sorted(package.rglob('*'))
            if path.is_file() and path.suffix in SOURCE_SUFFIXES]


def entries(files, generated, licence):
    name = 'AGPL-3.0-or-later' if licence == BASE / 'gnome/LICENSE' else 'SANSEVIERIA License 1.0'
    notice = (
        'KALIPSO DESKTOP COMPONENT — RELEASE NOTICE\n\n'
        'Steward: SANSEVIERIA OÜ\n'
        'Component and version: identified in the accompanying metadata.json.\n'
        f'Original code in this component: {name}; full text in LICENSE.\n'
        'Separately marked third-party material retains its own terms.\n'
        'This archive contains the component source. Preserve notices when redistributing.\n'
        'Project source and build scripts: https://github.com/SANSEVIERIA-OU/kalipso\n'
        'Licensing contact: https://github.com/SANSEVIERIA-OU/kalipso/issues\n'
        'The licence of this component is not an alternative licence for other components.\n'
    )
    return ([(destination, source.read_bytes()) for source, destination in files + NOTICES + [(licence, 'LICENSE')]]
            + [('NOTICE', notice.encode('utf-8'))]
            + [(destination, data) for _, destination, data in generated])


def write(name, files, generated=(), licence=ROOT / 'LICENSE'):
    with zipfile.ZipFile(OUT / name, 'w', zipfile.ZIP_DEFLATED) as archive:
        for destination, data in entries(files, generated, licence):
            info = zipfile.ZipInfo(destination, (2026, 10, 26, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(hashlib.sha256((OUT / name).read_bytes()).hexdigest(), name)


def write_tree(folder, files, generated=(), licence=ROOT / 'LICENSE'):
    """The archive's files, unpacked into an empty folder."""
    for destination, data in entries(files, generated, licence):
        path = folder / destination
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--tree', type=Path, help='write the files unpacked into this folder instead')
    tree = parser.parse_args().tree
    subprocess.run([sys.executable, str(ROOT / 'tools/sync-shared.py'), '--check'], check=True)
    plasma = (plasma_files(), translations('contents/locale/', PLASMA_DOMAIN))
    gnome = (gnome_files(), translations('locale/', GNOME_DOMAIN, BASE / 'gnome/po'), BASE / 'gnome/LICENSE')
    kwin = (kwin_files(),)
    if tree:
        for name, contents in (('plasma', plasma), ('gnome', gnome), ('kwin', kwin)):
            write_tree(tree / name, *contents)
        return
    OUT.mkdir(parents=True, exist_ok=True)
    write('kalipso-plasma.plasmoid', *plasma)
    write('kalipso-gnome.zip', *gnome)
    write('kalipso-kwin.kwinscript', *kwin)


if __name__ == '__main__':
    main()
