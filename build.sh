#!/usr/bin/env bash
# Build the C++ engine to wasm32 using the Zig-bundled clang/lld toolchain.
# Output: dist/engine.wasm  (raw module, loaded by frontend/js/engine-bridge.js)
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# Resolve the zig binary (installed via the `ziglang` pip wheel).
ZIG="${ZIG:-$(python3 -c 'import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),"zig"))')}"

SRC=(
  engine/board/zobrist.cpp
  engine/board/position.cpp
  engine/movegen/attacks.cpp
  engine/movegen/movegen.cpp
  engine/rules/gamestate.cpp
  engine/eval/hce.cpp
  engine/search/tt.cpp
  engine/search/search.cpp
  engine/nn/nnue.cpp
  engine/api/wasm_api.cpp
)

EXPORTS=(
  engine_init new_game get_fen_buf load_fen get_board side_to_move
  get_castling get_ep halfmove_clock fullmove_number
  last_move_from last_move_to get_targets_buf get_flags_buf
  get_legal_moves api_is_capture api_is_promotion api_make_move undo get_status
  in_check perft_current
  search_best_move search_score search_nodes search_depth
  get_nnue_buf nnue_blob_capacity load_nnue nnue_active
  eval_nnue_white
  set_search_eval
)
EXPORT_FLAGS=""
for e in "${EXPORTS[@]}"; do EXPORT_FLAGS="$EXPORT_FLAGS -Wl,--export=$e"; done

mkdir -p dist
"$ZIG" c++ -target wasm32-freestanding -std=c++17 -O3 \
  -fno-exceptions -fno-rtti -ffast-math -msimd128 \
  -nostdlib -Wl,--no-entry -Wl,--export-memory \
  $EXPORT_FLAGS \
  -o dist/engine.wasm "${SRC[@]}"

echo "Built dist/engine.wasm ($(wc -c < dist/engine.wasm) bytes)"

# Embed the wasm as base64 so the page runs even from file:// (no fetch/CORS).
node scripts/embed-wasm.js
