#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What a screen reader gets from Kalipso, through AT-SPI (the interface Orca uses on Linux).

Walks the accessibility tree of a running Kalipso and reports every control a person can reach that
has no name, and, with --tab N, presses Tab N times (xdotool) and lists what takes the focus, so a
keyboard path can be checked against what is on the screen.

    tests/atspi_audit.py [--app kalipso] [--tab 40] [--require-name]

Needs a session bus with the AT-SPI bus (at-spi-bus-launcher) and Kalipso started with
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1. Exit status: the number of unnamed controls (with --require-name).
"""
import argparse
import subprocess
import sys
import time

import gi

gi.require_version('Atspi', '2.0')
from gi.repository import Atspi  # noqa: E402

# Controls a person acts on or reads: each needs a name a screen reader can say.
NAMED = {
    Atspi.Role.PUSH_BUTTON, Atspi.Role.TOGGLE_BUTTON, Atspi.Role.CHECK_BOX, Atspi.Role.RADIO_BUTTON,
    Atspi.Role.COMBO_BOX, Atspi.Role.MENU_ITEM, Atspi.Role.CHECK_MENU_ITEM, Atspi.Role.RADIO_MENU_ITEM,
    Atspi.Role.SPIN_BUTTON, Atspi.Role.SLIDER, Atspi.Role.TEXT, Atspi.Role.ENTRY, Atspi.Role.PASSWORD_TEXT,
    Atspi.Role.LIST, Atspi.Role.LIST_BOX, Atspi.Role.PROGRESS_BAR, Atspi.Role.STATUS_BAR, Atspi.Role.PAGE_TAB,
    Atspi.Role.LINK, Atspi.Role.DIALOG, Atspi.Role.FRAME,
}


def label_of(node):
    name = (node.get_name() or '').strip()
    if name:
        return name
    for relation in node.get_relation_set() or []:
        if relation.get_relation_type() == Atspi.RelationType.LABELLED_BY:
            for i in range(relation.get_n_targets()):
                target = relation.get_target(i)
                if target and (target.get_name() or '').strip():
                    return target.get_name().strip()
    return ''


def showing(node):
    states = node.get_state_set()
    return states.contains(Atspi.StateType.SHOWING) and states.contains(Atspi.StateType.VISIBLE)


def walk(node, depth=0, path=''):
    try:
        role = node.get_role()
        role_name = node.get_role_name()
    except Exception:
        return
    here = f'{path}/{role_name}'
    yield node, role, here, depth
    try:
        count = node.get_child_count()
    except Exception:
        return
    for i in range(min(count, 2000)):
        try:
            child = node.get_child_at_index(i)
        except Exception:
            continue
        if child is not None:
            yield from walk(child, depth + 1, here)


def find_app(name, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        desktop = Atspi.get_desktop(0)
        for i in range(desktop.get_child_count()):
            app = desktop.get_child_at_index(i)
            if app is not None and name.lower() in (app.get_name() or '').lower():
                return app
        time.sleep(0.5)
    return None


def focused(app):
    """The control that has the keyboard focus. A list marks its current row "focused" even when the
    focus is elsewhere, so a row counts only when its list has the focus."""
    found = []
    for node, role, path, _ in walk(app):
        try:
            states = node.get_state_set()
            if not (states.contains(Atspi.StateType.FOCUSED) and states.contains(Atspi.StateType.SHOWING)):
                continue
            if role == Atspi.Role.LIST_ITEM:
                parent = node.get_parent()
                if not (parent and parent.get_state_set().contains(Atspi.StateType.FOCUSED)):
                    continue
            found.append((node, path))
        except Exception:
            continue
    # A focused text reports its viewport as focused too: the named object is the one a reader says.
    named = [item for item in found if label_of(item[0])]
    return (named or found or [(None, '')])[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--app', default='kalipso')
    parser.add_argument('--tab', type=int, default=0, help='press Tab this many times and list the focus')
    parser.add_argument('--require-name', action='store_true', help='exit with the number of unnamed controls')
    args = parser.parse_args()
    app = find_app(args.app)
    if app is None:
        sys.exit(f'No application named {args.app!r} on the accessibility bus')
    unnamed, counted = [], {}
    for node, role, path, _ in walk(app):
        if role not in NAMED:
            continue
        try:
            if not showing(node):
                continue
        except Exception:
            continue
        counted[node.get_role_name()] = counted.get(node.get_role_name(), 0) + 1
        if not label_of(node):
            unnamed.append(path)
    print('Controls in sight:', ', '.join(f'{k} {v}' for k, v in sorted(counted.items())))
    for path in unnamed:
        print('NO NAME:', path)
    if args.tab:
        seen = []
        for _ in range(args.tab):
            subprocess.run(['xdotool', 'key', 'Tab'], check=False)
            time.sleep(0.25)
            node, path = focused(app)
            label = label_of(node) if node else '(nothing)'
            role = node.get_role_name() if node else ''
            print(f"TAB → {role}: {label}" + (f"  [{path}]" if not label else ""))
            seen.append((role, label))
        if len(set(seen)) < len(seen) // 2:
            print('NOTE: the focus came back to the same controls quickly (a short cycle)')
    sys.exit(len(unnamed) if args.require_name else 0)


if __name__ == '__main__':
    main()
