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


Guided Windows/macOS downloads (catalog schema 2, updated 7 November 2026):
- Moonshine 0.1.6: official PyPI Windows x64 and macOS 15+ arm64 wheels. Keep the wheel's
  MIT LICENSE plus the ONNX Runtime 1.23.2 LICENSE and ThirdPartyNotices above. Windows uses
  the Microsoft VC runtime already supplied with Kalipso; keep its app-local distribution notices.
  ONNX Runtime binaries are byte-identical to the 0.1.5 wheels on all three supported targets;
  the existing 1.23.2 licence and third-party notices still apply.
- Vosk Windows x64: official PyPI 0.3.45 wheel, including libvosk.dll, libgcc_s_seh-1.dll,
  libstdc++-6.dll and libwinpthread-1.dll. GCC 10 is identified in the binaries; the precise
  GCC patch release is not established. The COPYING3 and COPYING.RUNTIME texts below come from
  GCC's 10.2.0 tag and are licence texts, not an assertion of that binary's exact patch release.
  The winpthreads binary identifies mingw-w64 8.0.0. Keep GPLv3 plus the GCC Runtime Library
  Exception 3.1 for libgcc/libstdc++, and the winpthreads COPYING (MIT/BSD notices).
- Vosk macOS: official PyPI 0.3.42 universal2 wheel, libvosk.dyld (its upstream filename).
  It uses Apple's system Accelerate framework. The base and initial-grammar C APIs are supported;
  Kalipso recreates its recognizer between utterances if the grammar-update API is absent.
- These are different upstream artifacts from the Linux 0.3.50 source build. The catalog records
  version, HTTPS URL, SHA-256 and the selected notice files for each platform. No claim is made
  that a newer tag with no binary assets upgrades an older native wheel.

Additional licence-text sources (filenames under vosk/):
- gcc-10.2.0-COPYING3.txt
  https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-10.2.0/COPYING3
  SHA-256 8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903
- gcc-10.2.0-COPYING.RUNTIME.txt
  https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-10.2.0/COPYING.RUNTIME
  SHA-256 9d6b43ce4d8de0c878bf16b54d8e7a10d9bd42b75178153e3af6a815bdc90f74
- winpthreads-v8.0.0-COPYING.txt
  https://raw.githubusercontent.com/mingw-w64/mingw-w64/v8.0.0/mingw-w64-libraries/winpthreads/COPYING
  SHA-256 63263614cdd29f2f93cba85e992f041b31f9fc7b4033692f31269489a8a1b177
- vosk-api-v0.3.45-COPYING.txt
  https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/COPYING
  SHA-256 bb618af833c817ab01c1564bd3ec75ba4b4f9cf30910a4a2168e30a674ee8cc6
- vosk-api-v0.3.42-COPYING.txt
  https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.42/COPYING
  SHA-256 bb618af833c817ab01c1564bd3ec75ba4b4f9cf30910a4a2168e30a674ee8cc6
- kaldi-vosk-android-COPYING.txt
  https://raw.githubusercontent.com/alphacep/kaldi/vosk-android/COPYING
  SHA-256 3fd3dd523b1717f1fb2ae40896df943d767fa5e664e46156c2dba7094f0d7172
- openfst-alphacep-master-COPYING.txt
  https://raw.githubusercontent.com/alphacep/openfst/master/COPYING
  SHA-256 dd0c6c9378a98b6a0e96b559b285c8506434fe2691eeeef151e8a8c1c492778e
- openblas-v0.3.20-LICENSE.txt
  https://raw.githubusercontent.com/OpenMathLib/OpenBLAS/v0.3.20/LICENSE
  SHA-256 190b5a9c8d9723fe958ad33916bd7346d96fab3c5ea90832bb02d854f620fcff
- clapack-v3.2.1-COPYING.txt
  https://raw.githubusercontent.com/alphacep/clapack/v3.2.1/COPYING
  SHA-256 23014869214a231525b38c39fdb5f82364fa24154ac80fdcab2a79671460fd09
