#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Checks a Vosk runtime built from source against the one Kalipso downloads today (#26).

For each library:
- the C functions Kalipso calls are there (src/speech.cpp, src/engine/config.h);
- the newest glibc and libstdc++ symbol versions it needs;
- what it recognizes on the same Italian recordings (FLEURS, CC-BY 4.0), with and without the grammar
  Kalipso gives it from the script, with the word error rate against the reference;
- how fast (decode time / audio time) and how much memory (peak RSS of a fresh process).

    check-runtime.py --new NEW/libvosk.so --old OLD/libvosk.so --model vosk-model-small-it-0.22 \\
        --data /tmp/fleurs-it [--clips 25] [--max-glibc 2.17] [--report report.json]

Exit status 0 when the new library has every function, needs no newer glibc than --max-glibc, and
recognizes no worse than the old one (word error rate at most 2 points higher). Standard library only.
"""
import argparse
import csv
import ctypes
import io
import json
import re
import resource
import subprocess
import sys
import tarfile
import time
import unicodedata
import urllib.request
import wave
from pathlib import Path

# Called by Kalipso: the first seven always (src/engine/config.h), the rest when present (src/speech.cpp).
REQUIRED = ['vosk_model_new', 'vosk_model_free', 'vosk_recognizer_new', 'vosk_recognizer_free',
            'vosk_recognizer_accept_waveform', 'vosk_recognizer_result', 'vosk_recognizer_partial_result',
            'vosk_set_log_level', 'vosk_model_find_word', 'vosk_recognizer_new_grm', 'vosk_recognizer_set_grm',
            'vosk_recognizer_final_result']
# FLEURS at the revision the ASR benchmark uses (benchmarks/asr/prepare_audio.py).
REVISION = '70bb2e84b976b7e960aa89f1c648e09c59f894dd'
FLEURS = 'https://huggingface.co/datasets/google/fleurs/resolve/{revision}/data/it_it/{path}'
RATE = 16000


def symbol_versions(library):
    """The newest GLIBC and GLIBCXX versions the library's dynamic symbols need."""
    out = subprocess.run(['objdump', '-T', str(library)], capture_output=True, text=True, check=True).stdout
    newest = {}
    for family in ('GLIBC', 'GLIBCXX', 'CXXABI'):
        found = re.findall(rf'\b{family}_([0-9][0-9.]*)', out)
        if found:
            newest[family] = max(found, key=lambda v: tuple(int(p) for p in v.split('.')))
    needed = re.findall(r'NEEDED\s+(\S+)', subprocess.run(['objdump', '-p', str(library)], capture_output=True,
                                                         text=True, check=True).stdout)
    return newest, needed


def fetch_clips(data, count):
    """The first `count` distinct sentences of FLEURS Italian test, as 16 kHz mono WAV files."""
    data.mkdir(parents=True, exist_ok=True)
    manifest = data / 'clips.json'
    if manifest.exists():
        clips = json.loads(manifest.read_text())
        if len(clips) >= count:
            return clips[:count]
    tsv = urllib.request.urlopen(FLEURS.format(revision=REVISION, path='test.tsv'), timeout=120).read().decode()
    rows, seen = [], set()
    for fields in csv.reader(io.StringIO(tsv), delimiter='\t', quoting=csv.QUOTE_NONE):
        if len(fields) >= 3 and fields[2] not in seen:
            seen.add(fields[2])
            rows.append({'file': fields[1], 'reference': fields[2]})
            if len(rows) == count:
                break
    wanted = {row['file']: row for row in rows}
    request = urllib.request.Request(FLEURS.format(revision=REVISION, path='audio/test.tar.gz'),
                                     headers={'User-Agent': 'kalipso-vosk-runtime-check'})
    with urllib.request.urlopen(request, timeout=300) as response, tarfile.open(fileobj=response, mode='r|gz') as tar:
        for member in tar:
            name = Path(member.name).name
            if name in wanted and member.isfile():
                (data / name).write_bytes(tar.extractfile(member).read())
                wanted.pop(name)
                if not wanted:
                    break
    if wanted:
        sys.exit(f'Missing FLEURS clips: {sorted(wanted)}')
    manifest.write_text(json.dumps(rows, ensure_ascii=False, indent=1))
    return rows


def words(text):
    text = unicodedata.normalize('NFKC', text).casefold()
    return re.sub(r"[^\w']+|_", ' ', text).replace("'", ' ').split()


def edits(reference, hypothesis):
    previous = list(range(len(hypothesis) + 1))
    for i, ref in enumerate(reference, 1):
        current = [i]
        for j, hyp in enumerate(hypothesis, 1):
            current.append(min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (ref != hyp)))
        previous = current
    return previous[-1]


def decode(library, model, data, clips, grammar):
    """Runs in its own process, so peak memory is this library's alone."""
    vosk = ctypes.CDLL(str(library))
    vosk.vosk_set_log_level(-1)
    vosk.vosk_model_new.restype = ctypes.c_void_p
    vosk.vosk_model_new.argtypes = [ctypes.c_char_p]
    vosk.vosk_recognizer_new.restype = ctypes.c_void_p
    vosk.vosk_recognizer_new.argtypes = [ctypes.c_void_p, ctypes.c_float]
    vosk.vosk_recognizer_new_grm.restype = ctypes.c_void_p
    vosk.vosk_recognizer_new_grm.argtypes = [ctypes.c_void_p, ctypes.c_float, ctypes.c_char_p]
    vosk.vosk_recognizer_accept_waveform.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    for name in ('vosk_recognizer_result', 'vosk_recognizer_final_result'):
        getattr(vosk, name).restype = ctypes.c_char_p
        getattr(vosk, name).argtypes = [ctypes.c_void_p]
    vosk.vosk_recognizer_free.argtypes = [ctypes.c_void_p]
    vosk.vosk_model_free.argtypes = [ctypes.c_void_p]
    started = time.perf_counter()
    handle = vosk.vosk_model_new(str(model).encode())
    if not handle:
        sys.exit(f'{library}: the model did not load')
    load_seconds = time.perf_counter() - started
    # As Kalipso does while following a script: the script's words the model knows (vosk_model_find_word),
    # and "[unk]" for anything else.
    vosk.vosk_model_find_word.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    vocabulary = sorted({word for clip in clips for word in words(clip['reference'])})
    known = [word for word in vocabulary if vosk.vosk_model_find_word(handle, word.encode()) >= 0]
    grm = json.dumps(known + ['[unk]'], ensure_ascii=False).encode()
    results, decode_seconds, audio_seconds = [], 0.0, 0.0
    for clip in clips:
        with wave.open(str(data / clip['file'])) as audio:
            if (audio.getframerate(), audio.getnchannels(), audio.getsampwidth()) != (RATE, 1, 2):
                sys.exit(f'{clip["file"]}: not 16 kHz mono 16-bit')
            pcm = audio.readframes(audio.getnframes())
        audio_seconds += len(pcm) / (2 * RATE)
        started = time.perf_counter()
        recognizer = (vosk.vosk_recognizer_new_grm(handle, RATE, grm) if grammar
                      else vosk.vosk_recognizer_new(handle, RATE))
        texts = []
        step = RATE // 5 * 2  # 200 ms, as Kalipso feeds the recognizer.
        for offset in range(0, len(pcm), step):
            chunk = pcm[offset:offset + step]
            if vosk.vosk_recognizer_accept_waveform(recognizer, chunk, len(chunk)) == 1:
                texts.append(json.loads(vosk.vosk_recognizer_result(recognizer).decode())['text'])
        texts.append(json.loads(vosk.vosk_recognizer_final_result(recognizer).decode())['text'])
        vosk.vosk_recognizer_free(recognizer)
        decode_seconds += time.perf_counter() - started
        results.append(' '.join(t for t in texts if t))
    vosk.vosk_model_free(handle)
    return {'transcripts': results, 'grammar_words': len(known), 'load_seconds': load_seconds, 'decode_seconds': decode_seconds,
            'audio_seconds': audio_seconds, 'peak_rss_mib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024}


def measure(library, model, data, clips, grammar):
    command = [sys.executable, __file__, '--decode', str(library), '--model', str(model), '--data', str(data),
               '--clips', str(len(clips))] + (['--grammar'] if grammar else [])
    run = json.loads(subprocess.run(command, capture_output=True, text=True, check=True).stdout)
    reference = sum(len(words(c['reference'])) for c in clips)
    errors = sum(edits(words(c['reference']), words(t)) for c, t in zip(clips, run['transcripts']))
    run['wer'] = round(100 * errors / reference, 1)
    run['words_recognized'] = sum(len(words(t)) for t in run['transcripts'])
    run['reference_words'] = reference
    run['rtf'] = round(run['decode_seconds'] / run['audio_seconds'], 3)
    return run


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--new', type=Path)
    parser.add_argument('--old', type=Path)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--clips', type=int, default=25)
    parser.add_argument('--max-glibc', default='2.17')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--decode', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--grammar', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    clips = fetch_clips(args.data, args.clips)
    if args.decode:
        print(json.dumps(decode(args.decode, args.model, args.data, clips, args.grammar)))
        return 0
    problems, report = [], {'clips': len(clips), 'libraries': {}}
    for label, library in (('new', args.new), ('old', args.old)):
        lib = ctypes.CDLL(str(library))
        missing = [name for name in REQUIRED if not hasattr(lib, name)]
        versions, needed = symbol_versions(library)
        entry = {'path': str(library), 'missing': missing, 'needs': versions, 'needed': needed}
        for grammar in (False, True):
            entry['grammar' if grammar else 'plain'] = measure(library, args.model, args.data, clips, grammar)
        report['libraries'][label] = entry
        if missing:
            problems.append(f'{label}: missing {", ".join(missing)}')
    new, old = report['libraries']['new'], report['libraries']['old']
    limit = tuple(int(p) for p in args.max_glibc.split('.'))
    if tuple(int(p) for p in new['needs'].get('GLIBC', '0').split('.')) > limit:
        problems.append(f'new: needs glibc {new["needs"]["GLIBC"]}, more than {args.max_glibc}')
    for mode in ('plain', 'grammar'):
        if new[mode]['words_recognized'] == 0:
            problems.append(f'new: recognized nothing ({mode})')
        if new[mode]['wer'] > old[mode]['wer'] + 2:
            problems.append(f'new: word error rate {new[mode]["wer"]}% against {old[mode]["wer"]}% ({mode})')
    report['problems'] = problems
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=1))
    print(f'{"":10}{"needs glibc":>12}{"mode":>9}{"WER %":>8}{"words":>7}{"RTF":>7}{"load s":>8}{"peak MiB":>10}')
    for label in ('old', 'new'):
        entry = report['libraries'][label]
        for mode in ('plain', 'grammar'):
            run = entry[mode]
            print(f'{label:10}{entry["needs"].get("GLIBC", "?"):>12}{mode:>9}{run["wer"]:>8}{run["words_recognized"]:>7}'
                  f'{run["rtf"]:>7}{run["load_seconds"]:>8.2f}{run["peak_rss_mib"]:>10.0f}')
    print(f'{report["clips"]} FLEURS Italian clips, {old["plain"]["audio_seconds"]:.0f} s of audio, '
          f'{old["plain"]["reference_words"]} reference words')
    for problem in problems:
        print('PROBLEM:', problem)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
