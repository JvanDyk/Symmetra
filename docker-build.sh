#!/bin/bash
set -e
cd /src/engine
rm -rf build-linux && mkdir build-linux
cmake -B build-linux -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_ARCHITECTURES=75 \
  2>&1 | grep -E "^--|error:|Error:|warning:" | head -20
cmake --build build-linux --parallel $(nproc) 2>&1 | tail -10
ls -lh build-linux/sym