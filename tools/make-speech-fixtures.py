#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Generate synthetic speech for native CI; requires espeak-ng and ffmpeg, never records a person."""
from pathlib import Path
import subprocess
import sys

target = Path(sys.argv[1])
target.mkdir(parents=True, exist_ok=True)
texts = {
    'it': 'Oggi leggiamo insieme un breve testo. La luce del sole entra dalla finestra. '
          'Sul tavolo ci sono un libro e una tazza. Parliamo con calma e seguiamo tutte le parole. '
          'La prova continua fino alla fine della pagina.',
    'en': 'Today we are reading a short story together. The sun is shining through the window. '
          'There is a book and a cup on the table. We speak slowly and follow all the words. '
          'The story continues until the end of the page.',
}
for language, text in texts.items():
    (target / f'{language}.txt').write_text(text, encoding='utf-8')
    subprocess.run(['espeak-ng', '-v', language, '-s', '145', '-w', str(target / f'{language}.wav'), text], check=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(target / f'{language}.wav'), '-ar', '16000',
                    '-ac', '1', '-f', 's16le', str(target / f'{language}.pcm')], check=True)
    (target / f'{language}.wav').unlink()
