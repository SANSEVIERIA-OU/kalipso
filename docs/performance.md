# Performance

Kalipso's performance suite measures the production Qt widgets, tracker and speech workers. It runs sequentially on Linux, Windows and macOS, writes machine-readable results, and checks conservative regression budgets. It does not measure a physical microphone or the delay until a monitor displays a frame.

## Method

- Release builds. The manifest records commit, working-tree diff fingerprint, OS/kernel, CPU, runner image, Qt paths and compiler flags. OS caches are not flushed; there is one unreported startup warm-up and one separately labelled warm-up per speech engine.
- Startup: 20 launches of the actual app with `--demo --quit-after 1`. This is process launch through window setup, first event-loop turn and shutdown, **not** compositor presentation or a cold boot.
- Editor, reader, steady scrolling at 150 words/min, and automatic screen colors: three fresh processes per workload, 2.2 seconds to settle, then approximately 10 seconds of CPU and RSS sampling. The UI harness links the production application library and uses isolated settings and scripts. Its 100 ms memory sampling overhead is included. Scrolling must advance the reading position; color sampling must produce at least two successful updates during the measured interval.
- Editing: three processes, each with a 1,000-word synthetic script; five unreported warm-up edits and 60 measured edits. A key event inserts a character, propagates the text to the tracker and renders the window to a pixmap. The character is removed between measurements, keeping the workload constant. This includes layout/paint, excludes physical input, compositor and display latency. It verifies that the edit was applied.
- Speech: actual Vosk and Moonshine libraries and models from the production SHA-256-pinned catalog. Download and extraction happen in a separate process, before timing. Each engine runs three times at maximum throughput (`batch`) and three times paced at recording speed (`paced`), in fresh processes. Vosk receives the script vocabulary as in the app; Moonshine uses `MOONSHINE_ORT_SINGLE_THREAD=1`, also as in the app. The same 200 ms PCM chunks reach the production speech worker and tracker. Four seconds of trailing silence allow finalization.
- Synthetic fixtures: espeak-ng Italian and English speech, PCM16 mono 16 kHz, about 15 seconds per language. Audio and script fingerprints, native runtime version and model name accompany every result. These test real inference with synthetic speech; they are not microphone or recognition-accuracy measurements.

CPU is a percentage of **one logical core**, from process user + system CPU counters, including recognizer threads. Windows `GetProcessTimes` can undercount short bursts separated by sleeps: some Vosk/scrolling runs report almost zero CPU despite measurable processing time. Windows reports also retain `QueryProcessCycleTime` deltas to show that work occurred; cycles are not converted into seconds using an assumed clock rate. Do not interpret zero as no work or compare those CPU percentages with Linux/macOS. RSS includes shared pages and is not memory uniquely owned by Kalipso. Speech peak RSS is the OS process high-water mark, including model initialization; it is not the whole GUI application's memory. Different OS memory counters and runners are not an apples-to-apples comparison.

`decode_rtf` divides time inside consume calls (including tracker callbacks) by input duration **including the four-second flush**. Under 1 means that this workload processes faster than playback overall. It does not guarantee every update fits in 200 ms. `chunk_p50_ms`/`chunk_p95_ms` describe processing calls, not spoken-word-to-highlight delay. `max_backlog_ms` in paced runs is the largest completion delay relative to when that audio chunk became available. First transcript/advance times are measured from stream start, after model loading, and include leading silence. There are no reference word timestamps, so no word alignment latency or WER claim is made.

## Measurements, 7 November 2026

Local Linux baseline: AMD Ryzen 5 PRO 6650U, 6 cores/12 threads, Debian 13 userland on Linux 7.2.14, GCC 14.2, Qt 6.8.2, Release, Xvfb 1280×900 at 1×, software rendering. No builds or other benchmarks ran concurrently; the host was not exclusively reserved (initial load average about 3). Figures below are medians across three processes unless specified otherwise.

| Local Linux workload | CPU, one core | RSS | Latency |
| --- | ---: | ---: | --- |
| Actual app startup, 20 runs | — | — | p50 365 ms; p95 415 ms |
| Editor idle | 0.18% | 78.4 MiB | — |
| Reader idle | 0.18% | 75.9 MiB | — |
| Steady scrolling, 150 words/min | 1.92% | 76.4 MiB | — |
| Automatic colors, X11 pixel sampling | 0.21% | 76.4 MiB | — |
| Edit → tracker → pixmap, 1,000 words | — | — | p50 6.7 ms; p95 8.6 ms (run p95 range 7.0–10.2 ms) |
| Vosk Italian, paced | 2.22% | 202.7 MiB peak | load 398 ms; chunk p95 7.1 ms; max backlog 8.5 ms |
| Moonshine English, paced | 22.91% | 211.0 MiB peak | load 100 ms; chunk p95 228 ms; max backlog 378 ms |

The speech models are `vosk-model-small-it-0.22` with Linux Vosk 0.3.50, and `tiny-streaming-en-quantized_26_08_21` with Moonshine 0.1.5. Median batch RTF is 0.019 for Vosk and 0.225 for Moonshine; median paced RTF is 0.022 and 0.231. All measured synthetic runs reached the last script word. Moonshine's sporadic updates exceed a chunk's 200 ms duration even though total throughput is comfortably faster than playback: that is recorded as backlog rather than hidden by average RTF.

### Native GitHub runners

The same suite and budgets passed on all three native lanes at benchmark commit `c1ca32d` ([Linux run](https://github.com/SANSEVIERIA-OU/kalipso/actions/runs/42373025224), [Windows/macOS run](https://github.com/SANSEVIERIA-OU/kalipso/actions/runs/42373025215)). These are virtual runner machines, not the reporter's PC. Hardware differs, so the table describes those runs rather than ranking operating systems.

| Environment | Startup p50 / p95 | Edit-to-pixmap p95 | Idle reader RSS | Scrolling CPU, one core |
| --- | ---: | ---: | ---: | ---: |
| Ubuntu 24.04, EPYC 7763 (4 vCPU), Qt 6.4.2, Xvfb | 303 / 306 ms | 6.9 ms | 70.5 MiB | 1.55% |
| Windows Server 2022, EPYC 9V74 (4 vCPU), Qt 6.12.0, native Windows | 338 / 430 ms | 8.8 ms | 72.4 MiB | Not reliable for short bursts; see accounting note |
| macOS 26.6.2, virtual Apple M1 (3 vCPU, 7 GiB), Qt 6.12.0, Cocoa | 1,273 / 1,638 ms | 24.2 ms | 143.8 MiB | 1.55% |

Paced synthetic recognition also passed in all repetitions. Median model-load time / peak RSS: Vosk Italian **410 ms / 198.9 MiB** on Linux, **390 ms / 170.1 MiB** on Windows, **216 ms / 186.6 MiB** on macOS; Moonshine English **121 ms / 218.2 MiB**, **183 ms / 147.7 MiB**, **79 ms / 404.6 MiB**, respectively. Vosk runtime versions differ by platform: Linux 0.3.50, Windows 0.3.45, macOS 0.3.42; Moonshine is 0.1.5. Native compiler/tool versions and runner image identifiers are in the build logs/manifests. macOS's first suite attempt hit an existing remote-network test failure; the repeated full native tests passed, without changing production network behavior.

### Recorded human speech, same Linux host

A supplementary workload uses the first five distinct English FLEURS test transcripts in dataset order, from the previously archived benchmark input, concatenated with 0.4 seconds of silence per clip: 45.32 seconds, 92 script words. No clips were selected by recognition results. The PCM copies supplied to three earlier engine runs were checked byte-for-byte; source records and new PCM fingerprints are retained with the private results. [FLEURS](https://huggingface.co/datasets/google/fleurs), revision `70bb2e84b976b7e960aa89f1c648e09c59f894dd`, is CC BY 4.0. This is playback of human recordings, not microphone capture in a room.

| Engine, English, paced | CPU, one core | Peak RSS | Model load | Chunk p95 | Largest backlog per run, median | RTF |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Vosk 0.3.50, small-en-us-0.15 | 2.37% | 165.0 MiB | 290 ms | 7.5 ms | 16 ms | 0.024 |
| Moonshine 0.1.5, tiny streaming | 35.03% | 228.9 MiB | 160 ms | 487 ms | 1,257 ms | 0.357 |

Three batch and three paced runs per engine, plus a separate warm-up, all reached the final script word. Reaching the end does not establish that every intermediate word was recognized or highlighted correctly. Moonshine's burst latency is materially larger on this recording than on the synthetic fixture, despite sufficient average throughput. This warrants a broader speech-quality/latency study before making responsiveness claims; no unvalidated decoder change is bundled with the benchmark work.

Raw JSON, logs, screenshots and audio stay in ignored `artifacts/` and CI artifacts, outside the versioned source distribution.

## Regression budgets

`tools/performance-report.py --check` rejects missing repetitions, unfinished runs, failed workloads, invalid/non-finite metrics, and these ceilings:

| Measurement | Ceiling | Reason |
| --- | ---: | --- |
| Startup p95 | 2,000 ms | Existing budget; observed local baseline 415 ms |
| Edit-to-pixmap p95 | 100 ms | Local run maxima 10.2 ms; initial Windows runner below 16 ms |
| Idle editor / reader CPU | 5% of one core | Wide guard above the measured fraction of one core |
| Scrolling / automatic-color CPU | 15% / 10% of one core | Allows runner variation; catches continuous expensive work |
| UI median RSS | 256 MiB | Above measured roughly 75 MiB on Linux/Windows and 149 MiB on macOS |
| Speech model load | 10,000 ms | Detects newly blocking initialization; downloads are never included |
| Speech peak RSS | 1 GiB | Above measured approximately 150–405 MiB across the native lanes |
| Speech RTF | 0.9 | Requires overall headroom before work becomes slower than playback |

These are broad regression guards, not supported-hardware requirements. Keep the individual runs when investigating a failure; do not silently widen a ceiling to make a run pass. CPU/latency changes require comparison on the same host and configuration. No production optimization is claimed solely from differences between these harness results and earlier measurements.

Counterchecks deliberately exceed a budget and supply missing, failed, negative and non-finite data. A real offscreen color run must fail because it produces no screen samples; X11 must pass. This prevents a non-working feature from being reported as cheap.

## Reproduce

Build dependencies are those of the app, plus espeak-ng and ffmpeg to generate synthetic audio. The runner/report use Python's standard library; no new runtime dependency is added to Kalipso.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
python3 tools/make-speech-fixtures.py artifacts/speech-fixtures
xvfb-run -a -s '-screen 0 1280x900x24' env QT_QPA_PLATFORM=xcb \
  python3 tools/run-performance.py --fixtures artifacts/speech-fixtures \
  --output artifacts/performance-run-1
python3 tools/performance-report.py artifacts/performance-run-1 --check
python3 tests/performance_metrics_test.py
```

On Windows/macOS, use the same Python commands without `xvfb-run`; the runner finds the Release executable or app bundle. The CI `macOS and Windows` and Linux distribution jobs perform the same sequence and upload `performance-*` artifacts for 30 days. Use a new output directory for every experiment. A failed benchmark must not overwrite a previous successful result.

Individual probes:

```sh
build/tracker_benchmark
build/ui_benchmark interaction 10
build/ui_benchmark colors 10
build/speech_benchmark vosk it artifacts/speech-fixtures artifacts/performance-models prepare
build/speech_benchmark vosk it artifacts/speech-fixtures artifacts/performance-models paced
```

For a recording, supply `<language>.pcm` and `<language>.txt` in a separate fixture directory, retaining source/license and fingerprints with the results. Use the speech probe directly and label that experiment as recorded audio. The full runner labels its fixtures as synthetic; do not substitute human recordings without changing the experiment's manifest. The broader accuracy/room-noise laboratory is documented in [benchmarks/asr/README.md](../benchmarks/asr/README.md).

## Common-task steps and remaining physical checks

With a script open and voice already configured: editing happens directly in the large reader (no view switch); moving under the camera takes one button; start/pause takes one button or Ctrl+Enter; pausing takes Escape; opening another script takes the scripts button/F9 then selecting the script. These are interface action counts, not completion times from a usability study. Initial language/model setup and download time are separate.

Not measured by this suite: microphone capture and permission prompts; word-to-highlight timing with aligned recordings; actual compositor/display presentation; GPU-backed Wayland, high DPI and multiple monitors; Plasma/GNOME shell costs; battery/electrical energy. macOS automatic color capture is omitted because it requires an interactive Screen Recording permission. [Manual testing](manual-testing.md) describes those checks.

## Historical measurements

The earlier results below are retained for context. They used a different measurement harness and must not be treated as a controlled before/after comparison with the new suite.


| What | Result |
| --- | --- |
| Standalone app, start to a working window (`--demo --quit-after 1`, offscreen, 20 runs) | p50 365–490 ms · p95 515–665 ms |
| Standalone app (one window), idle | 0.0% of one CPU core · 37 MiB PSS · 65 MiB RSS |
| Standalone app (one window), steady scrolling at 150 words/min | 2.3% of one CPU core · 37 MiB PSS |
| Before the one-window app (2026-10-29): editor and reader open, idle | 0.0% of one CPU core · 44 MiB PSS · 62 MiB RSS |
| Before the one-window app: steady scrolling at 140 words/min | 1.0% of one CPU core · 45–50 MiB PSS |
| `kalipso-engine` companion, idle | 0.0% of one CPU core · 10 MiB PSS · 17 MiB RSS |
| Tracker: a recognized phrase → new reading position (C++) | about 35 µs median for 1,000–50,000-word scripts |
| Tracker: worst case, off-script words found all over the script (C++) | 0.06 ms (1,000 words) · 0.11 ms (10,000) · 0.38 ms (50,000) median |
| Tracker: same two cases in JavaScript (Node 24) | 0.3–0.4 ms · up to 2 ms at 50,000 words |
| Linux `.deb` (app + companion) | 422 KiB download, 1 MiB installed |
| Plasma widget / GNOME extension / KWin script | 34 KiB / 36 KiB / 15 KiB |

CPU percentages of 2026-10-29 are three 10-second runs each, after a one-second warm-up; all three runs agreed within 0.01 percentage points. Those of 2026-11-03 are one 10-second run each, with other builds running on the machine; the startup range is two series of 20 runs in those conditions. Steady scrolling measured 2.3% against 1.0% before; with other work on the machine, part of the difference may be noise, and the cause has not been looked into. PSS counts shared libraries proportionally and is the better estimate of what Kalipso adds to memory use.

The **startup budget** in CI (`Native build and tests`, step *Startup time within budget*) fails when p95 goes over 2,000 ms on GitHub's runner: about three times the p95 above, so it catches a startup that starts waiting on something new (a network call, a model load) rather than runner noise.

The tracker's whole-script search, which lets the reader recover after you skip ahead or go back, used to scan every word and took 18 ms per recognized phrase on a 50,000-word script. It now only checks the places where the rarest spoken words occur.
