#!/usr/bin/env python3
"""
MQTT TUI Front End for Mosquitto
Provides an interactive Terminal User Interface for:
  - Connect (mosquitto broker management & server logs)
  - Subscribe (mosquitto_sub with comma-separated topic filtering)
  - Publish (mosquitto_pub message dispatcher)
  - Config (mosquitto.tui.conf editor & syntax validator)
"""

import os
import sys
import asyncio
import datetime
import shutil
from typing import List, Optional

from prompt_toolkit.application import Application
from prompt_toolkit.key_binding import KeyBindings
from prompt_toolkit.key_binding.bindings.focus import focus_next, focus_previous
from prompt_toolkit.layout.containers import (
    HSplit,
    VSplit,
    Window,
    DynamicContainer,
)
from prompt_toolkit.layout.controls import FormattedTextControl
from prompt_toolkit.layout.dimension import Dimension
from prompt_toolkit.layout.layout import Layout
from prompt_toolkit.layout.processors import Processor, Transformation
from prompt_toolkit.formatted_text import to_formatted_text
from prompt_toolkit.document import Document
from prompt_toolkit.widgets import TextArea, Frame
from prompt_toolkit.styles import Style
from prompt_toolkit.mouse_events import MouseEventType


# --- Config & Defaults ---
CONFIG_DIR = os.path.expanduser("~/.config/mosquitto")
CONFIG_PATH = os.path.join(CONFIG_DIR, "mosquitto.tui.conf")

DEFAULT_CONF_CONTENT = """# =================================================================
# mosquitto.tui.conf - Mosquitto TUI configuration file
# =================================================================

listener 1883
listener 9001
protocol websockets
allow_anonymous true

# Persistence
persistence false

# Logging
log_type error
log_type warning
log_type notice
log_type information
"""


def ensure_config_exists() -> str:
    """Ensure the config directory and config file exist, returning the path."""
    os.makedirs(CONFIG_DIR, exist_ok=True)
    if not os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            f.write(DEFAULT_CONF_CONTENT)
    return CONFIG_PATH


# --- Styled Placeholder Processor ---
class StyledPlaceholder(Processor):
    """Displays a styled placeholder when the buffer is empty."""
    def __init__(self, fragments):
        self.fragments = fragments

    def apply_transformation(self, transformation_input):
        buf = transformation_input.buffer_control.buffer
        if not buf.text and transformation_input.lineno == 0:
            return Transformation(to_formatted_text(self.fragments))
        return Transformation(transformation_input.fragments)


# --- Theme Styles ---
THEME_STYLE = Style.from_dict({
    # Base layout
    "root": "bg:#0d0e12 #d8dee9",
    "topbar": "bg:#15181f #e5e9f0",
    "topbar.logo": "#ff9f43 bold",
    "topbar.label": "#ff9f43 bold",
    "topbar.server": "#2ed573 bold",
    "topbar.subscribe": "#54a0ff bold",
    "topbar.publish": "#e056fd bold",
    "topbar.pipe": "#636e72",

    # Tab buttons (inactive / active)
    "tab.inactive": "bg:#232730 #a4b0be",
    "tab.active.connect": "bg:#5af78e #0c1c14 bold",
    "tab.active.subscribe": "bg:#54a0ff #0a192f bold",
    "tab.active.publish": "bg:#e056fd #25092a bold",
    "tab.active.config": "bg:#ffa502 #241400 bold",

    # Icon labels
    "icon": "#e5e9f0 bold",
    "icon.muted": "#8892b0",

    # Input boxes
    "input-frame": "#3b4252",
    "input-frame.focused": "#54a0ff bold",
    "input-text": "#f8f9fa",
    "placeholder.title": "#c8d6e5",
    "placeholder.default": "#636e72 italic",

    # Action buttons (ANSI 244 bg: #808080, ANSI 234 fg: #1c1c1c)
    "btn.badge": "bg:#808080 #1c1c1c bold",
    "btn": "bg:#808080 #1c1c1c bold",

    # Logs header and area
    "logs.header": "bg:#383e4a #f1f2f6 bold",
    "logs.status.idle": "#a4b0be",
    "logs.status.running": "#2ed573 bold",
    "logs.status.stopped": "#ff4757 bold",
    "logs.body": "bg:#0a0c0f #dcdde1",

    # Bottom bar
    "bottom.bar": "bg:#14171d #8892b0",
    "bottom.key": "#54a0ff bold",
    "bottom.desc": "#a4b0be",
})


class MqttTuiApp:
    def __init__(self):
        self.conf_path = ensure_config_exists()
        self.current_tab = "connect"  # "connect" | "subscribe" | "publish" | "config"

        # Background processes & monitoring tasks
        self.server_proc: Optional[asyncio.subprocess.Process] = None
        self.sub_proc: Optional[asyncio.subprocess.Process] = None
        self.server_task: Optional[asyncio.Task] = None
        self.sub_task: Optional[asyncio.Task] = None

        # Build UI Components
        self._init_inputs()
        self._init_logs()
        self._init_views()
        self._init_keybindings()

        # Build prompt_toolkit Application
        self.app = Application(
            layout=self.layout,
            key_bindings=self.kb,
            style=THEME_STYLE,
            full_screen=True,
            mouse_support=True,
            enable_page_navigation_bindings=True,
        )

    def _init_inputs(self):
        # 1. Connect Tab Inputs
        host_placeholder = StyledPlaceholder([
            ("class:placeholder.title", "Host Address "),
            ("class:placeholder.default", "(DEFAULT = 127.0.0.1)")
        ])
        self.server_host_input = TextArea(
            multiline=False,
            height=1,
            input_processors=[host_placeholder],
            style="class:input-text",
        )

        port_placeholder = StyledPlaceholder([
            ("class:placeholder.title", "Port "),
            ("class:placeholder.default", "(DEFAULT = 1883|9001)")
        ])
        self.server_port_input = TextArea(
            multiline=False,
            height=1,
            input_processors=[port_placeholder],
            style="class:input-text",
        )

        # 2. Subscribe Tab Inputs
        sub_topics_placeholder = StyledPlaceholder([
            ("class:placeholder.title", "Topics "),
            ("class:placeholder.default", "(Comma Separated, DEFAULT = #)")
        ])
        self.sub_topics_input = TextArea(
            multiline=False,
            height=1,
            input_processors=[sub_topics_placeholder],
            style="class:input-text",
        )

        sub_port_placeholder = StyledPlaceholder([
            ("class:placeholder.title", "Port "),
            ("class:placeholder.default", "(DEFAULT = 1883|9001)")
        ])
        self.sub_port_input = TextArea(
            multiline=False,
            height=1,
            input_processors=[sub_port_placeholder],
            style="class:input-text",
        )

        # 3. Publish Tab Inputs
        pub_topic_placeholder = StyledPlaceholder([
            ("class:placeholder.title", "Topic "),
            ("class:placeholder.default", "(Comma Separated, DEFAULT = #)")
        ])
        self.pub_topic_input = TextArea(
            multiline=False,
            height=1,
            input_processors=[pub_topic_placeholder],
            style="class:input-text",
        )

        pub_msg_placeholder = StyledPlaceholder([
            ("class:placeholder.title", "Message"),
            ("class:placeholder.default", "")
        ])
        self.pub_msg_input = TextArea(
            multiline=True,
            height=Dimension(min=4, preferred=7, max=12),
            input_processors=[pub_msg_placeholder],
            style="class:input-text",
            wrap_lines=True,
        )

        # 4. Config Tab Input (Editor)
        conf_text = ""
        try:
            with open(self.conf_path, "r", encoding="utf-8") as f:
                conf_text = f.read()
        except Exception as e:
            conf_text = f"# Error reading {self.conf_path}: {e}"

        self.config_editor = TextArea(
            text=conf_text,
            multiline=True,
            line_numbers=True,
            scrollbar=True,
            wrap_lines=False,
            style="class:input-text",
        )

    def _init_logs(self):
        # Dedicated scrollable text area for each tab's logs
        self.server_logs = TextArea(read_only=True, scrollbar=True, style="class:logs.body")
        self.sub_logs = TextArea(read_only=True, scrollbar=True, style="class:logs.body")
        self.pub_logs = TextArea(read_only=True, scrollbar=True, style="class:logs.body")
        self.config_logs = TextArea(
            read_only=True,
            scrollbar=True,
            style="class:logs.body",
            height=Dimension(min=5, preferred=7, max=10)
        )

        # Append welcoming info to server logs
        self.append_log(self.server_logs, "Mosquitto TUI initialized. Ready to connect or start broker.")
        self.append_log(self.sub_logs, "Subscriber ready. Enter topics or leave blank for wildcard '#'.")
        self.append_log(self.pub_logs, "Publisher ready. Enter topic and message, then press Publish (Enter).")
        self.append_log(self.config_logs, f"Loaded configuration file: {self.conf_path}")

    def append_log(self, text_area: TextArea, message: str):
        """Append a timestamped line to the given logs text area and scroll down."""
        now = datetime.datetime.now().strftime("%H:%M:%S")
        line = f"[{now}] {message}\n"
        existing = text_area.buffer.text
        new_text = existing + line
        # Keep at most last 1500 lines to prevent memory bloat
        lines = new_text.splitlines(keepends=True)
        if len(lines) > 1500:
            new_text = "".join(lines[-1200:])
        text_area.buffer.set_document(
            Document(new_text, cursor_position=len(new_text)),
            bypass_readonly=True
        )

    def clear_log(self, text_area: TextArea):
        """Clear a read-only log text area."""
        text_area.buffer.set_document(
            Document("", cursor_position=0),
            bypass_readonly=True
        )

    # --- Click & Navigation Handlers ---
    def switch_tab(self, tab_name: str):
        self.current_tab = tab_name
        if hasattr(self, "app"):
            if tab_name == "connect":
                self.app.layout.focus(self.server_host_input)
            elif tab_name == "subscribe":
                self.app.layout.focus(self.sub_topics_input)
            elif tab_name == "publish":
                self.app.layout.focus(self.pub_topic_input)
            elif tab_name == "config":
                self.app.layout.focus(self.config_editor)
            self.app.invalidate()

    def _make_mouse_click_handler(self, action_fn):
        def handler(mouse_event):
            if mouse_event.event_type == MouseEventType.MOUSE_UP:
                action_fn()
        return handler

    # --- Top Bar Header Layout ---
    def _render_topbar(self):
        def get_topbar_fragments():
            h_conn = self._make_mouse_click_handler(lambda: self.switch_tab("connect"))
            h_sub = self._make_mouse_click_handler(lambda: self.switch_tab("subscribe"))
            h_pub = self._make_mouse_click_handler(lambda: self.switch_tab("publish"))
            h_cfg = self._make_mouse_click_handler(lambda: self.switch_tab("config"))

            conn_cls = "class:tab.active.connect" if self.current_tab == "connect" else "class:tab.inactive"
            sub_cls = "class:tab.active.subscribe" if self.current_tab == "subscribe" else "class:tab.inactive"
            pub_cls = "class:tab.active.publish" if self.current_tab == "publish" else "class:tab.inactive"
            cfg_cls = "class:tab.active.config" if self.current_tab == "config" else "class:tab.inactive"

            return [
                ("class:topbar.logo", "  🦟 "),
                ("class:topbar.label", "MQTT : "),
                ("class:topbar.server", "Server"),
                ("class:topbar.pipe", " | "),
                ("class:topbar.subscribe", "Subscribe"),
                ("class:topbar.pipe", " | "),
                ("class:topbar.publish", "Publish"),
                ("", "    "),
                (conn_cls, "  Connect  ", h_conn),
                ("", "  "),
                (sub_cls, "  Subscribe  ", h_sub),
                ("", "  "),
                (pub_cls, "  Publish  ", h_pub),
                ("", "  "),
                (cfg_cls, "  🎛️  ", h_cfg),
                ("", "  "),
            ]

        return Window(
            content=FormattedTextControl(get_topbar_fragments),
            height=Dimension.exact(1),
            style="class:topbar",
        )

    # --- View 1: Connect / Server ---
    def _build_connect_view(self):
        # Row 1: Host Address with centered 🔗 icon
        row_host = VSplit([
            Window(FormattedTextControl("\n  🔗  \n"), width=Dimension.exact(6), height=Dimension.exact(3), style="class:icon"),
            Frame(self.server_host_input, height=Dimension.exact(3), style="class:input-frame"),
        ])

        # Row 2: Port with centered 🏗️ icon
        row_port = VSplit([
            Window(FormattedTextControl("\n  🏗️  \n"), width=Dimension.exact(6), height=Dimension.exact(3), style="class:icon"),
            Frame(self.server_port_input, height=Dimension.exact(3), style="class:input-frame"),
        ])

        # Logs Header Bar with quick action buttons
        def get_server_log_header():
            h_run = self._make_mouse_click_handler(self.action_toggle_broker)
            h_ping = self._make_mouse_click_handler(self.action_ping_broker)
            h_clear = self._make_mouse_click_handler(lambda: self.clear_log(self.server_logs))

            status_text = "● Broker: Running" if self.server_proc else "● Broker: Idle/External"
            status_cls = "class:logs.status.running" if self.server_proc else "class:logs.status.idle"
            action_btn_text = "⏹ Stop Broker" if self.server_proc else "▶ Start Mosquitto"

            return [
                ("class:logs.header", "  📝 Logs"),
                ("class:logs.header", " " * 6),
                (status_cls, status_text),
                ("class:logs.header", "   "),
                ("class:btn.badge", f" {action_btn_text} ", h_run),
                ("class:logs.header", "  "),
                ("class:btn.badge", " ⚡ Check/Ping ", h_ping),
                ("class:logs.header", "  "),
                ("class:btn.badge", " 🧹 Clear ", h_clear),
            ]

        logs_header = Window(
            content=FormattedTextControl(get_server_log_header),
            height=Dimension.exact(1),
            style="class:logs.header",
        )

        logs_frame = Frame(self.server_logs, style="class:input-frame")

        return HSplit([
            Window(height=1),
            row_host,
            row_port,
            Window(height=1),
            logs_header,
            logs_frame,
        ])

    # --- View 2: Subscribe ---
    def _build_subscribe_view(self):
        # Row 1: Topics (Comma separated)
        row_topics = VSplit([
            Window(FormattedTextControl("\n  💬  \n"), width=Dimension.exact(6), height=Dimension.exact(3), style="class:icon"),
            Frame(self.sub_topics_input, height=Dimension.exact(3), style="class:input-frame"),
        ])

        # Row 2: Port
        row_port = VSplit([
            Window(FormattedTextControl("\n  🏗️  \n"), width=Dimension.exact(6), height=Dimension.exact(3), style="class:icon"),
            Frame(self.sub_port_input, height=Dimension.exact(3), style="class:input-frame"),
        ])

        # Logs Header Bar
        def get_sub_log_header():
            h_sub = self._make_mouse_click_handler(self.action_toggle_subscribe)
            h_clear = self._make_mouse_click_handler(lambda: self.clear_log(self.sub_logs))

            status_text = "● Subscribing (Active)" if self.sub_proc else "● Inactive"
            status_cls = "class:logs.status.running" if self.sub_proc else "class:logs.status.idle"
            btn_text = "⏹ Stop Subscribe" if self.sub_proc else "▶ Start Subscribe"

            return [
                ("class:logs.header", "  📝 Logs"),
                ("class:logs.header", " " * 6),
                (status_cls, status_text),
                ("class:logs.header", "   "),
                ("class:btn.badge", f" {btn_text} ", h_sub),
                ("class:logs.header", "  "),
                ("class:btn.badge", " 🧹 Clear ", h_clear),
            ]

        logs_header = Window(
            content=FormattedTextControl(get_sub_log_header),
            height=Dimension.exact(1),
            style="class:logs.header",
        )

        logs_frame = Frame(self.sub_logs, style="class:input-frame")

        return HSplit([
            Window(height=1),
            row_topics,
            row_port,
            Window(height=1),
            logs_header,
            logs_frame,
        ])

    # --- View 3: Publish ---
    def _build_publish_view(self):
        # Row 1: Topic
        row_topic = VSplit([
            Window(FormattedTextControl("\n  💬  \n"), width=Dimension.exact(6), height=Dimension.exact(3), style="class:icon"),
            Frame(self.pub_topic_input, height=Dimension.exact(3), style="class:input-frame"),
        ])

        # Row 2: Message (multiline)
        row_msg = VSplit([
            Window(FormattedTextControl("\n  💬  \n"), width=Dimension.exact(6), style="class:icon"),
            Frame(self.pub_msg_input, style="class:input-frame"),
        ])

        # Logs Header Bar
        def get_pub_log_header():
            h_pub = self._make_mouse_click_handler(self.action_publish_message)
            h_clear_msg = self._make_mouse_click_handler(lambda: self.pub_msg_input.buffer.reset())
            h_clear_logs = self._make_mouse_click_handler(lambda: self.clear_log(self.pub_logs))

            return [
                ("class:logs.header", "  📝 Logs"),
                ("class:logs.header", " " * 6),
                ("class:btn.badge", " 🚀 Publish ", h_pub),
                ("class:logs.header", "  "),
                ("class:btn.badge", " 🗑️ Clear Message ", h_clear_msg),
                ("class:logs.header", "  "),
                ("class:btn.badge", " 🧹 Clear Logs ", h_clear_logs),
            ]

        logs_header = Window(
            content=FormattedTextControl(get_pub_log_header),
            height=Dimension.exact(1),
            style="class:logs.header",
        )

        logs_frame = Frame(self.pub_logs, style="class:input-frame")

        return HSplit([
            Window(height=1),
            row_topic,
            Window(height=1),
            row_msg,
            Window(height=1),
            logs_header,
            logs_frame,
        ])

    # --- View 4: Config (🎛️) ---
    def _build_config_view(self):
        def get_config_toolbar():
            h_save = self._make_mouse_click_handler(self.action_save_config)
            h_test = self._make_mouse_click_handler(self.action_test_config)
            h_reload = self._make_mouse_click_handler(self.action_reload_config)

            return [
                ("class:icon", "  🎛️  "),
                ("class:placeholder.title", f"Config: {self.conf_path}"),
                ("class:topbar", "    "),
                ("class:btn.badge", " 💾 Save Config ", h_save),
                ("class:topbar", "  "),
                ("class:btn.badge", " 🧪 Test Config ", h_test),
                ("class:topbar", "  "),
                ("class:btn.badge", " 🔄 Reload ", h_reload),
            ]

        toolbar = Window(
            content=FormattedTextControl(get_config_toolbar),
            height=Dimension.exact(1),
            style="class:topbar",
        )

        editor_frame = Frame(self.config_editor, title="mosquitto.tui.conf Editor", style="class:input-frame")

        def get_cfg_log_header():
            h_clear = self._make_mouse_click_handler(lambda: self.clear_log(self.config_logs))
            return [
                ("class:logs.header", "  📝 Validation & Status Logs"),
                ("class:logs.header", " " * 6),
                ("class:btn.badge", " 🧹 Clear ", h_clear),
            ]

        logs_header = Window(
            content=FormattedTextControl(get_cfg_log_header),
            height=Dimension.exact(1),
            style="class:logs.header",
        )

        logs_frame = Frame(self.config_logs, style="class:input-frame")

        return HSplit([
            Window(height=1),
            toolbar,
            Window(height=1),
            editor_frame,
            Window(height=1),
            logs_header,
            logs_frame,
        ])

    # --- Bottom Status / Help Bar ---
    def _render_bottombar(self):
        fragments = [
            ("class:bottom.key", " F1/1 "),
            ("class:bottom.desc", "Connect  "),
            ("class:bottom.key", " F2/2 "),
            ("class:bottom.desc", "Subscribe  "),
            ("class:bottom.key", " F3/3 "),
            ("class:bottom.desc", "Publish  "),
            ("class:bottom.key", " F4/4 "),
            ("class:bottom.desc", "Config  "),
            ("class:bottom.desc", "|  "),
            ("class:bottom.key", "Tab "),
            ("class:bottom.desc", "Next  "),
            ("class:bottom.key", "Enter/F5 "),
            ("class:bottom.desc", "Action  "),
            ("class:bottom.key", "Ctrl+L "),
            ("class:bottom.desc", "Clear  "),
            ("class:bottom.key", "Ctrl+Q "),
            ("class:bottom.desc", "Quit"),
        ]
        return Window(
            content=FormattedTextControl(fragments),
            height=Dimension.exact(1),
            style="class:bottom.bar",
        )

    def _init_views(self):
        self.view_connect = self._build_connect_view()
        self.view_subscribe = self._build_subscribe_view()
        self.view_publish = self._build_publish_view()
        self.view_config = self._build_config_view()

        def get_active_body():
            if self.current_tab == "connect":
                return self.view_connect
            elif self.current_tab == "subscribe":
                return self.view_subscribe
            elif self.current_tab == "publish":
                return self.view_publish
            elif self.current_tab == "config":
                return self.view_config
            return self.view_connect

        root_container = HSplit([
            self._render_topbar(),
            DynamicContainer(get_active_body),
            self._render_bottombar(),
        ], style="class:root")

        self.layout = Layout(root_container, focused_element=self.server_host_input)

    # --- Actions: Server / Connect ---
    def action_toggle_broker(self):
        """Start or stop the mosquitto broker."""
        if self.server_proc:
            # Stop broker
            self.append_log(self.server_logs, f"Stopping Mosquitto broker (PID {self.server_proc.pid})...")
            try:
                self.server_proc.terminate()
            except Exception as e:
                self.append_log(self.server_logs, f"Error terminating mosquitto: {e}")
            self.server_proc = None
            if self.server_task and not self.server_task.done():
                self.server_task.cancel()
            self.app.invalidate()
        else:
            # Start broker
            port_val = self.server_port_input.text.strip()
            if not port_val or port_val.startswith("1883|9001"):
                port_val = "1883"

            cmd = ["mosquitto", "-c", self.conf_path, "-v"]
            if port_val and port_val != "1883":
                cmd.extend(["-p", port_val])

            self.append_log(self.server_logs, f"Executing: {' '.join(cmd)}")

            async def _run_server():
                try:
                    self.server_proc = await asyncio.create_subprocess_exec(
                        *cmd,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.STDOUT
                    )
                    self.append_log(self.server_logs, f"Mosquitto broker started with PID {self.server_proc.pid}")
                    self.app.invalidate()

                    while True:
                        line = await self.server_proc.stdout.readline()
                        if not line:
                            break
                        decoded = line.decode("utf-8", errors="replace").rstrip()
                        self.append_log(self.server_logs, decoded)
                        self.app.invalidate()

                    rc = await self.server_proc.wait()
                    self.append_log(self.server_logs, f"Mosquitto broker exited with status code {rc}")
                except asyncio.CancelledError:
                    pass
                except Exception as ex:
                    self.append_log(self.server_logs, f"Broker execution error: {ex}")
                finally:
                    self.server_proc = None
                    self.app.invalidate()

            self.server_task = asyncio.create_task(_run_server())
            self.app.invalidate()

    def action_ping_broker(self):
        """Test connection to the configured host and port, and query broker details."""
        host = self.server_host_input.text.strip() or "127.0.0.1"
        port_raw = self.server_port_input.text.strip() or "1883"
        port = 1883
        if port_raw and not port_raw.startswith("1883|9001"):
            try:
                port = int(port_raw.split("|")[0])
            except ValueError:
                port = 1883

        self.append_log(self.server_logs, f"Testing TCP connection to {host}:{port}...")

        async def _check_conn():
            try:
                _, writer = await asyncio.wait_for(
                    asyncio.open_connection(host, port),
                    timeout=2.0
                )
                writer.close()
                await writer.wait_closed()
                self.append_log(self.server_logs, f"✅ Connection SUCCESS: Mosquitto broker is REACHABLE at {host}:{port}!")

                # Query $SYS/broker/version if reachable
                try:
                    sub_check = await asyncio.create_subprocess_exec(
                        "mosquitto_sub", "-h", host, "-p", str(port),
                        "-t", "$SYS/broker/version", "-C", "1", "-W", "2",
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE
                    )
                    out, _ = await asyncio.wait_for(sub_check.communicate(), timeout=3.0)
                    if out:
                        ver_str = out.decode("utf-8", errors="replace").strip()
                        self.append_log(self.server_logs, f"📊 Broker Info: {ver_str}")
                except Exception:
                    pass
            except Exception as e:
                self.append_log(self.server_logs, f"❌ Connection FAILED to {host}:{port}: {e}")
            self.app.invalidate()

        asyncio.create_task(_check_conn())

    # --- Actions: Subscribe ---
    def action_toggle_subscribe(self):
        """Start or stop mosquitto_sub."""
        if self.sub_proc:
            self.append_log(self.sub_logs, f"Stopping subscription (PID {self.sub_proc.pid})...")
            try:
                self.sub_proc.terminate()
            except Exception as e:
                self.append_log(self.sub_logs, f"Error terminating mosquitto_sub: {e}")
            self.sub_proc = None
            if self.sub_task and not self.sub_task.done():
                self.sub_task.cancel()
            self.app.invalidate()
        else:
            host = self.server_host_input.text.strip() or "127.0.0.1"
            port_raw = self.sub_port_input.text.strip() or self.server_port_input.text.strip() or "1883"
            port = "1883"
            if port_raw and not port_raw.startswith("1883|9001"):
                port = port_raw.split("|")[0].strip()

            # Comma-separated topics hack
            topics_raw = self.sub_topics_input.text.strip()
            if not topics_raw or topics_raw == "#":
                topics = ["#"]
            else:
                topics = [t.strip() for t in topics_raw.split(",") if t.strip()]
                if not topics:
                    topics = ["#"]

            cmd = ["mosquitto_sub", "-h", host, "-p", port, "-v"]
            if port == "9001":
                cmd.append("--ws")
            for t in topics:
                cmd.extend(["-t", t])

            self.append_log(self.sub_logs, f"Starting mosquitto_sub: {' '.join(cmd)}")

            async def _run_subscriber():
                try:
                    self.sub_proc = await asyncio.create_subprocess_exec(
                        *cmd,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.STDOUT
                    )
                    self.append_log(self.sub_logs, f"Subscribed to {topics} (PID {self.sub_proc.pid}). Listening...")
                    self.app.invalidate()

                    while True:
                        line = await self.sub_proc.stdout.readline()
                        if not line:
                            break
                        decoded = line.decode("utf-8", errors="replace").rstrip()
                        self.append_log(self.sub_logs, decoded)
                        self.app.invalidate()

                    rc = await self.sub_proc.wait()
                    self.append_log(self.sub_logs, f"mosquitto_sub exited with code {rc}")
                except asyncio.CancelledError:
                    pass
                except Exception as ex:
                    self.append_log(self.sub_logs, f"Subscription error: {ex}")
                finally:
                    self.sub_proc = None
                    self.app.invalidate()

            self.sub_task = asyncio.create_task(_run_subscriber())
            self.app.invalidate()

    # --- Actions: Publish ---
    def action_publish_message(self):
        """Publish the message in the text area using mosquitto_pub."""
        host = self.server_host_input.text.strip() or "127.0.0.1"
        port_raw = self.server_port_input.text.strip() or "1883"
        port = "1883"
        if port_raw and not port_raw.startswith("1883|9001"):
            port = port_raw.split("|")[0].strip()

        topic_raw = self.pub_topic_input.text.strip()
        if not topic_raw or topic_raw == "#":
            # MQTT spec prohibits wildcard '#' for publishing
            topics = ["test/tui"]
            self.append_log(self.pub_logs, "Notice: '#' is a wildcard topic. Defaulting publish to 'test/tui'.")
        else:
            topics = [t.strip() for t in topic_raw.split(",") if t.strip()]

        message = self.pub_msg_input.text
        if not message:
            self.append_log(self.pub_logs, "Warning: Empty payload. Publishing empty message.")

        async def _do_publish():
            for top in topics:
                cmd = ["mosquitto_pub", "-h", host, "-p", port, "-t", top, "-m", message]
                if port == "9001":
                    cmd.append("--ws")
                preview = (message[:40] + "...") if len(message) > 40 else message
                preview_clean = preview.replace("\n", " ")
                self.append_log(self.pub_logs, f"Publishing to '{top}': {preview_clean}")
                try:
                    proc = await asyncio.create_subprocess_exec(
                        *cmd,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE
                    )
                    stdout, stderr = await proc.communicate()
                    if proc.returncode == 0:
                        self.append_log(self.pub_logs, f"✅ Published to '{top}' successfully (exit 0).")
                    else:
                        err_msg = stderr.decode("utf-8", errors="replace").strip()
                        self.append_log(self.pub_logs, f"❌ Failed to publish to '{top}' (exit {proc.returncode}): {err_msg}")
                except Exception as ex:
                    self.append_log(self.pub_logs, f"❌ Error executing mosquitto_pub: {ex}")
                self.app.invalidate()

        asyncio.create_task(_do_publish())

    # --- Actions: Config ---
    def action_save_config(self):
        """Save the editor content to mosquitto.tui.conf."""
        content = self.config_editor.text
        try:
            with open(self.conf_path, "w", encoding="utf-8") as f:
                f.write(content)
            self.append_log(self.config_logs, f"💾 Saved configuration to {self.conf_path} ({len(content)} bytes).")
        except Exception as e:
            self.append_log(self.config_logs, f"❌ Failed to save config: {e}")
        if hasattr(self, "app"):
            self.app.invalidate()

    def action_test_config(self):
        """Run mosquitto --test-config on the current config file."""
        self.action_save_config()
        self.append_log(self.config_logs, f"Running: mosquitto --test-config -c {self.conf_path}")

        async def _run_test():
            try:
                proc = await asyncio.create_subprocess_exec(
                    "mosquitto", "--test-config", "-c", self.conf_path,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.STDOUT
                )
                stdout, _ = await proc.communicate()
                out_decoded = stdout.decode("utf-8", errors="replace").strip()
                for line in out_decoded.splitlines():
                    self.append_log(self.config_logs, line)
                if proc.returncode == 0:
                    self.append_log(self.config_logs, "✅ mosquitto syntax validation PASSED!")
                else:
                    self.append_log(self.config_logs, f"❌ Validation FAILED (exit code {proc.returncode}).")
            except Exception as e:
                self.append_log(self.config_logs, f"❌ Execution error: {e}")
            self.app.invalidate()

        asyncio.create_task(_run_test())

    def action_reload_config(self):
        """Reload the config file from disk."""
        try:
            with open(self.conf_path, "r", encoding="utf-8") as f:
                content = f.read()
            self.config_editor.text = content
            self.append_log(self.config_logs, f"🔄 Reloaded {self.conf_path} from disk.")
        except Exception as e:
            self.append_log(self.config_logs, f"❌ Error reloading config: {e}")
        self.app.invalidate()

    # --- Key Bindings ---
    def _init_keybindings(self):
        self.kb = KeyBindings()

        # Quit
        @self.kb.add("c-q")
        @self.kb.add("c-c")
        def _quit(event):
            # Clean up child processes
            if self.server_proc:
                try:
                    self.server_proc.terminate()
                except Exception:
                    pass
            if self.sub_proc:
                try:
                    self.sub_proc.terminate()
                except Exception:
                    pass
            event.app.exit()

        # Switch tabs via F1-F4 or Alt+1..4
        @self.kb.add("f1")
        @self.kb.add("escape", "1")
        def _tab1(event):
            self.switch_tab("connect")

        @self.kb.add("f2")
        @self.kb.add("escape", "2")
        def _tab2(event):
            self.switch_tab("subscribe")

        @self.kb.add("f3")
        @self.kb.add("escape", "3")
        def _tab3(event):
            self.switch_tab("publish")

        @self.kb.add("f4")
        @self.kb.add("escape", "4")
        def _tab4(event):
            self.switch_tab("config")

        # Tab navigation between inputs
        self.kb.add("tab")(focus_next)
        self.kb.add("s-tab")(focus_previous)

        # Clear current tab logs
        @self.kb.add("c-l")
        def _clear_logs(event):
            if self.current_tab == "connect":
                self.clear_log(self.server_logs)
            elif self.current_tab == "subscribe":
                self.clear_log(self.sub_logs)
            elif self.current_tab == "publish":
                self.clear_log(self.pub_logs)
            elif self.current_tab == "config":
                self.clear_log(self.config_logs)
            event.app.invalidate()

        # Action shortcuts (F5, Alt+Enter, Ctrl+J)
        @self.kb.add("f5")
        @self.kb.add("c-j")
        @self.kb.add("escape", "enter")
        def _action_trigger(event):
            if self.current_tab == "publish":
                self.action_publish_message()
            elif self.current_tab == "connect":
                self.action_toggle_broker()
            elif self.current_tab == "subscribe":
                self.action_toggle_subscribe()
            elif self.current_tab == "config":
                self.action_test_config()

        @self.kb.add("c-s")
        def _ctrl_s(event):
            if self.current_tab == "config":
                self.action_save_config()

        @self.kb.add("c-t")
        def _ctrl_t(event):
            if self.current_tab == "config":
                self.action_test_config()

        @self.kb.add("c-r")
        def _ctrl_r(event):
            if self.current_tab == "config":
                self.action_reload_config()

        # Enter in single line inputs triggers action or moves focus
        @self.kb.add("enter")
        def _enter_key(event):
            buf = event.app.current_buffer
            if buf == self.server_port_input.buffer:
                self.action_toggle_broker()
            elif buf == self.sub_port_input.buffer:
                self.action_toggle_subscribe()
            elif buf == self.pub_topic_input.buffer:
                event.app.layout.focus(self.pub_msg_input)
            elif buf == self.pub_msg_input.buffer:
                buf.insert_text("\n")
            elif buf == self.config_editor.buffer:
                buf.insert_text("\n")
            else:
                focus_next(event)

    def run(self):
        """Start the TUI Application."""
        self.app.run()


def main():
    # Verify required executables exist or notify user
    for exe in ["mosquitto", "mosquitto_sub", "mosquitto_pub"]:
        if not shutil.which(exe):
            print(f"Warning: '{exe}' was not found in PATH.", file=sys.stderr)

    tui = MqttTuiApp()
    tui.run()


if __name__ == "__main__":
    main()
