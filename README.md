# Mosquitto MQTT TUI

An interactive Terminal User Interface front end for **Eclipse Mosquitto** built with Python and `prompt_toolkit`.

The interface replicates the multi-tab layout for Mosquitto tools, featuring adaptive responsive sizing, real-time log streaming, and direct integration with `mosquitto`, `mosquitto_sub`, and `mosquitto_pub`.

---

## Architecture & Tabs

```
  🦟  MQTT : Server | Subscribe | Publish    [ Connect ]  [ Subscribe ]  [ Publish ]  [ 🎛️ ]
```

### 1. `[ Connect ]` (Server / `mosquitto`)
- **Host Address**: Defaults to `127.0.0.1`.
- **Port**: Defaults to `1883` (standard TCP) or `9001` (WebSockets).
- **Broker Management**:
  - `[ ▶ Start Mosquitto ]` / `[ ⏹ Stop Broker ]`: Launches or stops `mosquitto -c ~/.config/mosquitto/mosquitto.tui.conf -v` in the background with real-time log streaming.
  - `[ ⚡ Check/Ping ]`: Tests TCP reachability and inspects broker information via `$SYS/broker/version`.
- **Logs Panel**: Displays streaming server logs, connection attempts, and PID status.

### 2. `[ Subscribe ]` (`mosquitto_sub`)
- **Topics**: Comma-separated list of topics (e.g. `sensors/temp, sensors/pressure, alerts/#`). Defaults to `#` (wildcard).
  - *Multi-topic experimental hack*: Automatically splits comma-separated entries into multiple `-t <topic>` flags for `mosquitto_sub`.
- **Port**: Configurable port (defaults to `1883` or `9001` with `--ws`).
- **Subscription Controls**:
  - `[ ▶ Start Subscribe ]` / `[ ⏹ Stop Subscribe ]`: Spawns `mosquitto_sub -v` as an asynchronous subprocess.
- **Logs Panel**: Live message stream with topic and payload output.

### 3. `[ Publish ]` (`mosquitto_pub`)
- **Topic**: Destination topic(s). Supports comma-separated topics for broadcasting the payload to multiple topics in sequence.
- **Message**: Multiline payload text area. Press `Enter` for newlines; press `Enter` on the single-line input or `F5` / click `[ 🚀 Publish ]` to dispatch.
- **Publish Execution**: Invokes `mosquitto_pub` with proper flags and WebSocket support (`--ws` when port `9001`).
- **Logs Panel**: Confirms published messages, byte counts, and delivery exit codes.

### 4. `[ 🎛️ ]` (Config / `~/.config/mosquitto/mosquitto.tui.conf`)
- Dedicated configuration manager for `~/.config/mosquitto/mosquitto.tui.conf`.
- **Integrated Editor**: Scrollable text editor with line numbers.
- **Toolbar Actions**:
  - `[ 💾 Save Config (Ctrl+S) ]`: Writes edits back to `~/.config/mosquitto/mosquitto.tui.conf`.
  - `[ 🧪 Test Config (Ctrl+T) ]`: Executes `mosquitto --test-config -c ~/.config/mosquitto/mosquitto.tui.conf` to validate syntax before running.
  - `[ 🔄 Reload (Ctrl+R) ]`: Reloads configuration from disk.
- **Validation Logs**: Displays syntax check results and configuration diagnostics.

---

## Controls & Keybindings

| Key / Action | Function |
|---|---|
| **Mouse Click** | Click any tab button, action button, or text box to focus/activate |
| **`F1` / `Alt+1`** | Switch to **Connect** (Server) tab |
| **`F2` / `Alt+2`** | Switch to **Subscribe** tab |
| **`F3` / `Alt+3`** | Switch to **Publish** tab |
| **`F4` / `Alt+4`** | Switch to **Config** (`🎛️`) tab |
| **`Tab` / `Shift+Tab`** | Navigate between input fields and controls |
| **`Enter` / `F5`** | Trigger active action (Connect / Subscribe / Publish / Test) |
| **`Ctrl+S`** | Save configuration (Config tab) |
| **`Ctrl+T`** | Test configuration syntax (Config tab) |
| **`Ctrl+R`** | Reload configuration from disk (Config tab) |
| **`Ctrl+L`** | Clear active tab logs |
| **`Ctrl+Q` / `Ctrl+C`** | Clean exit (terminates all background processes) |

---

## Running the TUI

```bash
# Direct execution:
python3 mqtt_tui.py

# Or make executable:
chmod +x mqtt_tui.py
./mqtt_tui.py
```

### Requirements
- Python 3.8+
- `prompt_toolkit` (`pip install prompt_toolkit`)
- `mosquitto`, `mosquitto_sub`, `mosquitto_pub` installed and available in `$PATH` (e.g. `brew install mosquitto`).
