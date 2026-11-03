Upstream native ASR notices, pinned 2026-10-26

ONNX Runtime 1.23.2 (Moonshine's bundled runtime):
- Source tag: microsoft/onnxruntime v1.23.2, commit a83fc4d58cb48eb68890dd689f94f28288cf2278.
- LICENSE: https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.2/LICENSE
  SHA-256 2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c
- ThirdPartyNotices.txt: https://raw.githubusercontent.com/microsoft/onnxruntime/v1.23.2/ThirdPartyNotices.txt
  SHA-256 e9e90971a8e75a9a8ac0c6412e29c1202d079998389915aa485f46c816c3b4cc
- Main ONNX Runtime license is MIT. If redistributing its shared object, retain the MIT copyright/license and its ThirdPartyNotices.txt for the third-party components included in that build.

Vosk API 0.3.50, built from source by .github/workflows/vosk-runtime.yml (#26):
- Source tag: alphacep/vosk-api v0.3.50, commit a7bf6a51e299152a8fb496b928a21eb79a1d7bea.
  vosk-0.3.50-COPYING (Apache License 2.0), SHA-256 bb618af833c817ab01c1564bd3ec75ba4b4f9cf30910a4a2168e30a674ee8cc6
- libvosk.so links these statically, so their licences go with it:
  - Kaldi, alphacep/kaldi commit 2b69aed630e26fb2c700bba8c45f3bd012371c5c (Apache 2.0):
    vosk-0.3.50-kaldi-COPYING, SHA-256 3fd3dd523b1717f1fb2ae40896df943d767fa5e664e46156c2dba7094f0d7172
  - OpenFST, alphacep/openfst commit 7dfd808194105162f20084bb4d8e4ee4b65266d5 (Apache 2.0):
    vosk-0.3.50-openfst-COPYING, SHA-256 dd0c6c9378a98b6a0e96b559b285c8506434fe2691eeeef151e8a8c1c492778e
  - OpenBLAS v0.3.20, commit 0b678b19dc03f2a999d6e038814c4c50b9640a4e (BSD-3-Clause):
    vosk-0.3.50-openblas-LICENSE, SHA-256 190b5a9c8d9723fe958ad33916bd7346d96fab3c5ea90832bb02d854f620fcff
  - CLAPACK, alphacep/clapack commit 67d9d1aa7261a1a8b457af3f38ea974946935b02 (BSD-style):
    vosk-0.3.50-clapack-COPYING, SHA-256 23014869214a231525b38c39fdb5f82364fa24154ac80fdcab2a79671460fd09
- The 0.3.45 PyPI wheel (the runtime before) linked the same libraries; its notice was the Vosk COPYING alone.

The installer downloads upstream artifacts to the user's machine, rather than committing these binaries to the app repository. The user-local installation should retain the corresponding notice files alongside/presented with each selected backend. This note is operational guidance, not legal advice.
