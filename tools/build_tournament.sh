#!/usr/bin/env bash
# Build the native tournament tool (uses g++; not wasm).
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -O2 -fno-exceptions -fno-rtti \
  tools/tournament.cpp \
  engine/board/zobrist.cpp engine/board/position.cpp engine/movegen/attacks.cpp \
  engine/movegen/movegen.cpp engine/rules/gamestate.cpp engine/eval/hce.cpp \
  engine/search/tt.cpp engine/search/search.cpp engine/nn/nnue.cpp \
  -o tools/tournament
echo "built tools/tournament"
