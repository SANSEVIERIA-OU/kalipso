# Performance

Measured on 2026-10-29, and again on 2026-11-03 for the one-window app (startup, idle, steady speed), with the commands below, on an AMD Ryzen 5 PRO 6650U (12 threads), Debian 13, Qt 6.8.2, Release build, X11 (Xvfb, software rendering). Numbers on your machine will differ; the commands let you reproduce them.

## Results

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

## Not measured yet

- **Speech recognition** itself: model load time, CPU while listening and the delay from a spoken word to the highlight depend on the engine, the model and the machine. `benchmarks/asr/` contains the harness (`README.md` there); it needs the models and recorded test audio, which are not in the repository.
- **Plasma and GNOME** frontends: their cost is part of the desktop shell's process and needs a profiler on a real session.
- **macOS and Windows**, GPU-accelerated rendering, high-DPI and multi-monitor setups.

## Reproduce

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Startup time, p50/p95 over 20 runs (JSON); --budget-p95 MS makes it a check
python3 tools/measure-startup.py build/kalipso --runs 20

# Tracker latency, JSON per script size (microseconds)
build/tracker_benchmark

# CPU and memory of the app (process tree, PSS/RSS from /proc)
python3 tools/measure-process.py --seconds 10 -- build/kalipso --demo --prompter
python3 tools/measure-process.py --seconds 10 -- build/kalipso --demo --steady
dbus-run-session -- python3 tools/measure-process.py --seconds 10 -- build/kalipso-engine --serve
```

`--demo` uses a sample script and never touches your saved scripts. Run under `xvfb-run -a env QT_QPA_PLATFORM=xcb` for a headless X11 server.
