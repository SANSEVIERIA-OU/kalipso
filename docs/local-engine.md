# Voice following on Linux

Voice following needs three things on your computer:

| Component | What it does | How you get it |
| --- | --- | --- |
| `kalipso-engine` (the companion) | Captures the microphone and runs recognition for the widget and extension | Included in the Kalipso packages, in the `kalipso-companion` download, or built from source |
| A speech runtime | Moonshine (with its ONNX Runtime) **or** Vosk | Downloaded by Kalipso when you choose a language, or a library you already have |
| A language model | Recognition for one language | Downloaded by Kalipso when you choose a language, or a model folder you already have |

The standalone app runs recognition itself but uses the same runtimes and models: a language downloaded in
the app is ready in the widget and extension, and the other way round.

Recognition stays on your computer: audio goes from PipeWire to the native library and nowhere else. Only the selected model is loaded, and only while you are listening.

## Choose your language

Choose yours: in Kalipso, **Settings → Voice**; in the Plasma widget, **Configure Kalipso… → Voice**; in
the GNOME extension, **Settings → Language you speak**. The first time, Kalipso downloads its speech model (40 to 60 MB, with the engine's runtime if it is
not there yet), shows the progress with **Cancel download**, checks every file against the SHA-256
fingerprint pinned in [the catalog](../tools/data/speech-catalog.json) and keeps nothing that does not
match. Then **Follow my voice** and **Start**. Choosing a language never opens the microphone,
and nothing you say is sent anywhere. Downloads go to `~/.local/share/kalipso`; they use the system proxy
settings.

| Engine | Version | Languages | Platform |
| --- | --- | --- | --- |
| Moonshine (Tiny streaming) | 0.1.5 | en, de, es, ja, zh | Linux x86_64, glibc 2.34+ |
| Vosk (small models) | 0.3.50, built from source | en, de, es, ja, zh, it | Linux x86_64, glibc 2.17+ |

Kalipso picks the engine that follows the language best: Moonshine where it has the language, Vosk
otherwise (Italian uses Vosk, as there is no Italian Moonshine model).

With a Vosk small model, Kalipso gives the recognizer the script while you read: it expects the
script's sentences, numbers as you say them ("2011" as *duemilaundici*), and hears anything else as
unknown. In a room with some echo and noise this keeps it on your words: on Italian read speech the
word error rate fell from 28% to 6%, and the reader was within two words of the speaker at 93% of
sentence ends instead of 62% (`benchmarks/follow_probe.cpp`, recordings made room-like on purpose;
not a measurement of your microphone). Because such a recognizer can hear a few of the script's words
in anything said off the script, the reader then moves ahead past nearby words only on four words
heard right, and jumps elsewhere in the script only on five (two and four with Moonshine, which hears
any words). The Plasma widget and the GNOME extension
send their script to the companion too; an older companion ignores it.

The same from a terminal, for example over SSH or in a script:

```sh
kalipso-voice-setup --language it            # installed packages; ~/.local/bin/kalipso-engine --download --language it otherwise
kalipso-voice-setup --list-models            # the languages, and which are installed
kalipso-voice-setup --language en --backend vosk   # another engine than the usual one
```

## Get the companion

**You installed a Kalipso package** (AUR `kalipso-bin` or `kalipso`, the `.deb`, the `.rpm`): the
companion is already there.

**You use only the widget or the extension**, without the app: download
`kalipso-companion-…-linux-x86_64.tar.gz` from the [releases page](https://github.com/SANSEVIERIA-OU/kalipso/releases),
then:

```sh
tar -xzf kalipso-companion-*-linux-x86_64.tar.gz
cd kalipso-companion-*-linux-x86_64
./install
```

It copies the companion to `~/.local/bin` and registers it with your desktop. It needs Qt 6 and PipeWire,
which current Plasma and GNOME desktops have. If the widget says the companion is missing right after
installing, log out and back in so D-Bus discovers it.

**From the source code** (a clone of this repository, a C++ compiler, CMake, Qt 6 and PipeWire):

```sh
# Debian 13 / Ubuntu 24.04 or newer
sudo apt install build-essential cmake git qt6-base-dev libpipewire-0.3-dev pkg-config pipewire-bin python3
# Arch / EndeavourOS
sudo pacman -S --needed base-devel cmake git qt6-base pipewire pipewire-audio pkgconf python

git clone https://github.com/SANSEVIERIA-OU/kalipso.git
cd kalipso
python3 tools/install-local-engine.py --language it
```

It builds the companion, installs it under `~/.local` with its D-Bus service and downloads the language.

## Use it

In the widget, choose **Follow my voice** in the mode menu under the text (or in **Configure Kalipso… →
Reading**); in the extension, **Settings → Follow my voice**. Then **Start**.

The companion also gives the widget and the extension the scripts you write in the Kalipso app
(`~/.local/share/sansevieria-ou/kalipso/scripts`), over D-Bus (`ListScripts`, `GetScript`). It only reads
them; **Edit in Kalipso** (`OpenInApp`) makes the script the app's current one and starts the app. Pause releases the microphone and keeps your place. Stop listening before changing the language; only one frontend can listen at a time.

To see what is happening, run `~/.local/bin/kalipso-engine --serve` (or `kalipso-engine --serve`) in a terminal and start listening.

## Your own runtime or model

```sh
python3 tools/install-local-engine.py --backend vosk --language it \
  --library /usr/lib/libvosk.so --model /path/to/vosk-model-small-it-0.22

python3 tools/install-local-engine.py --backend moonshine --language de \
  --library /path/to/moonshine_voice/libmoonshine.so \
  --model /path/to/tiny-streaming-model --architecture 2
```

In the app, **Settings → Language you speak → Other model installed by hand…** asks for the same folder
and library.

- A Vosk model folder contains `am/final.mdl` and `conf/mfcc.conf`. Arch packages the runtime as [`vosk-api`](https://archlinux.org/packages/extra/x86_64/vosk-api/).
- A Moonshine model is a complete *streaming* model folder (tokenizer, frontend, encoder, adapter, decoder and `streaming_config.json`); keep the ONNX Runtime library next to `libmoonshine`. Architectures: 2 Tiny, 3 Base, 4 Small, 5 Medium. Only Tiny is in the catalog and has been exercised here.

Without the Python installer:

```sh
cmake -S . -B build-engine -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-engine --target kalipso-engine
cmake --install build-engine --component Engine
~/.local/bin/kalipso-engine --configure --backend vosk --language it \
  --library /usr/lib/libvosk.so --model /path/to/model
```

Configure and install with the same prefix: the D-Bus service file records the executable's path.

## Check and change settings

```sh
~/.local/bin/kalipso-engine --list-backends   # engines and their saved models
~/.local/bin/kalipso-engine --check           # what is missing, as JSON
~/.local/bin/kalipso-engine --configure --microphone 'PIPEWIRE_NODE_NAME'   # '' for the default
```

`--check` looks for the library symbols and model files without loading the model or opening the microphone. Settings can only change while nobody is listening.

## Behaviour and limits

- Stopping cancels capture and unloads the model. The companion exits after 90 seconds unused; if a frontend disappears, its session ends within ten seconds.
- If recognition falls more than two seconds behind, listening stops with a message instead of buffering without limit.
- Numbers are matched as words: `100%` in the script and "one hundred percent" spoken do not match.
- The companion also serves **Choose background window…** in the Plasma widget, which needs PipeWire and a window-capable desktop portal (`xdg-desktop-portal-kde` on KDE) but no speech model. See [window colors](accessibility.md#window-colors-on-kdewayland).

## Vosk built from source

PyPI's newest Vosk is 0.3.45, the runtime the catalog downloads. Vosk's newest tag, v0.3.50, comes with
no built files. `.github/workflows/vosk-runtime.yml` builds it from source (#26):
- **How:** with `tools/vosk/build-libvosk.sh` in the `manylinux2014_x86_64` image, which is pinned by
  digest. The script follows upstream's own manylinux build.
- **Dependencies:** Kaldi (branch `vosk`), OpenFST, OpenBLAS 0.3.20 and CLAPACK, each at the commit
  upstream's build would have used on the day of the tag. `BUILD-INFO.txt` in the archive records the
  commits. Two separate builds on GitHub gave the same archive, byte for byte (SHA-256 `404e9f15…`).
- **What it makes:** `vosk-0.3.50-linux-x86_64.zip`, laid out like the wheel's `vosk/` folder, with
  the five licences.

`tools/vosk/check-runtime.py` then compares the result with the 0.3.45 wheel:
- the C functions Kalipso calls;
- the glibc and libstdc++ versions each library needs;
- 25 FLEURS Italian sentences (392 s), plain and with the grammar Kalipso builds from a script.

The results, on 3 November 2026:
- **Functions:** all of Kalipso's are present. The new library exports 41 `vosk_` functions, against 36.
- **Requirements:** glibc 2.14 and GLIBCXX 3.4.18, against glibc 2.4 and GLIBCXX 3.4.13. That is
  within the manylinux2014 baseline, glibc 2.17.
- **Recognition:**

  | Runtime | WER, plain | WER, grammar | Decode time / audio, plain · grammar | Peak memory |
  | --- | --- | --- | --- | --- |
  | 0.3.45 (PyPI wheel) | 11.4% | 5.8% | 0.059 · 0.016 | 251 MiB |
  | 0.3.50 (built here) | 11.4% | 5.8% | 0.061 · 0.014 | 249 MiB |

  The 25 transcripts are word for word the same in both modes. Speed and memory were measured on
  GitHub's Ubuntu runner.
- **Following a reading** (`benchmarks/follow_probe.cpp`, with the grammar): also the same. Sentence
  ends reached were 98% clean, 90% room-like and 52% noisy for both runtimes.

The catalog downloads 0.3.50 from Kalipso's own release `vosk-runtime-0.3.50`, by its SHA-256. A
runtime 0.3.45 that is already installed keeps working; new downloads get 0.3.50.

## Remove

Stop listening and remove the widget or extension first. Then delete:

```sh
rm ~/.local/bin/kalipso-engine
rm ~/.local/share/dbus-1/services/io.github.sansevieria_ou.Kalipso.Engine.service
rm -r ~/.local/share/licenses/kalipso-engine
rm -r ~/.config/Kalipso                       # engine settings
rm -r ~/.local/share/kalipso/runtimes ~/.local/share/kalipso/models   # downloaded speech files
```

`~/.local/share/kalipso/gnome-extension.json` holds the GNOME extension's script; keep it if you want your script back later. Libraries and models you supplied yourself are not touched.
