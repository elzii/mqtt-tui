# AGENTS.md

## Project Overview

`mqtt-tui` is a high-performance, cross-platform Terminal User Interface (TUI) for managing Eclipse Mosquitto MQTT broker instances, streaming subscribed topic data, publishing MQTT messages, and interactively editing broker configuration files.

Originally prototyped in Python, the project was systematically analyzed and ported to modern **C++20 (FTXUI)** and **Go (Bubble Tea)**.

---

## Architecture & Implementations

```
mqtt-tui-cpp/
├── cpp/                  # C++20 implementation (FTXUI) - Primary production version
│   ├── CMakeLists.txt    # Build definition with custom FTXUI upstream patches
│   └── src/
│       ├── main.cpp      # Main application loop, tabs, event routing, and UI rendering
│       ├── history.hpp   # CSV history storage interface (~/.config/mqtttui/history.csv)
│       ├── history.cpp   # Implementation of input history serialization
│       ├── beautify.hpp  # JSON beautification helper interface
│       └── beautify.cpp  # JSON extraction and indentation implementation
├── python/               # Python implementation (prompt_toolkit) - Reference prototype
│   ├── mqtt_tui.py       # Full-featured single-file TUI application
│   └── README.md
├── golang/               # Go implementation (Bubble Tea / Lip Gloss)
│   ├── main.go           # Bubble Tea model, update loops, and view rendering
│   ├── history.go        # CSV history management
│   ├── beautify.go       # Log JSON prettifier
│   └── go.mod
├── build_all.sh          # One-step compilation script for both Go and C++ targets
└── AGENTS.md             # Architecture, design decisions, and agent reference
```

---

## Core Capabilities & Features

### 1. Tabbed Interface
- **🔗 Server / Connect Tab (F1 / 1)**:
  - Input fields for `Host Address` (default `127.0.0.1`) and `Port` (default `1883|9001`).
  - Action buttons: `▶ Start Mosquitto`, `⏹ Stop Mosquitto`, `⚡ Check/Ping`, and `🧹 Clear`.
  - Non-blocking socket ping utility to verify port availability and broker status.
- **💬 Subscribe Tab (F2 / 2)**:
  - Comma-separated topic subscription input (`Topics`, default `#`) and `Port`.
  - Action buttons: `▶ Start Sub` / `● Subscribing`, `⏹ Stop Sub`, `💋 Beautify JSON`, and `🧹 Clear`.
  - Asynchronous background process execution streaming live messages with timestamps.
  - State persistence: retains active subscription badge across tab switches.
- **🚀 Publish Tab (F3 / 3)**:
  - Input fields for target `Topic` (default `test/topic`) and multiline `Message`.
  - Action buttons: `🚀 Publish Now`, `💋 Beautify JSON`, and `🧹 Clear`.
- **🎛️ Config Tab (F4 / 4)**:
  - Fullscreen multiline editor bound to `~/.config/mosquitto/mosquitto.tui.conf`.
  - Integrated native vertical scrollbar (`vscroll_indicator`), mouse wheel, and <kbd>PageUp</kbd>/<kbd>PageDown</kbd> paging.
  - Action buttons: `💾 Save Config`, `🔄 Reload`, and `🧹 Clear`.
  - Automatically minimizes the Logs panel to a compact 5-row drawer at the bottom.

### 2. Input History Tracking
- All input fields record submitted values into `~/.config/mqtttui/history.csv`.
- <kbd>Up</kbd> and <kbd>Down</kbd> arrow keys navigate through previous entries per field.

### 3. Log JSON Beautification (`💋 Beautify JSON`)
- Scans existing log lines matching `[HH:MM:SS] { ... }` or raw JSON payloads.
- Formats single-line JSON payloads into multi-line indented blocks:
  ```
  [11:34:50] {"timestamp": 1793124973.167886, "numeric": 15, "value": 15}
  ```
  becomes:
  ```
  [11:34:50]
  {
    "numeric": 15,
    "timestamp": 1793124973.167886,
    "value": 15
  }
  ```

### 4. Dynamic Border Styling & Cursor Visibility
- **Border States**:
  - **Normal (Inactive)**: 40% white (`#666666`).
  - **Hover**: 60% white (`#999999`).
  - **Active (Focused)**: 100% white (`#ffffff`).
- **Cursor**:
  - Renders a high-contrast inverted block cursor `█` (`inverted | focusCursorBlock`) directly in cell memory.
  - Displays a visible cursor block at the start of empty placeholder inputs upon focus.

### 5. Unicode & Terminal Alignment Fixes
- **Variation Selectors (`\uFE0F`)**:
  - In modern terminals (Kitty, Alacritty, WezTerm), variation selectors attached to emojis (`🏗️`, `🎛️`) caused a 1-column cell calculation mismatch in default FTXUI and Python `wcwidth`.
  - Patched FTXUI's `string.cpp` to attach `0xFE00..0xFE0F` combining marks to the preceding glyph.
  - Patched `python/mqtt_tui.py` with custom `wcwidth` handlers before UI layout initialization.

---

## Build and Run Instructions

### C++ Target (FTXUI)
```bash
cmake -B cpp/build -S cpp
cmake --build cpp/build -j4
./cpp/build/mqtt-tui
```

### Go Target (Bubble Tea)
```bash
cd golang
go build -o mqtt-tui .
./mqtt-tui
```

### Python Target (prompt_toolkit)
```bash
python3 python/mqtt_tui.py
```

### Unified Build
```bash
./build_all.sh
```

---

## Maintenance Guidelines for Future Agents
1. **FTXUI Upstream Patches**:
   - Any modifications to FTXUI behavior (cursor block, multiline scrollbar, unicode variation selectors, background styling) should be registered in [`cpp/CMakeLists.txt`](file:///Users/azizzo/Source/CLANG/mqtt-tui-cpp/cpp/CMakeLists.txt) using CMake string replacement hooks on `FetchContent` source files to maintain reproducible builds.
2. **Tab Navigation Focus Flow**:
   - Focus traversal is centralized in `get_focus_chain()` in [`cpp/src/main.cpp`](file:///Users/azizzo/Source/CLANG/mqtt-tui-cpp/cpp/src/main.cpp). New buttons or inputs must be added to this focus list to participate in <kbd>Tab</kbd> / <kbd>Shift+Tab</kbd> cycling.
3. **Log Auto-Scroll Contract**:
   - Logs lock scroll position when manually navigated using the mouse wheel or <kbd>PageUp</kbd>/<kbd>PageDown</kbd>. Setting `log_scroll_line = -1` or scrolling to the bottom re-enables automatic scrolling.
