$ErrorActionPreference = "Stop"
Set-Location "$PSScriptRoot\.."

$zig = (python -c "import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), 'zig.exe'))")
$node = "C:\Users\theso\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe"

$sources = @(
  "engine/board/zobrist.cpp",
  "engine/board/position.cpp",
  "engine/movegen/attacks.cpp",
  "engine/movegen/movegen.cpp",
  "engine/rules/gamestate.cpp",
  "engine/eval/hce.cpp",
  "engine/search/tt.cpp",
  "engine/search/search.cpp",
  "engine/nn/nnue.cpp",
  "engine/api/wasm_api.cpp"
)

$exports = @(
  "engine_init", "new_game", "get_fen_buf", "load_fen", "get_board", "side_to_move",
  "get_castling", "get_ep", "halfmove_clock", "fullmove_number",
  "last_move_from", "last_move_to", "get_targets_buf", "get_flags_buf",
  "get_legal_moves", "api_is_capture", "api_is_promotion", "api_make_move",
  "undo", "get_status", "in_check", "perft_current",
  "search_best_move", "search_score", "search_nodes", "search_depth",
  "get_nnue_buf", "nnue_blob_capacity", "load_nnue", "nnue_active",
  "eval_nnue_white", "set_search_eval"
)

$exportFlags = @("-Wl,--no-entry", "-Wl,--export-memory")
foreach ($name in $exports) {
  $exportFlags += "-Wl,--export=$name"
}

New-Item -ItemType Directory -Force -Path dist | Out-Null
& $zig c++ -target wasm32-freestanding -std=c++17 -O3 `
  -fno-exceptions -fno-rtti -ffast-math -msimd128 -nostdlib `
  @exportFlags `
  -o dist/engine.wasm `
  @sources

Write-Host "Built dist/engine.wasm ($((Get-Item dist/engine.wasm).Length) bytes)"
& $node scripts/embed-wasm.js
