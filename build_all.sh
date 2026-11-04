#!/usr/bin/env bash
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "=========================================================="
echo " Building Mosquitto TUI across implementations"
echo " Workspace: ${REPO_ROOT}"
echo "=========================================================="

# 1. Golang Build
echo ""
echo "==> [1/2] Building Go implementation (Bubble Tea)..."
cd "${REPO_ROOT}/golang"
go build -o mqtt-tui .
echo "    ✓ Go binary built at: ${REPO_ROOT}/golang/mqtt-tui"

# 2. C++20 Build
echo ""
echo "==> [2/2] Building C++20 implementation (FTXUI)..."
cd "${REPO_ROOT}/cpp"
cmake -B build -S .
cmake --build build -j4
echo "    ✓ C++ binary built at: ${REPO_ROOT}/cpp/build/mqtt-tui"

# 3. Python status
echo ""
echo "==> Python script ready at: ${REPO_ROOT}/python/mqtt_tui.py"

echo ""
echo "=========================================================="
echo " All builds succeeded!"
echo " Launch with Kitty:"
echo "   Python: kitty @ launch --type=os-window --copy-env --title 'MQTT_TUI_PY'  /usr/bin/arch -arm64 /opt/homebrew/bin/zsh -ic '\$HOME/Source/CLANG/mqtt-tui-cpp/python/mqtt_tui.py'"
echo "   Go:     kitty @ launch --type=os-window --copy-env --title 'MQTT_TUI_GO'  /usr/bin/arch -arm64 /opt/homebrew/bin/zsh -ic '\$HOME/Source/CLANG/mqtt-tui-cpp/golang/mqtt-tui'"
echo "   C++:    kitty @ launch --type=os-window --copy-env --title 'MQTT_TUI_CPP' /usr/bin/arch -arm64 /opt/homebrew/bin/zsh -ic '\$HOME/Source/CLANG/mqtt-tui-cpp/cpp/build/mqtt-tui'"
echo "=========================================================="
