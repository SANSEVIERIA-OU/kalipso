#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Prepare recordings and specs for benchmarks/follow_probe.cpp: voice following, end to end.

    prepare_follow.py OUTPUT [--language it] [--clips 48]

Streams the FLEURS test archive of the language (CC BY 4.0, pinned revision) and keeps the first
clips, in archive order, whose transcripts differ. Writes them as PCM16 mono 16 kHz, plus two
room-like versions made here: "room" (reverberation RT60 0.4 s, steady noise at 20 dB SNR, a quiet
microphone) and "noisy" (RT60 0.6 s, 10 dB). The specs read the clips in groups of twelve as
scripts ("read-N"), skip one or four sentences ("skip-1", "skip-4") and add two sentences that are
not in the script ("aside"). FLEURS speakers read single sentences; the joined clips stand in for a
person reading a script, with a pause between sentences.

Needs NumPy for the room versions (convolution); everything else is the standard library.
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import struct
import tarfile
import urllib.request
from pathlib import Path

REVISION = "70bb2e84b976b7e960aa89f1c648e09c59f894dd"
CONFIGS = {"it": "it_it", "en": "en_us", "de": "de_de", "es": "es_419", "fr": "fr_fr"}
URL = "https://huggingface.co/datasets/google/fleurs/resolve/{revision}/data/{config}/{path}"
ROOMS = {"room": (0.4, 20.0), "noisy": (0.6, 10.0)}


def fetch(url: str, timeout: int = 300):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "kalipso-follow/1.0"}),
                                  timeout=timeout)


def pcm16(source: bytes) -> bytes:
    """FLEURS RIFF/WAVE float32 mono 16 kHz to raw signed PCM16."""
    pos, fmt, audio = 12, None, None
    while pos + 8 <= len(source):
        tag, size = source[pos:pos + 4], struct.unpack_from("<I", source, pos + 4)[0]
        chunk = source[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", chunk)
        elif tag == b"data":
            audio = chunk
            break
        pos += 8 + size + (size & 1)
    if fmt is None or audio is None or fmt[:3] != (3, 1, 16000):
        raise ValueError(f"Unexpected FLEURS audio format {fmt}")
    out = bytearray()
    for (sample,) in struct.iter_unpack("<f", audio):
        out += struct.pack("<h", max(-32768, min(32767, round(max(-1.0, min(1.0, sample)) * 32767))))
    return bytes(out)


def download(output: Path, language: str, count: int) -> list[dict]:
    config = CONFIGS[language]
    with fetch(URL.format(revision=REVISION, config=config, path="test.tsv")) as response:
        rows = {f[1]: f[2] for f in csv.reader(io.StringIO(response.read().decode("utf-8")), delimiter="\t")}
    kept, seen = [], set()
    with fetch(URL.format(revision=REVISION, config=config, path="audio/test.tar.gz")) as response, \
            tarfile.open(fileobj=response, mode="r|gz") as archive:
        for member in archive:
            name = Path(member.name).name
            if not member.isfile() or name not in rows or rows[name] in seen:
                continue
            seen.add(rows[name])
            file = f"clip-{len(kept):03d}.pcm"
            (output / file).write_bytes(pcm16(archive.extractfile(member).read()))
            kept.append({"file": file, "source": member.name, "text": rows[name]})
            if len(kept) == count:
                break
    (output / "manifest.json").write_text(json.dumps({"dataset": "google/fleurs", "revision": REVISION,
                                                      "license": "CC-BY-4.0", "clips": kept},
                                                     ensure_ascii=False, indent=1), encoding="utf-8")
    return kept


def roomed(output: Path, clips: list[dict]) -> None:
    import numpy as np

    for name, (rt60, snr) in ROOMS.items():
        rng = np.random.default_rng(20261102)
        folder = output / name
        folder.mkdir(exist_ok=True)
        # Direct sound, then diffuse noise decaying by 60 dB over RT60.
        t = np.arange(int(16000 * rt60)) / 16000
        rir = rng.standard_normal(len(t)) * np.exp(-6.9 * t / rt60) * 0.25
        rir[:48] = 0
        rir[0] = 1.0
        rir /= np.sqrt(np.sum(rir ** 2))
        for clip in clips:
            x = np.frombuffer((output / clip["file"]).read_bytes(), dtype="<i2").astype(np.float64) / 32768
            y = np.convolve(x, rir)[:len(x)]
            # Steady noise falling with frequency (fan, room tone): white noise through a one-pole low-pass.
            white = rng.standard_normal(len(y))
            noise = np.empty_like(white)
            level = 0.0
            for i, value in enumerate(white):
                level = 0.9 * level + 0.1 * value
                noise[i] = level
            speech = np.mean(y[np.abs(y) > 0.01 * np.max(np.abs(y))] ** 2)
            noise *= np.sqrt(speech / 10 ** (snr / 10) / np.mean(noise ** 2))
            z = y + noise
            z *= 0.25 / max(1e-9, np.max(np.abs(z)))  # A quiet microphone: peaks at -12 dBFS.
            (folder / clip["file"]).write_bytes((np.clip(z, -1, 1) * 32767).astype("<i2").tobytes())


def specs(output: Path, clips: list[dict], language: str) -> None:
    for condition in ("clean", *ROOMS):
        folder = output if condition == "clean" else output / condition
        target = output / "specs" / condition
        target.mkdir(parents=True, exist_ok=True)

        def segment(i: int, kind: str = "read") -> dict:
            return {"pcm": str((folder / clips[i]["file"]).resolve()), "text": clips[i]["text"], "kind": kind}

        def write(name: str, segments: list[dict]) -> None:
            (target / f"{name}.json").write_text(json.dumps({"language": language, "segments": segments},
                                                            ensure_ascii=False, indent=1), encoding="utf-8")

        groups = len(clips) // 12
        for g in range(groups):
            write(f"read-{g}", [segment(i) for i in range(g * 12, g * 12 + 12)])
        write("skip-1", [segment(i, "skipped" if i == 3 else "read") for i in range(10)])
        if groups >= 2:
            write("skip-4", [segment(i, "skipped" if 15 <= i <= 18 else "read") for i in range(12, 24)])
        if groups >= 4:
            asides = [segment(i) for i in range(24, 32)]
            asides.insert(3, segment(40, "aside"))
            asides.insert(7, segment(41, "aside"))
            write("aside", asides)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("output", type=Path)
    parser.add_argument("--language", default="it", choices=sorted(CONFIGS))
    parser.add_argument("--clips", type=int, default=48)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    clips = download(args.output, args.language, args.clips)
    roomed(args.output, clips)
    specs(args.output, clips, args.language)
    print(f"{len(clips)} clips, specs in {args.output / 'specs'}")


if __name__ == "__main__":
    main()
