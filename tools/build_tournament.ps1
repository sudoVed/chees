# Build the tournament tool on Windows (no bash/WSL needed).
# Uses the zig compiler shipped by the `ziglang` pip package you already have.
$ErrorActionPreference = "Stop"
Set-Location "$PSScriptRoot\.."
$zig = (python -c "import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), 'zig.exe'))")
& $zig c++ -std=c++17 -O2 -fno-exceptions -fno-rtti `
  tools/tournament.cpp `
  engine/board/zobrist.cpp engine/board/position.cpp engine/movegen/attacks.cpp `
  engine/movegen/movegen.cpp engine/rules/gamestate.cpp engine/eval/hce.cpp `
  engine/search/tt.cpp engine/search/search.cpp engine/nn/nnue.cpp `
  -o tools/tournament.exe
Write-Host "built tools/tournament.exe"
Write-Host "run e.g.:  .\tools\tournament.exe --a nnue:10 --b hce:10 --games 50 --net dist\net.nnue"
