$ErrorActionPreference = "Stop"; Set-Location "$PSScriptRoot\.."
$zig = (python -c "import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), 'zig.exe'))")
& $zig c++ -std=c++17 -O2 -fno-exceptions -fno-rtti tools/checknet.cpp `
  engine/board/zobrist.cpp engine/board/position.cpp engine/movegen/attacks.cpp engine/nn/nnue.cpp `
  -o tools/checknet.exe
Write-Host "built tools/checknet.exe   ->   .\tools\checknet.exe frontend\models\net.nnue"
