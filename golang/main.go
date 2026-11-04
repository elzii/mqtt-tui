package main

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/charmbracelet/bubbles/textarea"
	"github.com/charmbracelet/bubbles/textinput"
	"github.com/charmbracelet/bubbles/viewport"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

// Active Tab
type Tab int

const (
	TabConnect Tab = iota
	TabSubscribe
	TabPublish
	TabConfig
)

// Focus Target
type FocusIndex int

const (
	FocusServerHost FocusIndex = iota
	FocusServerPort
	FocusServerBtnStart
	FocusServerBtnStop
	FocusServerBtnPing
	FocusServerBtnClear
	FocusServerLogs

	FocusSubTopics
	FocusSubPort
	FocusSubBtnStart
	FocusSubBtnStop
	FocusSubBtnBeautify
	FocusSubBtnClear
	FocusSubLogs

	FocusPubTopic
	FocusPubMsg
	FocusPubBtnPublish
	FocusPubBtnBeautify
	FocusPubBtnClear
	FocusPubLogs

	FocusConfigEditor
	FocusConfigBtnSave
	FocusConfigBtnTest
	FocusConfigBtnReload
	FocusConfigBtnClear
	FocusConfigLogs
)

// Messages
type logMsg struct {
	tab  Tab
	line string
}

type statusMsg struct {
	tab    Tab
	status string
}

// Colors and Styles matching reference Python TUI
var (
	colorOrange   = lipgloss.Color("#ff9f43")
	colorGreen    = lipgloss.Color("#2ed573")
	colorBlue     = lipgloss.Color("#54a0ff")
	colorMagenta  = lipgloss.Color("#e056fd")
	colorAmber    = lipgloss.Color("#ffa502")
	colorDarkBg   = lipgloss.Color("#0d0e12")
	colorTopBar   = lipgloss.Color("#15181f")
	colorFrame    = lipgloss.Color("#3b4252")
	colorLogHdr   = lipgloss.Color("#383e4a")
	colorBadgeBg  = lipgloss.Color("#808080")
	colorBadgeFg  = lipgloss.Color("#1c1c1c")
	colorTabInact = lipgloss.Color("#232730")
	colorTabFg    = lipgloss.Color("#a4b0be")

	// Styles
	styleTopBar = lipgloss.NewStyle().Background(colorTopBar).Foreground(lipgloss.Color("#e5e9f0"))
	styleBadge  = lipgloss.NewStyle().Background(colorBadgeBg).Foreground(colorBadgeFg).Bold(true)
	styleLogHdr = lipgloss.NewStyle().Background(colorLogHdr).Foreground(lipgloss.Color("#f1f2f6")).Bold(true)

	// Tab styles
	styleTabInactive = lipgloss.NewStyle().Background(colorTabInact).Foreground(colorTabFg)
	styleTabConnect  = lipgloss.NewStyle().Background(lipgloss.Color("#5af78e")).Foreground(lipgloss.Color("#0c1c14")).Bold(true)
	styleTabSub      = lipgloss.NewStyle().Background(colorBlue).Foreground(lipgloss.Color("#0a192f")).Bold(true)
	styleTabPub      = lipgloss.NewStyle().Background(colorMagenta).Foreground(lipgloss.Color("#25092a")).Bold(true)
	styleTabCfg      = lipgloss.NewStyle().Background(colorAmber).Foreground(lipgloss.Color("#241400")).Bold(true)

	// Box borders
	styleBoxNormal  = lipgloss.NewStyle().Border(lipgloss.NormalBorder()).BorderForeground(colorFrame)
	styleBoxFocused = lipgloss.NewStyle().Border(lipgloss.NormalBorder()).BorderForeground(colorBlue)
)

type model struct {
	tab        Tab
	focusIndex FocusIndex
	width      int
	height     int

	// Connect Tab
	serverHostInput textinput.Model
	serverPortInput textinput.Model
	serverLogs      string
	serverVp        viewport.Model
	serverCmd       *exec.Cmd
	serverRunning   bool

	// Subscribe Tab
	subTopicsInput textinput.Model
	subPortInput   textinput.Model
	subLogs        string
	subVp          viewport.Model
	subCmd         *exec.Cmd
	subRunning     bool

	// Publish Tab
	pubTopicInput textinput.Model
	pubMsgInput   textarea.Model
	pubLogs       string
	pubVp         viewport.Model

	// Config Tab
	configPath   string
	configEditor textarea.Model
	configLogs   string
	configVp     viewport.Model

	// History Navigation
	histIndices map[FocusIndex]int

	mu sync.Mutex
}

func initialModel() model {
	home, _ := os.UserHomeDir()
	confPath := filepath.Join(home, ".config", "mosquitto", "mosquitto.tui.conf")

	// Initialize inputs
	hostIn := textinput.New()
	hostIn.Placeholder = "Host Address (DEFAULT = 127.0.0.1)"
	hostIn.Prompt = ""
	hostIn.Focus()

	portIn := textinput.New()
	portIn.Placeholder = "Port (DEFAULT = 1883|9001)"
	portIn.Prompt = ""

	subTopIn := textinput.New()
	subTopIn.Placeholder = "Topics (Comma Separated, DEFAULT = #)"
	subTopIn.Prompt = ""

	subPortIn := textinput.New()
	subPortIn.Placeholder = "Port (DEFAULT = 1883|9001)"
	subPortIn.Prompt = ""

	pubTopIn := textinput.New()
	pubTopIn.Placeholder = "Topic (Comma Separated, DEFAULT = #)"
	pubTopIn.Prompt = ""

	pubMsgIn := textarea.New()
	pubMsgIn.Placeholder = "Message"
	pubMsgIn.ShowLineNumbers = false
	pubMsgIn.SetHeight(5)

	// Config Editor
	cfgEditor := textarea.New()
	cfgEditor.ShowLineNumbers = true
	cfgContent, err := os.ReadFile(confPath)
	if err == nil {
		cfgEditor.SetValue(string(cfgContent))
	} else {
		cfgEditor.SetValue("# " + confPath + " not found")
	}

	m := model{
		tab:             TabConnect,
		focusIndex:      FocusServerHost,
		width:           100,
		height:          30,
		serverHostInput: hostIn,
		serverPortInput: portIn,
		subTopicsInput:  subTopIn,
		subPortInput:    subPortIn,
		pubTopicInput:   pubTopIn,
		pubMsgInput:     pubMsgIn,
		configPath:      confPath,
		configEditor:    cfgEditor,
		histIndices:     make(map[FocusIndex]int),
		serverVp:        viewport.New(80, 10),
		subVp:           viewport.New(80, 10),
		pubVp:           viewport.New(80, 10),
		configVp:        viewport.New(80, 6),
	}

	now := time.Now().Format("15:04:05")
	m.serverLogs = fmt.Sprintf("[%s] Mosquitto TUI (Go) initialized. Ready to connect or start broker.\n", now)
	m.subLogs = fmt.Sprintf("[%s] Subscriber ready. Enter topics or leave blank for wildcard '#'.\n", now)
	m.pubLogs = fmt.Sprintf("[%s] Publisher ready. Enter topic and message, then press Publish (Enter).\n", now)
	m.configLogs = fmt.Sprintf("[%s] Loaded configuration file: %s\n", now, confPath)

	m.serverVp.SetContent(m.serverLogs)
	m.subVp.SetContent(m.subLogs)
	m.pubVp.SetContent(m.pubLogs)
	m.configVp.SetContent(m.configLogs)

	return m
}

func (m model) Init() tea.Cmd {
	return textinput.Blink
}

func (m *model) appendLog(tab Tab, text string) {
	m.mu.Lock()
	defer m.mu.Unlock()

	now := time.Now().Format("15:04:05")
	entry := fmt.Sprintf("[%s] %s\n", now, text)

	switch tab {
	case TabConnect:
		m.serverLogs += entry
		m.serverVp.SetContent(m.serverLogs)
		m.serverVp.GotoBottom()
	case TabSubscribe:
		m.subLogs += entry
		m.subVp.SetContent(m.subLogs)
		m.subVp.GotoBottom()
	case TabPublish:
		m.pubLogs += entry
		m.pubVp.SetContent(m.pubLogs)
		m.pubVp.GotoBottom()
	case TabConfig:
		m.configLogs += entry
		m.configVp.SetContent(m.configLogs)
		m.configVp.GotoBottom()
	}
}

func (m *model) switchTab(t Tab) {
	m.tab = t
	switch t {
	case TabConnect:
		m.setFocus(FocusServerHost)
	case TabSubscribe:
		m.setFocus(FocusSubTopics)
	case TabPublish:
		m.setFocus(FocusPubTopic)
	case TabConfig:
		m.setFocus(FocusConfigEditor)
	}
}

func (m *model) setFocus(fi FocusIndex) {
	m.focusIndex = fi
	m.serverHostInput.Blur()
	m.serverPortInput.Blur()
	m.subTopicsInput.Blur()
	m.subPortInput.Blur()
	m.pubTopicInput.Blur()
	m.pubMsgInput.Blur()
	m.configEditor.Blur()

	switch fi {
	case FocusServerHost:
		m.serverHostInput.Focus()
	case FocusServerPort:
		m.serverPortInput.Focus()
	case FocusSubTopics:
		m.subTopicsInput.Focus()
	case FocusSubPort:
		m.subPortInput.Focus()
	case FocusPubTopic:
		m.pubTopicInput.Focus()
	case FocusPubMsg:
		m.pubMsgInput.Focus()
	case FocusConfigEditor:
		m.configEditor.Focus()
	}
}

func (m *model) focusNext() {
	switch m.tab {
	case TabConnect:
		switch m.focusIndex {
		case FocusServerHost:
			m.setFocus(FocusServerPort)
		case FocusServerPort:
			m.setFocus(FocusServerBtnStart)
		case FocusServerBtnStart:
			m.setFocus(FocusServerBtnStop)
		case FocusServerBtnStop:
			m.setFocus(FocusServerBtnPing)
		case FocusServerBtnPing:
			m.setFocus(FocusServerBtnClear)
		case FocusServerBtnClear:
			m.setFocus(FocusServerLogs)
		default:
			m.setFocus(FocusServerHost)
		}
	case TabSubscribe:
		switch m.focusIndex {
		case FocusSubTopics:
			m.setFocus(FocusSubPort)
		case FocusSubPort:
			m.setFocus(FocusSubBtnStart)
		case FocusSubBtnStart:
			m.setFocus(FocusSubBtnStop)
		case FocusSubBtnStop:
			m.setFocus(FocusSubBtnBeautify)
		case FocusSubBtnBeautify:
			m.setFocus(FocusSubBtnClear)
		case FocusSubBtnClear:
			m.setFocus(FocusSubLogs)
		default:
			m.setFocus(FocusSubTopics)
		}
	case TabPublish:
		switch m.focusIndex {
		case FocusPubTopic:
			m.setFocus(FocusPubMsg)
		case FocusPubMsg:
			m.setFocus(FocusPubBtnPublish)
		case FocusPubBtnPublish:
			m.setFocus(FocusPubBtnBeautify)
		case FocusPubBtnBeautify:
			m.setFocus(FocusPubBtnClear)
		case FocusPubBtnClear:
			m.setFocus(FocusPubLogs)
		default:
			m.setFocus(FocusPubTopic)
		}
	case TabConfig:
		switch m.focusIndex {
		case FocusConfigEditor:
			m.setFocus(FocusConfigBtnSave)
		case FocusConfigBtnSave:
			m.setFocus(FocusConfigBtnTest)
		case FocusConfigBtnTest:
			m.setFocus(FocusConfigBtnReload)
		case FocusConfigBtnReload:
			m.setFocus(FocusConfigBtnClear)
		case FocusConfigBtnClear:
			m.setFocus(FocusConfigLogs)
		default:
			m.setFocus(FocusConfigEditor)
		}
	}
}

func (m *model) focusPrev() {
	switch m.tab {
	case TabConnect:
		switch m.focusIndex {
		case FocusServerLogs:
			m.setFocus(FocusServerBtnClear)
		case FocusServerBtnClear:
			m.setFocus(FocusServerBtnPing)
		case FocusServerBtnPing:
			m.setFocus(FocusServerBtnStop)
		case FocusServerBtnStop:
			m.setFocus(FocusServerBtnStart)
		case FocusServerBtnStart:
			m.setFocus(FocusServerPort)
		case FocusServerPort:
			m.setFocus(FocusServerHost)
		default:
			m.setFocus(FocusServerLogs)
		}
	case TabSubscribe:
		switch m.focusIndex {
		case FocusSubLogs:
			m.setFocus(FocusSubBtnClear)
		case FocusSubBtnClear:
			m.setFocus(FocusSubBtnBeautify)
		case FocusSubBtnBeautify:
			m.setFocus(FocusSubBtnStop)
		case FocusSubBtnStop:
			m.setFocus(FocusSubBtnStart)
		case FocusSubBtnStart:
			m.setFocus(FocusSubPort)
		case FocusSubPort:
			m.setFocus(FocusSubTopics)
		default:
			m.setFocus(FocusSubLogs)
		}
	case TabPublish:
		switch m.focusIndex {
		case FocusPubLogs:
			m.setFocus(FocusPubBtnClear)
		case FocusPubBtnClear:
			m.setFocus(FocusPubBtnBeautify)
		case FocusPubBtnBeautify:
			m.setFocus(FocusPubBtnPublish)
		case FocusPubBtnPublish:
			m.setFocus(FocusPubMsg)
		case FocusPubMsg:
			m.setFocus(FocusPubTopic)
		default:
			m.setFocus(FocusPubLogs)
		}
	case TabConfig:
		switch m.focusIndex {
		case FocusConfigLogs:
			m.setFocus(FocusConfigBtnClear)
		case FocusConfigBtnClear:
			m.setFocus(FocusConfigBtnReload)
		case FocusConfigBtnReload:
			m.setFocus(FocusConfigBtnTest)
		case FocusConfigBtnTest:
			m.setFocus(FocusConfigBtnSave)
		case FocusConfigBtnSave:
			m.setFocus(FocusConfigEditor)
		default:
			m.setFocus(FocusConfigLogs)
		}
	}
}

func (m *model) navigateHistory(up bool) {
	var fieldName string
	var input *textinput.Model

	switch m.focusIndex {
	case FocusServerHost:
		fieldName = "host"
		input = &m.serverHostInput
	case FocusServerPort:
		fieldName = "port"
		input = &m.serverPortInput
	case FocusSubTopics:
		fieldName = "subscribe_topics"
		input = &m.subTopicsInput
	case FocusSubPort:
		fieldName = "subscribe_port"
		input = &m.subPortInput
	case FocusPubTopic:
		fieldName = "publish_topic"
		input = &m.pubTopicInput
	default:
		return
	}

	hist := LoadHistory(fieldName)
	if len(hist) == 0 {
		return
	}

	idx, exists := m.histIndices[m.focusIndex]
	if !exists {
		idx = len(hist)
	}

	if up {
		if idx > 0 {
			idx--
			input.SetValue(hist[idx])
			m.histIndices[m.focusIndex] = idx
		}
	} else {
		if idx < len(hist)-1 {
			idx++
			input.SetValue(hist[idx])
			m.histIndices[m.focusIndex] = idx
		} else {
			idx = len(hist)
			input.SetValue("")
			m.histIndices[m.focusIndex] = idx
		}
	}
}

// Background Actions
func (m *model) toggleBroker() {
	host := strings.TrimSpace(m.serverHostInput.Value())
	port := strings.TrimSpace(m.serverPortInput.Value())
	if host != "" {
		SaveHistory("host", host)
	}
	if port != "" {
		SaveHistory("port", port)
	}

	if m.serverRunning && m.serverCmd != nil {
		m.appendLog(TabConnect, fmt.Sprintf("Stopping Mosquitto broker (PID %d)...", m.serverCmd.Process.Pid))
		_ = m.serverCmd.Process.Kill()
		m.serverRunning = false
		m.serverCmd = nil
		return
	}

	if port == "" || strings.HasPrefix(port, "1883|9001") {
		port = "1883"
	}

	args := []string{"-c", m.configPath, "-v"}
	if port != "1883" {
		args = append(args, "-p", port)
	}

	cmd := exec.Command("mosquitto", args...)
	m.appendLog(TabConnect, fmt.Sprintf("Executing: mosquitto %s", strings.Join(args, " ")))

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		m.appendLog(TabConnect, fmt.Sprintf("Error starting broker: %v", err))
		return
	}
	cmd.Stderr = cmd.Stdout

	if err := cmd.Start(); err != nil {
		m.appendLog(TabConnect, fmt.Sprintf("Failed to launch mosquitto: %v", err))
		return
	}

	m.serverCmd = cmd
	m.serverRunning = true
	m.appendLog(TabConnect, fmt.Sprintf("Mosquitto broker started with PID %d", cmd.Process.Pid))

	go func() {
		scanner := bufio.NewScanner(stdout)
		for scanner.Scan() {
			m.appendLog(TabConnect, scanner.Text())
		}
		_ = cmd.Wait()
		m.mu.Lock()
		m.serverRunning = false
		m.serverCmd = nil
		m.mu.Unlock()
		m.appendLog(TabConnect, "Mosquitto broker process terminated.")
	}()
}

func (m *model) pingBroker() {
	host := strings.TrimSpace(m.serverHostInput.Value())
	if host == "" {
		host = "127.0.0.1"
	}
	portRaw := strings.TrimSpace(m.serverPortInput.Value())
	port := "1883"
	if portRaw != "" && !strings.HasPrefix(portRaw, "1883|9001") {
		port = strings.Split(portRaw, "|")[0]
	}

	SaveHistory("host", host)
	SaveHistory("port", port)

	m.appendLog(TabConnect, fmt.Sprintf("Testing TCP connection to %s:%s...", host, port))

	go func() {
		conn, err := net.DialTimeout("tcp", net.JoinHostPort(host, port), 2*time.Second)
		if err != nil {
			m.appendLog(TabConnect, fmt.Sprintf("❌ Connection FAILED to %s:%s: %v", host, port, err))
			return
		}
		_ = conn.Close()
		m.appendLog(TabConnect, fmt.Sprintf("✅ Connection SUCCESS: Mosquitto broker is REACHABLE at %s:%s!", host, port))

		// Query broker version
		subCmd := exec.Command("mosquitto_sub", "-h", host, "-p", port, "-t", "$SYS/broker/version", "-C", "1", "-W", "2")
		out, err := subCmd.Output()
		if err == nil && len(out) > 0 {
			m.appendLog(TabConnect, fmt.Sprintf("📊 Broker Info: %s", strings.TrimSpace(string(out))))
		}
	}()
}

func (m *model) toggleSubscribe() {
	topicsRaw := strings.TrimSpace(m.subTopicsInput.Value())
	portRaw := strings.TrimSpace(m.subPortInput.Value())
	if topicsRaw != "" {
		SaveHistory("subscribe_topics", topicsRaw)
	}
	if portRaw != "" {
		SaveHistory("subscribe_port", portRaw)
	}

	if m.subRunning && m.subCmd != nil {
		m.appendLog(TabSubscribe, fmt.Sprintf("Stopping subscription (PID %d)...", m.subCmd.Process.Pid))
		_ = m.subCmd.Process.Kill()
		m.subRunning = false
		m.subCmd = nil
		return
	}

	host := strings.TrimSpace(m.serverHostInput.Value())
	if host == "" {
		host = "127.0.0.1"
	}
	port := "1883"
	if portRaw != "" && !strings.HasPrefix(portRaw, "1883|9001") {
		port = strings.Split(portRaw, "|")[0]
	}

	var topics []string
	if topicsRaw == "" || topicsRaw == "#" {
		topics = []string{"#"}
	} else {
		for _, t := range strings.Split(topicsRaw, ",") {
			tr := strings.TrimSpace(t)
			if tr != "" {
				topics = append(topics, tr)
			}
		}
		if len(topics) == 0 {
			topics = []string{"#"}
		}
	}

	args := []string{"-h", host, "-p", port, "-v"}
	if port == "9001" {
		args = append(args, "--ws")
	}
	for _, t := range topics {
		args = append(args, "-t", t)
	}

	cmd := exec.Command("mosquitto_sub", args...)
	m.appendLog(TabSubscribe, fmt.Sprintf("Starting mosquitto_sub: mosquitto_sub %s", strings.Join(args, " ")))

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		m.appendLog(TabSubscribe, fmt.Sprintf("Error: %v", err))
		return
	}
	cmd.Stderr = cmd.Stdout

	if err := cmd.Start(); err != nil {
		m.appendLog(TabSubscribe, fmt.Sprintf("Failed to launch mosquitto_sub: %v", err))
		return
	}

	m.subCmd = cmd
	m.subRunning = true
	m.appendLog(TabSubscribe, fmt.Sprintf("Subscribed to %v (PID %d). Listening...", topics, cmd.Process.Pid))

	go func() {
		scanner := bufio.NewScanner(stdout)
		for scanner.Scan() {
			m.appendLog(TabSubscribe, scanner.Text())
		}
		_ = cmd.Wait()
		m.mu.Lock()
		m.subRunning = false
		m.subCmd = nil
		m.mu.Unlock()
		m.appendLog(TabSubscribe, "mosquitto_sub exited.")
	}()
}

func (m *model) publishMessage() {
	host := strings.TrimSpace(m.serverHostInput.Value())
	if host == "" {
		host = "127.0.0.1"
	}
	portRaw := strings.TrimSpace(m.serverPortInput.Value())
	port := "1883"
	if portRaw != "" && !strings.HasPrefix(portRaw, "1883|9001") {
		port = strings.Split(portRaw, "|")[0]
	}

	topicRaw := strings.TrimSpace(m.pubTopicInput.Value())
	message := m.pubMsgInput.Value()

	if topicRaw != "" {
		SaveHistory("publish_topic", topicRaw)
	}
	if message != "" {
		SaveHistory("publish_message", message)
	}

	var topics []string
	if topicRaw == "" || topicRaw == "#" {
		topics = []string{"test/tui"}
		m.appendLog(TabPublish, "Notice: '#' is a wildcard topic. Defaulting publish to 'test/tui'.")
	} else {
		for _, t := range strings.Split(topicRaw, ",") {
			tr := strings.TrimSpace(t)
			if tr != "" {
				topics = append(topics, tr)
			}
		}
	}

	preview := message
	if len(preview) > 40 {
		preview = preview[:40] + "..."
	}
	preview = strings.ReplaceAll(preview, "\n", " ")

	go func() {
		for _, top := range topics {
			m.appendLog(TabPublish, fmt.Sprintf("Publishing to '%s': %s", top, preview))
			args := []string{"-h", host, "-p", port, "-t", top, "-m", message}
			if port == "9001" {
				args = append(args, "--ws")
			}
			cmd := exec.Command("mosquitto_pub", args...)
			out, err := cmd.CombinedOutput()
			if err == nil {
				m.appendLog(TabPublish, fmt.Sprintf("✅ Published to '%s' successfully (exit 0).", top))
			} else {
				m.appendLog(TabPublish, fmt.Sprintf("❌ Failed to publish to '%s': %s", top, strings.TrimSpace(string(out))))
			}
		}
	}()
}

func (m *model) saveConfig() {
	content := m.configEditor.Value()
	err := os.WriteFile(m.configPath, []byte(content), 0644)
	if err == nil {
		m.appendLog(TabConfig, fmt.Sprintf("💾 Saved configuration to %s (%d bytes).", m.configPath, len(content)))
	} else {
		m.appendLog(TabConfig, fmt.Sprintf("❌ Failed to save config: %v", err))
	}
}

func (m *model) testConfig() {
	m.saveConfig()
	m.appendLog(TabConfig, fmt.Sprintf("Running: mosquitto --test-config -c %s", m.configPath))

	go func() {
		cmd := exec.Command("mosquitto", "--test-config", "-c", m.configPath)
		out, err := cmd.CombinedOutput()
		lines := strings.Split(strings.TrimSpace(string(out)), "\n")
		for _, l := range lines {
			if strings.TrimSpace(l) != "" {
				m.appendLog(TabConfig, l)
			}
		}
		if err == nil {
			m.appendLog(TabConfig, "✅ mosquitto syntax validation PASSED!")
		} else {
			m.appendLog(TabConfig, "❌ Validation FAILED.")
		}
	}()
}

func (m *model) reloadConfig() {
	content, err := os.ReadFile(m.configPath)
	if err == nil {
		m.configEditor.SetValue(string(content))
		m.appendLog(TabConfig, fmt.Sprintf("🔄 Reloaded %s from disk.", m.configPath))
	} else {
		m.appendLog(TabConfig, fmt.Sprintf("❌ Error reloading config: %v", err))
	}
}

func (m *model) beautify(tab Tab) {
	switch tab {
	case TabSubscribe:
		m.subLogs = BeautifyJSON(m.subLogs)
		m.subVp.SetContent(m.subLogs)
	case TabPublish:
		m.pubLogs = BeautifyJSON(m.pubLogs)
		m.pubVp.SetContent(m.pubLogs)
	}
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmds []tea.Cmd

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.serverVp.Width = msg.Width - 4
		m.serverVp.Height = msg.Height - 14
		m.subVp.Width = msg.Width - 4
		m.subVp.Height = msg.Height - 14
		m.pubVp.Width = msg.Width - 4
		m.pubVp.Height = msg.Height - 19
		m.configVp.Width = msg.Width - 4
		m.configVp.Height = 7
		m.configEditor.SetWidth(msg.Width - 6)
		m.configEditor.SetHeight(msg.Height - 18)

	case tea.MouseMsg:
		switch msg.Type {
		case tea.MouseWheelUp:
			switch m.tab {
			case TabConnect:
				m.serverVp.LineUp(2)
			case TabSubscribe:
				m.subVp.LineUp(2)
			case TabPublish:
				m.pubVp.LineUp(2)
			case TabConfig:
				m.configVp.LineUp(2)
			}
		case tea.MouseWheelDown:
			switch m.tab {
			case TabConnect:
				m.serverVp.LineDown(2)
			case TabSubscribe:
				m.subVp.LineDown(2)
			case TabPublish:
				m.pubVp.LineDown(2)
			case TabConfig:
				m.configVp.LineDown(2)
			}
		case tea.MouseRelease:
			if msg.Y == 0 {
				// Calculate approximate tab offsets based on width
				if msg.X >= m.width-45 && msg.X < m.width-32 {
					m.switchTab(TabConnect)
				} else if msg.X >= m.width-32 && msg.X < m.width-18 {
					m.switchTab(TabSubscribe)
				} else if msg.X >= m.width-18 && msg.X < m.width-8 {
					m.switchTab(TabPublish)
				} else if msg.X >= m.width-8 {
					m.switchTab(TabConfig)
				}
			}
		}

	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+c", "ctrl+q":
			if m.serverCmd != nil && m.serverCmd.Process != nil {
				_ = m.serverCmd.Process.Kill()
			}
			if m.subCmd != nil && m.subCmd.Process != nil {
				_ = m.subCmd.Process.Kill()
			}
			return m, tea.Quit

		case "f1", "alt+1":
			m.switchTab(TabConnect)
			return m, nil
		case "f2", "alt+2":
			m.switchTab(TabSubscribe)
			return m, nil
		case "f3", "alt+3":
			m.switchTab(TabPublish)
			return m, nil
		case "f4", "alt+4":
			m.switchTab(TabConfig)
			return m, nil

		case "tab":
			m.focusNext()
			return m, nil
		case "shift+tab":
			m.focusPrev()
			return m, nil

		case "up":
			if m.focusIndex != FocusPubMsg && m.focusIndex != FocusConfigEditor && !strings.Contains(m.focusName(), "Logs") {
				m.navigateHistory(true)
				return m, nil
			}
		case "down":
			if m.focusIndex != FocusPubMsg && m.focusIndex != FocusConfigEditor && !strings.Contains(m.focusName(), "Logs") {
				m.navigateHistory(false)
				return m, nil
			}

		case "ctrl+l":
			switch m.tab {
			case TabConnect:
				m.serverLogs = ""
				m.serverVp.SetContent("")
			case TabSubscribe:
				m.subLogs = ""
				m.subVp.SetContent("")
			case TabPublish:
				m.pubLogs = ""
				m.pubVp.SetContent("")
			case TabConfig:
				m.configLogs = ""
				m.configVp.SetContent("")
			}
			return m, nil

		case "f5", "enter":
			// If focus is on a button, trigger that button's action
			switch m.focusIndex {
			case FocusServerBtnStart, FocusServerBtnStop:
				m.toggleBroker()
				return m, nil
			case FocusServerBtnPing:
				m.pingBroker()
				return m, nil
			case FocusServerBtnClear:
				m.serverLogs = ""
				m.serverVp.SetContent("")
				return m, nil
			case FocusSubBtnStart, FocusSubBtnStop:
				m.toggleSubscribe()
				return m, nil
			case FocusSubBtnBeautify:
				m.beautify(TabSubscribe)
				return m, nil
			case FocusSubBtnClear:
				m.subLogs = ""
				m.subVp.SetContent("")
				return m, nil
			case FocusPubBtnPublish:
				m.publishMessage()
				return m, nil
			case FocusPubBtnBeautify:
				m.beautify(TabPublish)
				return m, nil
			case FocusPubBtnClear:
				m.pubLogs = ""
				m.pubVp.SetContent("")
				return m, nil
			case FocusConfigBtnSave:
				m.saveConfig()
				return m, nil
			case FocusConfigBtnTest:
				m.testConfig()
				return m, nil
			case FocusConfigBtnReload:
				m.reloadConfig()
				return m, nil
			case FocusConfigBtnClear:
				m.configLogs = ""
				m.configVp.SetContent("")
				return m, nil
			}

			// Defaults if not on a button
			if m.tab == TabConnect {
				m.toggleBroker()
				return m, nil
			} else if m.tab == TabSubscribe {
				m.toggleSubscribe()
				return m, nil
			} else if m.tab == TabPublish {
				if m.focusIndex == FocusPubTopic {
					m.setFocus(FocusPubMsg)
					return m, nil
				}
				if msg.String() == "f5" {
					m.publishMessage()
					return m, nil
				}
			} else if m.tab == TabConfig {
				if msg.String() == "f5" {
					m.testConfig()
					return m, nil
				}
			}

		case "ctrl+s":
			if m.tab == TabConfig {
				m.saveConfig()
				return m, nil
			}
		case "ctrl+t":
			if m.tab == TabConfig {
				m.testConfig()
				return m, nil
			}
		case "ctrl+r":
			if m.tab == TabConfig {
				m.reloadConfig()
				return m, nil
			}
		}
	}

	// Forward events to active component
	var cmd tea.Cmd
	switch m.focusIndex {
	case FocusServerHost:
		m.serverHostInput, cmd = m.serverHostInput.Update(msg)
		cmds = append(cmds, cmd)
	case FocusServerPort:
		m.serverPortInput, cmd = m.serverPortInput.Update(msg)
		cmds = append(cmds, cmd)
	case FocusServerLogs:
		m.serverVp, cmd = m.serverVp.Update(msg)
		cmds = append(cmds, cmd)

	case FocusSubTopics:
		m.subTopicsInput, cmd = m.subTopicsInput.Update(msg)
		cmds = append(cmds, cmd)
	case FocusSubPort:
		m.subPortInput, cmd = m.subPortInput.Update(msg)
		cmds = append(cmds, cmd)
	case FocusSubLogs:
		m.subVp, cmd = m.subVp.Update(msg)
		cmds = append(cmds, cmd)

	case FocusPubTopic:
		m.pubTopicInput, cmd = m.pubTopicInput.Update(msg)
		cmds = append(cmds, cmd)
	case FocusPubMsg:
		m.pubMsgInput, cmd = m.pubMsgInput.Update(msg)
		cmds = append(cmds, cmd)
	case FocusPubLogs:
		m.pubVp, cmd = m.pubVp.Update(msg)
		cmds = append(cmds, cmd)

	case FocusConfigEditor:
		m.configEditor, cmd = m.configEditor.Update(msg)
		cmds = append(cmds, cmd)
	case FocusConfigLogs:
		m.configVp, cmd = m.configVp.Update(msg)
		cmds = append(cmds, cmd)
	}

	return m, tea.Batch(cmds...)
}

func (m model) focusName() string {
	switch m.focusIndex {
	case FocusServerLogs, FocusSubLogs, FocusPubLogs, FocusConfigLogs:
		return "Logs"
	default:
		return "Input"
	}
}

func (m model) renderTopBar() string {
	logo := lipgloss.NewStyle().Foreground(colorOrange).Bold(true).Render("  🦟 MQTT : ")
	serverTxt := lipgloss.NewStyle().Foreground(colorGreen).Bold(true).Render("Server")
	pipe := lipgloss.NewStyle().Foreground(lipgloss.Color("#636e72")).Render(" | ")
	subTxt := lipgloss.NewStyle().Foreground(colorBlue).Bold(true).Render("Subscribe")
	pubTxt := lipgloss.NewStyle().Foreground(colorMagenta).Bold(true).Render("Publish")

	title := logo + serverTxt + pipe + subTxt + pipe + pubTxt

	connBtn := styleTabInactive.Render("  Connect  ")
	if m.tab == TabConnect {
		connBtn = styleTabConnect.Render("  Connect  ")
	}

	subBtn := styleTabInactive.Render("  Subscribe  ")
	if m.tab == TabSubscribe {
		subBtn = styleTabSub.Render("  Subscribe  ")
	}

	pubBtn := styleTabInactive.Render("  Publish  ")
	if m.tab == TabPublish {
		pubBtn = styleTabPub.Render("  Publish  ")
	}

	cfgBtn := styleTabInactive.Render("  🎛️  ")
	if m.tab == TabConfig {
		cfgBtn = styleTabCfg.Render("  🎛️  ")
	}

	spaceCount := m.width - lipgloss.Width(title) - lipgloss.Width(connBtn) - lipgloss.Width(subBtn) - lipgloss.Width(pubBtn) - lipgloss.Width(cfgBtn) - 6
	if spaceCount < 2 {
		spaceCount = 2
	}
	spacing := strings.Repeat(" ", spaceCount)

	bar := title + spacing + connBtn + "  " + subBtn + "  " + pubBtn + "  " + cfgBtn + " "
	return styleTopBar.Width(m.width).Render(bar)
}

func (m model) renderBadge(label string, focused bool) string {
	if focused {
		return lipgloss.NewStyle().Background(lipgloss.Color("#ffffff")).Foreground(lipgloss.Color("#000000")).Bold(true).Render(label)
	}
	return styleBadge.Render(label)
}

func (m model) renderBox(content string, focused bool) string {
	boxWidth := m.width - 9
	if boxWidth < 20 {
		boxWidth = 20
	}
	st := styleBoxNormal
	if focused {
		st = styleBoxFocused
	}
	return st.Width(boxWidth).Render(content)
}

func (m model) View() string {
	topBar := m.renderTopBar()

	var body string
	switch m.tab {
	case TabConnect:
		row1 := lipgloss.JoinHorizontal(lipgloss.Top, "  🔗  ", m.renderBox(m.serverHostInput.View(), m.focusIndex == FocusServerHost))
		row2 := lipgloss.JoinHorizontal(lipgloss.Top, "  🏗️  ", m.renderBox(m.serverPortInput.View(), m.focusIndex == FocusServerPort))

		statusBadge := lipgloss.NewStyle().Foreground(lipgloss.Color("#a4b0be")).Render("● Broker: Idle/External")
		actBtnText := " ▶ Start Mosquitto "
		if m.serverRunning {
			statusBadge = lipgloss.NewStyle().Foreground(colorGreen).Bold(true).Render("● Broker: Running")
			actBtnText = " ⏹ Stop Broker "
		}

		hdr := styleLogHdr.Render("  📝 Logs") + "      " + statusBadge + "   " +
			m.renderBadge(actBtnText, m.focusIndex == FocusServerBtnStart || m.focusIndex == FocusServerBtnStop) + "  " +
			m.renderBadge(" ⚡ Check/Ping ", m.focusIndex == FocusServerBtnPing) + "  " +
			m.renderBadge(" 🧹 Clear ", m.focusIndex == FocusServerBtnClear)
		hdrLine := styleLogHdr.Width(m.width).Render(hdr)

		vpBox := styleBoxNormal.Width(m.width - 2).Height(m.serverVp.Height).Render(m.serverVp.View())
		body = lipgloss.JoinVertical(lipgloss.Left, "", row1, row2, "", hdrLine, vpBox)

	case TabSubscribe:
		row1 := lipgloss.JoinHorizontal(lipgloss.Top, "  💬  ", m.renderBox(m.subTopicsInput.View(), m.focusIndex == FocusSubTopics))
		row2 := lipgloss.JoinHorizontal(lipgloss.Top, "  🏗️  ", m.renderBox(m.subPortInput.View(), m.focusIndex == FocusSubPort))

		statusBadge := lipgloss.NewStyle().Foreground(lipgloss.Color("#a4b0be")).Render("● Inactive")
		actBtnText := " ▶ Start Subscribe "
		if m.subRunning {
			statusBadge = lipgloss.NewStyle().Foreground(colorGreen).Bold(true).Render("● Subscribing (Active)")
			actBtnText = " ⏹ Stop Subscribe "
		}

		hdr := styleLogHdr.Render("  📝 Logs") + "      " + statusBadge + "   " +
			m.renderBadge(actBtnText, m.focusIndex == FocusSubBtnStart || m.focusIndex == FocusSubBtnStop) + "  " +
			m.renderBadge(" 💋 Beautify JSON ", m.focusIndex == FocusSubBtnBeautify) + "  " +
			m.renderBadge(" 🧹 Clear ", m.focusIndex == FocusSubBtnClear)
		hdrLine := styleLogHdr.Width(m.width).Render(hdr)

		vpBox := styleBoxNormal.Width(m.width - 2).Height(m.subVp.Height).Render(m.subVp.View())
		body = lipgloss.JoinVertical(lipgloss.Left, "", row1, row2, "", hdrLine, vpBox)

	case TabPublish:
		row1 := lipgloss.JoinHorizontal(lipgloss.Top, "  💬  ", m.renderBox(m.pubTopicInput.View(), m.focusIndex == FocusPubTopic))
		row2 := lipgloss.JoinHorizontal(lipgloss.Top, "  💬  ", m.renderBox(m.pubMsgInput.View(), m.focusIndex == FocusPubMsg))

		hdr := styleLogHdr.Render("  📝 Logs") + "      " +
			m.renderBadge(" 🚀 Publish Now ", m.focusIndex == FocusPubBtnPublish) + "  " +
			m.renderBadge(" 💋 Beautify JSON ", m.focusIndex == FocusPubBtnBeautify) + "  " +
			m.renderBadge(" 🧹 Clear Logs ", m.focusIndex == FocusPubBtnClear)
		hdrLine := styleLogHdr.Width(m.width).Render(hdr)

		vpBox := styleBoxNormal.Width(m.width - 2).Height(m.pubVp.Height).Render(m.pubVp.View())
		body = lipgloss.JoinVertical(lipgloss.Left, "", row1, row2, "", hdrLine, vpBox)

	case TabConfig:
		toolbar := "  🎛️  " + lipgloss.NewStyle().Foreground(lipgloss.Color("#c8d6e5")).Render(fmt.Sprintf("Config: %s", m.configPath)) + "    " +
			m.renderBadge(" 💾 Save Config ", m.focusIndex == FocusConfigBtnSave) + "  " +
			m.renderBadge(" 🧪 Test Config ", m.focusIndex == FocusConfigBtnTest) + "  " +
			m.renderBadge(" 🔄 Reload ", m.focusIndex == FocusConfigBtnReload)
		tbLine := styleTopBar.Width(m.width).Render(toolbar)

		editorBox := styleBoxNormal.Width(m.width - 2).Render(m.configEditor.View())

		hdr := styleLogHdr.Render("  📝 Validation & Status Logs") + "      " +
			m.renderBadge(" 🧹 Clear ", m.focusIndex == FocusConfigBtnClear)
		hdrLine := styleLogHdr.Width(m.width).Render(hdr)

		vpBox := styleBoxNormal.Width(m.width - 2).Height(m.configVp.Height).Render(m.configVp.View())
		body = lipgloss.JoinVertical(lipgloss.Left, "", tbLine, "", editorBox, "", hdrLine, vpBox)
	}

	bottomBar := lipgloss.NewStyle().Background(lipgloss.Color("#14171d")).Foreground(lipgloss.Color("#8892b0")).Width(m.width).Render(
		" F1/1 Connect   F2/2 Subscribe   F3/3 Publish   F4/4 Config  |  Tab Next  Enter/F5 Action  Ctrl+L Clear  Ctrl+Q Quit",
	)

	return lipgloss.JoinVertical(lipgloss.Left, topBar, body, bottomBar)
}

func main() {
	if len(os.Args) > 1 && (os.Args[1] == "--version" || os.Args[1] == "-v") {
		fmt.Println("Mosquitto MQTT TUI (Go Edition) v1.0.0")
		return
	}

	for _, exe := range []string{"mosquitto", "mosquitto_sub", "mosquitto_pub"} {
		if _, err := exec.LookPath(exe); err != nil {
			fmt.Fprintf(os.Stderr, "Warning: '%s' not found in PATH.\n", exe)
		}
	}

	p := tea.NewProgram(
		initialModel(),
		tea.WithAltScreen(),
	)

	if _, err := p.Run(); err != nil {
		fmt.Printf("Error running program: %v\n", err)
		os.Exit(1)
	}
}
