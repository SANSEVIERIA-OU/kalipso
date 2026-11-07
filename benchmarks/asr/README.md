# Local ASR benchmark tools

CPU-only benchmark runners for Vosk, Whisper and Moonshine. Input specifications are in [models.json](models.json); results and downloaded data belong outside Git.

The Python laboratory below intentionally pins the historical 0.1.5 baseline. For the current native runtime and catalog, use the [production performance suite](../../docs/performance.md). Do not mix results from different runtime/model versions.

## Reproduce

Requires Linux with at least two physical cores available, Python 3, Node.js, a C++20 compiler, CMake, Ninja and Qt 6 Core development files. The Moonshine runner additionally uses an isolated Python environment. Reserve several GiB for temporary data/models; FLEURS test archives are streamed and can transfer substantially more than the selected five minutes of audio.

```sh
python3 -m venv /tmp/kalipso-moonshine
/tmp/kalipso-moonshine/bin/pip install moonshine-voice==0.1.5

git clone https://github.com/ggml-org/whisper.cpp /tmp/kalipso-whisper-compare
git -C /tmp/kalipso-whisper-compare checkout 231638a0717b7822784297e5980965ffcec05810
cmake -S /tmp/kalipso-whisper-compare -B /tmp/kalipso-whisper-compare/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=OFF -DGGML_CUDA=OFF -DGGML_VULKAN=OFF
cmake --build /tmp/kalipso-whisper-compare/build --parallel 2

cmake -S benchmarks/asr -B build-asr-benchmark -G Ninja -DCMAKE_BUILD_TYPE=Release -DWHISPER_ROOT=/tmp/kalipso-whisper-compare
cmake --build build-asr-benchmark --parallel 2

python3 benchmarks/asr/prepare_audio.py --output /tmp/kalipso-asr-benchmark
python3 benchmarks/asr/prepare_models.py benchmarks/asr/models.json /tmp/kalipso-asr-benchmark
```

Obtain the Vosk 0.3.45 Linux x86-64 wheel from [PyPI](https://pypi.org/project/vosk/0.3.45/#files), unpack its `vosk/libvosk.so` into `/tmp/kalipso-speech-test/libvosk.so`, and retain the upstream provenance and checksum with local results. The C++ runner loads that library directly; installing the Vosk Python package into the host is unnecessary. A different architecture needs its corresponding native library and a separately identified experiment.

Run each command **after the preceding one finishes**:

```sh
python3 benchmarks/asr/run.py --phase batch --repeat-first 2 --moonshine-model-root /tmp/kalipso-asr-benchmark/models/moonshine
python3 benchmarks/asr/run.py --phase batch --repeat-first 2 --configs moonshine-tiny-single --moonshine-model-root /tmp/kalipso-asr-benchmark/models/moonshine
python3 benchmarks/asr/run.py --phase stream --moonshine-model-root /tmp/kalipso-asr-benchmark/models/moonshine
python3 benchmarks/asr/run.py --phase stream --configs moonshine-tiny-single --moonshine-model-root /tmp/kalipso-asr-benchmark/models/moonshine
python3 benchmarks/asr/run.py --phase controls --configs vosk-small whisper-tiny-q5 whisper-base-q5 whisper-small-q5 moonshine-tiny moonshine-tiny-single --moonshine-model-root /tmp/kalipso-asr-benchmark/models/moonshine
node benchmarks/asr/track_events.mjs artifacts/asr-benchmark stream
node benchmarks/asr/track_events.mjs artifacts/asr-benchmark controls
python3 benchmarks/asr/summarize.py artifacts/asr-benchmark
python3 -m unittest discover -s benchmarks/asr -p 'test_*.py'
```

Use `--out` for a new experiment. Existing `(id, configuration, repeat)` rows are skipped for crash recovery; do not reuse an output directory with different models, settings or hardware. `--data`, `--build-dir`, `--whisper-root`, `--vosk-library`, `--moonshine-python` and `--moonshine-model-root` relocate the laboratory paths. The data manifest stores absolute WAV paths; rerun preparation after moving the audio directory.

## What is measured

- `batch`: 25 identical human recordings per configuration, plus two extra runs of the first English clip. WER/CER use the original 25 only. A fresh process loads the model for every run; OS file caches are not flushed.
- `stream`: one preselected clip per language, audio fed at recording speed. Vosk feeds 200 ms; Moonshine 100 ms with minimum 500 ms updates; Whisper redecodes an 8-second rolling window every second. This is a simple untuned Whisper streaming policy, not an evaluation of every possible Whisper integration. After the final decode, its transcript contains only the last window and is never given a full-recording WER.
- `controls`: three seconds of digital silence; the first English recording with two seconds of silence inserted at second four. These are basic diagnostic probes, not a realistic noise corpus.
- `track_events.mjs`: replay actual events through the production JS tracker. The reference is given only to the tracker, never to an ASR engine. Final cursor position is not word alignment accuracy: wrong early jumps can produce high coverage. Backward moves are counted, not presumed all erroneous (some are legitimate hypothesis revisions). No word-level reference timestamps are available.

No external VAD, reference-derived vocabulary or decoder prompt is added. The streaming engines retain their own built-in segmentation; Whisper receives each window directly.

`decodeRtf` is decode time / recording duration. Under 1 means faster than the recording for that workload, not necessarily sufficient for repeated streaming updates. Batch Moonshine uses elapsed wall time through `stream.stop()`; its summed call time is retained separately. Paced stream decode time excludes intentional sleeps. `finishLagSeconds` measures completion beyond the input recording duration, including finalization; `firstNonemptySeconds` and tracker first advancement start **after model loading** and include any original leading silence.

`cpuSeconds` is user + system CPU from `wait4` across the whole process, including initialization and shutdown. It is not wall latency or electrical energy. Peak RSS is the kernel process high-water mark; peak PSS is sampled every 50 ms and can miss brief peaks. Python/NumPy overhead is included for Moonshine, whereas Vosk and Whisper use native C++ runners.

The parent requests affinity to the first logical CPU of two different physical cores. Whisper requests two threads. **Moonshine's default ORT pools can override inherited thread affinity**, so this is not a uniform two-core cap. The separate `moonshine-tiny-single` configuration sets `MOONSHINE_ORT_SINGLE_THREAD=1`; sampled thread affinities and CPU/wall ratios provide a check of its actual behavior. This setting is documented upstream for serial ORT execution, not a documented arbitrary thread-count API. The comparison retains the default configuration so its CPU cost is visible. No inference jobs run concurrently.

Text scoring uses Unicode NFKC + case folding, keeps letters/marks/numbers, converts punctuation/symbols to separators, and collapses whitespace. Accents are preserved; written numbers and digits are not equated. WER uses whitespace units only for EN/DE/ES. JA/ZH use character error rate; CER excludes spaces for every language. Edit rates may exceed 100% because of insertions. Aggregates divide total edits by total reference units, rather than averaging clip percentages. The runtime's Unicode database version is recorded.

## Following a reading (`follow_probe`)

`benchmarks/follow_probe.cpp` runs recorded speech through the app's own speech worker and tracker, at recording time, and
measures how closely the reader follows the speaker. The recordings are FLEURS test sentences joined into scripts of
twelve, plus room-like versions made on purpose (reverberation and steady noise, a quiet microphone): simulations, not a
microphone in a real room.

```sh
cmake --build build --target follow_probe
python3 benchmarks/asr/prepare_follow.py /tmp/kalipso-follow --language it     # needs NumPy for the room versions
build/kalipso-engine --download --language it                                   # Vosk runtime and model, as the app does
LIB=~/.local/share/kalipso/runtimes/vosk-0.3.50/vosk/libvosk.so
MODEL=~/.local/share/kalipso/models/vosk/vosk-model-small-it-0.22
for condition in clean room noisy; do
  mkdir -p /tmp/follow-results/$condition
  for spec in /tmp/kalipso-follow/specs/$condition/*.json; do
    build/follow_probe $LIB $MODEL $spec > /tmp/follow-results/$condition/$(basename $spec)   # add --bias as the app does
  done
done
python3 benchmarks/asr/follow_summary.py /tmp/follow-results
```

`--bias` gives the recognizer the script as the app does (`speechVocabulary`); `--backend moonshine` uses a Moonshine
model instead; `--record` and `--replay` save and replay transcripts, to compare trackers on the same ones. Word timings
are estimated (letters spread over the speech of each sentence), good to a word or two and the same for every
configuration. Measured 2026-11-02 on 48 Italian sentences (12 minutes, many speakers) with the Vosk small model:

| Audio | Before: any word, old matching | Script learnt by Vosk, new matching |
| --- | --- | --- |
| clean | WER 10.4%, within 2 words at 95% of sentence ends, 90% of words reached within 1.35 s | WER 4.3%, 99%, 0.81 s |
| room (RT60 0.4 s, 20 dB SNR) | WER 28.2%, 62%, 5.12 s, 26 words never reached | WER 5.8%, 93%, 0.52 s, 7 |
| noisy (RT60 0.6 s, 10 dB SNR) | WER 79.4%, 11%, 29 s, 296 | WER 34.0%, 56%, 5.3 s, 32 |

Two sentences said off the script moved the reader on by 0 words before and by at most 7 after. With the script learnt
and the old thresholds (two words heard right to move anywhere nearby, four to jump anywhere) they moved it on by 116 to
228 words: the recognizer hears runs of the script's words in anything said off the script. So the tracker asks for four
and five only when the recognizer learnt the script (`ScriptTracker::setGuided`, `SpeechService::guided`); with those
thresholds, a recognizer that hears any words lost the speaker more often in noise. Moonshine has no way to learn a
script; its transcripts differ slightly from run to run, so its numbers vary by a few points between runs.

## Provenance and rights

[FLEURS](https://huggingface.co/datasets/google/fleurs) by Google, revision `70bb2e84b976b7e960aa89f1c648e09c59f894dd`, is CC BY 4.0. Selection is the first five **distinct reference strings** in official test TSV order per language, fixed before inference. The TSV's first column is a dataset ID, not a speaker ID; speaker diversity is not established here. The WAVs are converted to PCM16, mono, 16 kHz. The manifest records source archive/member, transcript, revision and converted file hash. Reference excerpts remain CC BY 4.0 and are attributed to that dataset; software licensing does not replace their license.

[Vosk models](https://alphacephei.com/vosk/models) selected here are listed as Apache 2.0. [Whisper](https://github.com/openai/whisper/blob/main/LICENSE) and its [converted weights](https://huggingface.co/ggerganov/whisper.cpp) are MIT; [Moonshine's streaming models/runtime](https://github.com/moonshine-ai/moonshine) used here are MIT. Exact versions, direct sources and hashes are in `models.json`. Vosk archive hashes and directory digests are locally calculated fingerprints, not signed upstream authenticity proofs. Model/audio binaries are deliberately outside Git.
