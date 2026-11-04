#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run src/impress/impress_follow.py against a real LibreOffice Impress slideshow.

Needs LibreOffice, a Python that can import uno (on Windows and macOS,
LibreOffice's own), and on Linux a display (xvfb-run is fine). Exits 77
(skipped) when any of them is missing.
"""
import json
import os
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
# The Python that runs the helper, as Kalipso starts it (ImpressFollower::findOffice): LibreOffice's own
# python(.exe) on Windows and macOS. Under LibreOffice's launcher sys.executable is not always a program
# Windows can start, so CI names it.
PYTHON = os.environ.get('KALIPSO_IMPRESS_PYTHON') or sys.executable
HELPER = ROOT / 'src' / 'impress' / 'impress_follow.py'
FIXTURE = ROOT / 'tests' / 'fixtures' / 'notes.odp'
SKIP = 77


class OfficeUnavailable(Exception):
    pass


def main():
    office = shutil.which('soffice') or shutil.which('libreoffice')
    # A display: on Linux an X11 or Wayland one; Windows and macOS always have their desktop.
    display = not sys.platform.startswith('linux') or os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')
    if not office or not display:
        print('skipped: needs LibreOffice and a display')
        return SKIP
    try:
        import uno  # noqa: F401
        from com.sun.star.beans import PropertyValue  # noqa: F401
    except ImportError:
        print('skipped: Python cannot import uno')
        return SKIP

    # LibreOffice on Windows can hold its profile's files a moment after it quits.
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as work:
        work = pathlib.Path(work)
        (work / 'real').mkdir()
        presentation = work / 'real' / 'talk notes.odp'  # A space, as in real file names.
        shutil.copy(FIXTURE, presentation)
        # The helper is given the same file by another path where the system allows a symbolic link, as
        # LibreOffice reports /private/var for /var on macOS, or long names for short ones on Windows.
        try:
            (work / 'link').symlink_to(work / 'real', target_is_directory=True)
            given = work / 'link' / presentation.name
        except OSError:  # Windows without the privilege: its short and long names do the same job.
            given = presentation
        pipe = f'kalipso-test-{os.getpid()}'
        soffice = subprocess.Popen([office, f'-env:UserInstallation={(work / "profile").as_uri()}', '--norestore',
                                    '--nologo', '--nodefault', f'--accept=pipe,name={pipe};urp;', str(presentation)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        errors = open(work / 'helper-errors.txt', 'w+', encoding='utf-8')
        helper = subprocess.Popen([PYTHON, str(HELPER), pipe, str(given)], stdout=subprocess.PIPE,
                                  stderr=errors, text=True)
        lines = queue.Queue()
        seen = []  # Every event, to say what came instead when one does not.
        diagnosis = []
        threading.Thread(target=lambda: [lines.put(json.loads(line)) for line in helper.stdout], daemon=True).start()

        def expect(state, timeout=60, **fields):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if soffice.poll() is not None and state != 'closed':
                    raise OfficeUnavailable(f'LibreOffice exited ({soffice.returncode}); is the display usable?')
                try:
                    event = lines.get(timeout=1)
                except queue.Empty:
                    continue
                seen.append(event)
                if event.get('state') == state and all(event.get(k) == v for k, v in fields.items()):
                    return event
            errors.seek(0)
            raise AssertionError(f'no {state} {fields} within {timeout} s; events: {seen}; {"; ".join(diagnosis)}; '
                                 f'helper errors: {errors.read()[-2000:]!r}')

        try:
            expect('connecting', timeout=10)
            expect('waiting', timeout=90)
            desktop = connect(pipe)
            document = find(desktop, presentation)
            show = document.getPresentation()
            if sys.platform == 'darwin':
                # A full-screen slideshow needs a Space of its own, which a session nobody sits at may not
                # give; in a window it runs, and Kalipso follows it the same way.
                show.IsFullScreen = False
                subprocess.run(['osascript', '-e', 'tell application "LibreOffice" to activate'],
                               capture_output=True, timeout=20)
            show.start()
            # What LibreOffice itself says, to tell a slideshow that did not start from one not followed.
            try:
                started = wait_for(lambda: show.isRunning() and show.getController() is not None, timeout=20)
            except AssertionError:
                started = False
            diagnosis.append(f'LibreOffice says the slideshow is running: {bool(started)}')
            expect('slide', slide=1, notes='Thank everyone for coming.\nMention the  two  spaces.')
            controller = wait_for(lambda: show.getController())
            controller.gotoSlideIndex(2)
            expect('slide', slide=3, notes='Revenue grew by twelve percent.\nPause for questions.')
            controller.gotoSlideIndex(1)
            expect('slide', slide=2, notes='')  # A slide without notes.
            show.end()
            expect('ended')
            try:
                document.close(True)
            except Exception as reason:  # noqa: BLE001
                # Closing the only document quits LibreOffice, which can drop the bridge during this very
                # call (seen on Windows). The helper reports that as closed, which is what is checked.
                if 'Disposed' not in type(reason).__name__:
                    raise
            expect('closed')
            helper.wait(timeout=10)
            assert helper.returncode == 0, helper.returncode
        except OfficeUnavailable as reason:
            print(f'skipped: {reason}')
            return SKIP
        finally:
            helper.kill()
            helper.wait(timeout=10)
            errors.close()
            soffice.terminate()
            try:
                soffice.wait(timeout=30)
            except subprocess.TimeoutExpired:
                soffice.kill()

        # Negative control: a presentation that is not open is never reported as a slide.
        bad = subprocess.run([PYTHON, str(HELPER), 'kalipso-no-such-pipe', str(presentation)],
                             capture_output=True, text=True, timeout=60,
                             env={**os.environ, 'KALIPSO_IMPRESS_CONNECT_SECONDS': '2'})
        assert '"slide"' not in bad.stdout and '"error"' in bad.stdout, bad.stdout
    print('impress: slideshow start, slide changes, empty notes, end and close PASS; negative control fails as expected')
    return 0


def connect(pipe):
    import uno
    local = uno.getComponentContext()
    resolver = local.ServiceManager.createInstanceWithContext('com.sun.star.bridge.UnoUrlResolver', local)
    context = wait_for(lambda: resolver.resolve(f'uno:pipe,name={pipe};urp;StarOffice.ComponentContext'))
    return context.ServiceManager.createInstanceWithContext('com.sun.star.frame.Desktop', context)


def find(desktop, path):
    import uno

    def lookup():
        components = desktop.getComponents().createEnumeration()
        while components.hasMoreElements():
            component = components.nextElement()
            if hasattr(component, 'getURL') and component.getURL():
                try:
                    if os.path.samefile(uno.fileUrlToSystemPath(component.getURL()), path):
                        return component
                except (OSError, ValueError):
                    pass
        return None
    return wait_for(lookup)


def wait_for(probe, timeout=60):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = probe()
            if value:
                return value
        except Exception:  # Not ready yet (for example no connection).
            pass
        time.sleep(0.3)
    raise AssertionError('timed out')


if __name__ == '__main__':
    sys.exit(main())
