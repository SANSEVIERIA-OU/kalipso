#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Summarise follow_probe reports: follow_summary.py RESULTS [RESULTS...]

Each RESULTS folder holds one sub-folder per audio condition (clean, room, noisy) with one
follow_probe JSON report per spec. One line per folder and condition:
reach   share of sentence ends where the reader was within two words of the speaker;
|beh|   mean distance in words from the speaker at sentence ends;
d50/d90 median and 90th percentile, over specs, of the delay (s) between a word being said and the
        reader reaching it (word times estimated from the speech in each clip, see follow_probe.cpp);
unr     words said but never reached;
aside+  words the reader moved on while the speaker said sentences that are not in the script;
back    backward moves; skipOK: read sentences after a skip that the reader caught;
wer     word error rate of the final transcripts on the read-N specs.
"""
import json
import sys
from pathlib import Path

print(f"{'results':24} {'cond':6} {'reach':>6} {'|beh|':>6} {'d50':>6} {'d90':>6} {'unr':>5} {'aside+':>6} "
      f"{'back':>5} {'skipOK':>6} {'wer':>6}")
for root in map(Path, sys.argv[1:]):
    for condition in ("clean", "room", "noisy"):
        reports = {path.stem: json.loads(path.read_text()) for path in sorted((root / condition).glob("*.json"))}
        if not reports:
            continue
        segments = [s for r in reports.values() for s in r["segments"]]
        reach = sum(abs(s["behind"]) <= 2 for s in segments) / len(segments)
        behind = sum(abs(s["behind"]) for s in segments) / len(segments)
        middle = len(reports) // 2
        d50 = sorted(r["delayMedian"] for r in reports.values())[middle]
        d90 = sorted(r["delayP90"] for r in reports.values())[middle]
        unreached = sum(r["unreached"] for r in reports.values())
        aside = reports["aside"]["asideAdvance"] if "aside" in reports else "-"
        backward = sum(r["backward"] for r in reports.values())
        skips = [r for name, r in reports.items() if name.startswith("skip")]
        caught = sum(abs(r["segments"][3]["behind"]) <= 2 for r in skips)
        reads = [r for name, r in reports.items() if name.startswith("read")]
        wer = sum(r["wer"] for r in reads) / max(1, len(reads))
        print(f"{root.name:24} {condition:6} {reach:6.2f} {behind:6.1f} {d50:6.2f} {d90:6.2f} {unreached:5} "
              f"{aside:>6} {backward:5} {caught:4}/{len(skips)} {wer:6.3f}")
