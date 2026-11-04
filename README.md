# Mosquitto MQTT TUI (Multi-Language Implementation)

A modern, high-fidelity Terminal User Interface (TUI) front end for Mosquitto MQTT broker management, topic subscription, payload publishing, and configuration editing.

This repository contains implementations in **three languages**, all adhering strictly to the user interface specifications, responsive layouts, CSV history persistence, and JSON formatting:

| Language | Directory | Framework / Stack | Binary / Script |
| :--- | :--- | :--- | :--- |
| **Python** | [`python/`](python/) | `prompt_toolkit` + Python Standard Library | `python/mqtt_tui.py` |
| **Golang** | [`golang/`](golang/) | Charm `bubbletea`, `lipgloss`, `bubbles` | `golang/mqtt-tui` |
| **C++20** | [`cpp/`](cpp/) | `FTXUI` v5.0, `nlohmann/json`, `libmosquitto` | `cpp/build/mqtt-tui` |

---

## Features

- **4 Interactive Tabs**:
  - **Connect**: Broker host address and port controls, live ping socket diagnostics, and mosquitto daemon lifecycle management.
  - **Subscribe**: Multi-topic subscriber with comma-separated topic hack (`#`, `topic1, topic2/sub`), background streaming, and live timestamped event logging.
  - **Publish**: Topic and payload input fields with one-click dispatch via `mosquitto_pub` or native socket API.
  - **🎛️ Config**: Direct viewer and editor for Mosquitto configuration (`~/.config/mosquitto/mosquitto.tui.conf`) with save and reload actions.
- **Pixel-Accurate Mockup Styling**:
  - Vertical alignment across leading emojis: `🦟`, `🔗`, `🏗️`, `💬`, `📝`, `🎛️`.
  - Color accents matching the design:
    - `Server`: Green (`#2ed573`)
    - `Subscribe`: Sky Blue (`#54a0ff`)
    - `Publish`: Magenta (`#e056fd`)
    - `Config`: Amber (`#ffa502`)
  - Neutral badge button design for secondary actions (`bg:#808080 #1c1c1c bold`), with bracket artifacts removed.
- **CSV History Persistence**:
  - All input fields record entries to `~/.config/mqtttui/history.csv` in `timestamp,field,value` format.
  - `Up` and `Down` arrow keys cycle through previous input history on focused input fields.
- **💋 Beautify JSON**:
  - Formats raw single-line JSON messages (e.g., `[11:34:50] {"timestamp": 1793124973.167886, "numeric": 15, "value": 15}`) into indented multi-line JSON structures.
- **Clean Scrollbars**:
  - Custom scrollbar margins without intrusive `^` and `v` tip characters.

---

## Quick Start & Building

### One-Click Build Script

To compile both Go and C++ binaries simultaneously:

```bash
./build_all.sh
```

---

## Running in Kitty

You can run any of the three versions directly in an isolated Kitty OS window:

### 1. Python (`prompt_toolkit`)

```bash
kitty @ launch --type=os-window --copy-env --title 'MQTT_TUI_PY' \
  /usr/bin/arch -arm64 /opt/homebrew/bin/zsh -ic '$HOME/Source/CLANG/mqtt-tui-cpp/python/mqtt_tui.py'
```

### 2. Golang (`bubbletea`)

```bash
kitty @ launch --type=os-window --copy-env --title 'MQTT_TUI_GO' \
  /usr/bin/arch -arm64 /opt/homebrew/bin/zsh -ic '$HOME/Source/CLANG/mqtt-tui-cpp/golang/mqtt-tui'
```

### 3. C++20 (`FTXUI`)

```bash
kitty @ launch --type=os-window --copy-env --title 'MQTT_TUI_CPP' \
  /usr/bin/arch -arm64 /opt/homebrew/bin/zsh -ic '$HOME/Source/CLANG/mqtt-tui-cpp/cpp/build/mqtt-tui'
```

---

## Global Keybindings

| Key | Action |
| :--- | :--- |
| `F1` or `1` | Switch to **Connect** Tab |
| `F2` or `2` | Switch to **Subscribe** Tab |
| `F3` or `3` | Switch to **Publish** Tab |
| `F4` or `4` | Switch to **Config (🎛️)** Tab |
| `Tab` / `Shift+Tab` | Navigate between input fields and action buttons |
| `Up` / `Down` | Browse input history from `~/.config/mqtttui/history.csv` |
| `Enter` / `F5` | Execute focused button or trigger default action |
| `Ctrl+L` | Clear Logs area |
| `Ctrl+Q` | Quit TUI |
