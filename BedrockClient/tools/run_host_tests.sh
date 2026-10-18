#!/usr/bin/env bash
# Chạy trên máy Linux thường (không cần NDK): kiểm tra cú pháp toàn bộ native + test logic lõi.
# Header Android/ImGui ở đây chỉ là stub, nên đây KHÔNG thay thế build thật bằng NDK.
set -euo pipefail
cd "$(dirname "$0")/.."
CPP=app/src/main/cpp; STUBS=tools/host_stubs
for f in client_hooks game_internals launcher_native module_manager client_ui; do
  g++ -std=c++17 -fsyntax-only -Wall -Wextra -I$STUBS -I$CPP $CPP/$f.cpp
  echo "syntax OK: $f.cpp"
done
g++ -std=c++17 -g -fsanitize=address,undefined -I$STUBS -I$CPP tests/test_native.cpp $CPP/module_manager.cpp -o /tmp/test_native -pthread -ldl
/tmp/test_native
