#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The GNOME extension finds a companion from Kalipso 2.0, which answers only to the name it had
before Kalipso moved to SANSEVIERIA-OU (io.github.brianidavide.Kalipso.Engine).

On a private session bus, a stand-in companion owns only the earlier name; the extension's own client
(desktop/gnome/local-engine.js) asks for the app's scripts and must get them. The counter-test: with no
companion at all, it reports an empty library instead of hanging.

    companion_names_test.py <gjs>
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
GJS = sys.argv[1] if len(sys.argv) > 1 else 'gjs'

SERVICE = r'''
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
const name = 'io.github.brianidavide.Kalipso.Engine';
const xml = `<node><interface name="${name}"><method name="ListScripts"><arg type="s" direction="out"/></method></interface></node>`;
const object = Gio.DBusExportedObject.wrapJSObject(xml, {
    ListScripts() { return JSON.stringify({available: true, current: 'a', scripts: [{id: 'a', title: 'Older talk'}]}); },
});
object.export(Gio.DBus.session, '/io/github/brianidavide/Kalipso/Engine');
Gio.bus_own_name_on_connection(Gio.DBus.session, name, Gio.BusNameOwnerFlags.NONE, () => print('READY'), null);
new GLib.MainLoop(null, false).run();
'''

CLIENT = r'''
import GLib from 'gi://GLib';
import {LocalEngine} from './local-engine.js';
const loop = new GLib.MainLoop(null, false);
new LocalEngine(() => {}).scripts().then(library => { print(JSON.stringify(library)); loop.quit(); });
GLib.timeout_add(GLib.PRIORITY_DEFAULT, 10000, () => { print('TIMEOUT'); loop.quit(); return false; });
loop.run();
'''


def run_client(folder):
    return subprocess.run([GJS, '-m', str(folder / 'client.js')], capture_output=True, text=True, timeout=30).stdout


def main():
    if 'DBUS_SESSION_BUS_ADDRESS' not in os.environ or os.environ.get('KALIPSO_NAMES_INNER') != '1':
        env = dict(os.environ, KALIPSO_NAMES_INNER='1')
        sys.exit(subprocess.run(['dbus-run-session', '--', sys.executable, __file__, GJS], env=env).returncode)
    with tempfile.TemporaryDirectory() as directory:
        folder = pathlib.Path(directory)
        # The extension's client as GNOME Shell loads it, with gettext from GNOME Shell stood in for.
        source = (ROOT / 'desktop/gnome/local-engine.js').read_text()
        source = source.replace("import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';",
                                'const _ = text => text;')
        (folder / 'local-engine.js').write_text(source)
        (folder / 'client.js').write_text(CLIENT)
        (folder / 'service.js').write_text(SERVICE)
        # Counter-test: no companion at all, an empty library.
        alone = run_client(folder)
        assert '"available":false' in alone, alone
        service = subprocess.Popen([GJS, '-m', str(folder / 'service.js')], stdout=subprocess.PIPE, text=True)
        try:
            assert service.stdout.readline().strip() == 'READY'
            time.sleep(0.2)
            found = run_client(folder)
            assert '"available":true' in found and 'Older talk' in found, found
        finally:
            service.terminate()
    print('PASS: the extension finds a Kalipso 2.0 companion by its earlier name; without one, an empty library')


if __name__ == '__main__':
    main()
