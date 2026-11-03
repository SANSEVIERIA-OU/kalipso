#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds libvosk.so for Linux x86_64 from the Vosk source at a pinned tag, with every dependency at a
# pinned commit, following upstream's own manylinux build (vosk-api travis/Dockerfile.manylinux and
# travis/build-wheels.sh). Runs inside quay.io/pypa/manylinux2014_x86_64, so the library needs glibc
# 2.17 at most. The result goes to $OUT: vosk-<version>-linux-x86_64.zip (vosk/libvosk.so, the header,
# the licences, BUILD-INFO.txt), the same layout as the wheel's vosk/ folder, and SHA256SUMS.
#
#   docker run --rm -v "$PWD:/io" -e OUT=/io/out quay.io/pypa/manylinux2014_x86_64 /io/tools/vosk/build-libvosk.sh
set -euo pipefail

# The pins. Vosk's build clones Kaldi's "vosk" branch, OpenFST's master and CLAPACK's v3.2.1 branch at
# their tips; these are their commits on the day v0.3.50 was tagged (2024-04-22).
VOSK_VERSION=${VOSK_VERSION:-0.3.50}
VOSK_COMMIT=${VOSK_COMMIT:-a7bf6a51e299152a8fb496b928a21eb79a1d7bea}       # alphacep/vosk-api v0.3.50
KALDI_COMMIT=${KALDI_COMMIT:-2b69aed630e26fb2c700bba8c45f3bd012371c5c}     # alphacep/kaldi, branch vosk
OPENFST_COMMIT=${OPENFST_COMMIT:-7dfd808194105162f20084bb4d8e4ee4b65266d5} # alphacep/openfst, master
OPENBLAS_COMMIT=${OPENBLAS_COMMIT:-0b678b19dc03f2a999d6e038814c4c50b9640a4e} # OpenMathLib/OpenBLAS v0.3.20
CLAPACK_COMMIT=${CLAPACK_COMMIT:-67d9d1aa7261a1a8b457af3f38ea974946935b02} # alphacep/clapack, branch v3.2.1
OUT=${OUT:-/io/out}
JOBS=${JOBS:-$(nproc)}
# Every file in the archive gets this time, so the archive depends only on what is in it.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1713789597} # 2024-04-22T12:39:57Z, the v0.3.50 commit.
export SOURCE_DATE_EPOCH

fetch() { # fetch <repository URL> <commit> <directory>
    git init -q "$3"
    git -C "$3" fetch -q --depth 1 "$1" "$2"
    git -C "$3" checkout -q FETCH_HEAD
    test "$(git -C "$3" rev-parse HEAD)" = "$2"
}

yum -y -q install automake autoconf libtool zip >/dev/null
PYTHON=/opt/python/cp311-cp311/bin/python3
"$PYTHON" -m pip install -q cmake==3.31.6
PATH="$(dirname "$PYTHON"):$PATH"
export PATH

mkdir -p /opt/build && cd /opt/build
fetch https://github.com/alphacep/kaldi "$KALDI_COMMIT" kaldi
fetch https://github.com/OpenMathLib/OpenBLAS "$OPENBLAS_COMMIT" kaldi/tools/OpenBLAS
fetch https://github.com/alphacep/clapack "$CLAPACK_COMMIT" kaldi/tools/clapack
fetch https://github.com/alphacep/openfst "$OPENFST_COMMIT" kaldi/tools/openfst
fetch https://github.com/alphacep/vosk-api "$VOSK_COMMIT" vosk-api

# OpenBLAS (CBLAS only) and CLAPACK, as upstream builds them.
tools=/opt/build/kaldi/tools
make -s -C "$tools/OpenBLAS" ONLY_CBLAS=1 DYNAMIC_ARCH=1 TARGET=NEHALEM USE_LOCKING=1 USE_THREAD=0 -j "$JOBS" all
make -s -C "$tools/OpenBLAS" PREFIX="$tools/OpenBLAS/install" install
mkdir -p "$tools/clapack/BUILD"
# Upstream built it with GCC 8; GCC 10 no longer merges the tentative definitions CLAPACK's tests share.
(cd "$tools/clapack/BUILD" && cmake -DCMAKE_C_FLAGS=-fcommon .. >/dev/null && make -s -j "$JOBS")
find "$tools/clapack/BUILD" -name '*.a' -exec cp -t "$tools/OpenBLAS/install/lib" {} +

# OpenFST, static and with the n-gram and look-ahead extensions, installed into its own source folder as
# upstream does: Kaldi's configure reads the version from its Makefile.
(cd "$tools/openfst" && autoreconf -i >/dev/null 2>&1 &&
    CFLAGS="-g -O3" ./configure -q --prefix="$tools/openfst" --enable-static --enable-shared --enable-far \
        --enable-ngram-fsts --enable-lookahead-fsts --with-pic --disable-bin && make -s -j "$JOBS" && make -s install)

# Kaldi: the online decoder and the RNN language model, nothing else.
cd /opt/build/kaldi/src
./configure --mathlib=OPENBLAS_CLAPACK --shared --use-cuda=no >/dev/null
sed -i 's: -O1 : -O3 :g' kaldi.mk
make -s -j "$JOBS" online2 rnnlm >/dev/null

# libvosk.so, statically linked with all of the above.
cd /opt/build/vosk-api/src
KALDI_ROOT=/opt/build/kaldi OPENFST_ROOT="$tools/openfst" OPENBLAS_ROOT="$tools/OpenBLAS/install" make -s -j "$JOBS"

# The archive: the wheel's layout, the licences of everything inside the library, and how it was made.
stage=/opt/build/stage/vosk
mkdir -p "$stage/licenses"
cp libvosk.so vosk_api.h "$stage/"
cp /opt/build/vosk-api/COPYING "$stage/licenses/vosk-COPYING"
cp /opt/build/kaldi/COPYING "$stage/licenses/kaldi-COPYING"
cp "$tools/openfst/COPYING" "$stage/licenses/openfst-COPYING"
cp "$tools/OpenBLAS/LICENSE" "$stage/licenses/openblas-LICENSE"
cp "$tools/clapack/COPYING" "$stage/licenses/clapack-COPYING" 2>/dev/null ||
    cp "$tools/clapack/LICENSE" "$stage/licenses/clapack-LICENSE"
{
    echo "Vosk $VOSK_VERSION for Linux x86_64, built from source by Kalipso's vosk-runtime workflow."
    echo "vosk-api  $VOSK_COMMIT (tag v$VOSK_VERSION)"
    echo "kaldi     $KALDI_COMMIT (alphacep/kaldi, branch vosk)"
    echo "openfst   $OPENFST_COMMIT (alphacep/openfst)"
    echo "OpenBLAS  $OPENBLAS_COMMIT (v0.3.20, ONLY_CBLAS=1 DYNAMIC_ARCH=1 TARGET=NEHALEM USE_THREAD=0)"
    echo "clapack   $CLAPACK_COMMIT (alphacep/clapack, branch v3.2.1)"
    echo "image     ${IMAGE:-quay.io/pypa/manylinux2014_x86_64}"
    echo "compiler  $(g++ --version | head -1)"
    echo "glibc     $(ldd --version | head -1)"
} > "$stage/BUILD-INFO.txt"
find "$stage" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
mkdir -p "$OUT"
archive="$OUT/vosk-$VOSK_VERSION-linux-x86_64.zip"
rm -f "$archive"
# Sorted names, no extra attributes: the same files give the same archive.
(cd /opt/build/stage && find vosk -type f | LC_ALL=C sort | zip -q -X -D "$archive" -@)
cp "$stage/libvosk.so" "$OUT/libvosk.so"
cp "$stage/BUILD-INFO.txt" "$OUT/BUILD-INFO.txt"
(cd "$OUT" && sha256sum "$(basename "$archive")" libvosk.so > SHA256SUMS && cat SHA256SUMS)
