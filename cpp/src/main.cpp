#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>

#include "history.hpp"
#include "beautify.hpp"

using namespace ftxui;

static inline std::string trim_str(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string get_current_time_str() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    return ss.str();
}

int main() {
    auto screen = ScreenInteractive::Fullscreen();

    // State
    int active_tab = 0; // 0: Connect, 1: Subscribe, 2: Publish, 3: Config

    // Connect Tab inputs
    std::string host_val = "127.0.0.1";
    std::string port_val = "1883";
    int host_hist_idx = -1;
    int port_hist_idx = -1;
    Component raw_inp_host;
    Component raw_inp_port;

    // Subscribe Tab inputs
    std::string sub_topics_val = "#";
    std::string sub_port_val = "1883";
    int sub_topics_hist_idx = -1;
    int sub_port_hist_idx = -1;
    Component raw_inp_sub_topics;
    Component raw_inp_sub_port;

    // Publish Tab inputs
    std::string pub_topic_val = "test/topic";
    std::string pub_msg_val = "{\"timestamp\": 1793124973.167886, \"numeric\": 15, \"value\": 15}";
    int pub_topic_hist_idx = -1;
    int pub_msg_hist_idx = -1;
    Component raw_inp_pub_topic;
    Component raw_inp_pub_msg;

    // Config Tab content
    std::string config_val = "";
    Component inp_config;
    {
        std::ifstream cfg_in(mqtt_tui::get_config_file_path());
        if (cfg_in.is_open()) {
            std::stringstream ss;
            ss << cfg_in.rdbuf();
            config_val = ss.str();
        } else {
            config_val = "# Mosquitto TUI Configuration\nlistener 1883\nallow_anonymous true\n\nlistener 9001\nprotocol websockets\n";
        }
    }

    // Logs
    std::mutex log_mutex;
    std::vector<std::string> log_lines = {
        "[" + get_current_time_str() + "] Mosquitto TUI initialized (C++20 / FTXUI). Ready."
    };
    int log_scroll_line = -1; // -1 means auto-scroll to the bottom!

    auto append_log = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_lines.push_back(msg);
        screen.PostEvent(Event::Custom);
    };

    auto clear_logs = [&]() {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_lines.clear();
        log_scroll_line = -1;
        screen.PostEvent(Event::Custom);
    };

    auto do_beautify_logs = [&]() {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ostringstream oss;
        for (const auto& l : log_lines) {
            oss << l << "\n";
        }
        std::string beautified = beautify_json(oss.str());
        std::stringstream ss(beautified);
        log_lines.clear();
        std::string bline;
        while (std::getline(ss, bline)) {
            if (!bline.empty() && bline.back() == '\r') bline.pop_back();
            log_lines.push_back(bline);
        }
        log_scroll_line = -1;
        screen.PostEvent(Event::Custom);
    };

    // Subprocess tracking
    std::atomic<pid_t> broker_pid{-1};
    std::atomic<pid_t> sub_pid{-1};
    std::string broker_status = "Idle/External";
    Color broker_status_color = Color::RGB(180, 180, 180);

    auto update_broker_status = [&](const std::string& st, Color col) {
        broker_status = st;
        broker_status_color = col;
        screen.PostEvent(Event::Custom);
    };

    // Helper: Badge button options
    auto make_badge_btn_option = [](Color bg_col = Color::RGB(128, 128, 128), Color fg_col = Color::RGB(28, 28, 28)) {
        ButtonOption opt;
        opt.transform = [bg_col, fg_col](const EntryState& s) {
            auto elem = text(" " + s.label + " ");
            if (s.focused) {
                return elem | bgcolor(Color::RGB(210, 210, 210)) | color(Color::Black) | bold;
            }
            return elem | bgcolor(bg_col) | color(fg_col) | bold;
        };
        return opt;
    };

    // Helper: Tab button options (matching rounded border & height 3)
    auto make_tab_btn = [&](const std::string& label, int tab_id, Color active_col, std::function<void()> on_select = nullptr) {
        ButtonOption opt;
        opt.transform = [&, label, tab_id, active_col](const EntryState& s) {
            bool is_active = (active_tab == tab_id);
            auto elem = text(" " + label + " ");
            if (is_active) {
                return elem | bold | color(Color::Black) | borderRounded | bgcolor(active_col) | color(Color::Black);
            }
            if (s.focused) {
                return elem | bold | color(active_col) | borderRounded | color(active_col);
            }
            return elem | color(Color::RGB(160, 160, 160)) | borderRounded | color(Color::RGB(80, 80, 80));
        };
        return Button(label, [&, tab_id, on_select] {
            active_tab = tab_id;
            if (on_select) on_select();
        }, opt);
    };

    auto tab_connect_btn = make_tab_btn("Connect", 0, Color::RGB(46, 213, 115), [&]{
        if (raw_inp_host) raw_inp_host->TakeFocus();
    });
    auto tab_sub_btn = make_tab_btn("Subscribe", 1, Color::RGB(84, 160, 255), [&]{
        if (raw_inp_sub_topics) raw_inp_sub_topics->TakeFocus();
    });
    auto tab_pub_btn = make_tab_btn("Publish", 2, Color::RGB(224, 86, 253), [&]{
        if (raw_inp_pub_topic) raw_inp_pub_topic->TakeFocus();
    });
    auto tab_cfg_btn = make_tab_btn("🎛️", 3, Color::RGB(255, 165, 2), [&]{
        if (inp_config) inp_config->TakeFocus();
    });

    // Hover state tracking for input borders
    bool hover_host = false;
    bool hover_port = false;
    bool hover_sub_topics = false;
    bool hover_sub_port = false;
    bool hover_pub_topic = false;
    bool hover_pub_msg = false;
    bool hover_cfg = false;

    // Inputs with history handler decorator
    auto make_history_input = [&](std::string& val_ref, const std::string& placeholder, const std::string& field_name, int& hist_idx, Component& raw_out, bool& hover_ref) {
        InputOption opt;
        opt.placeholder = placeholder;
        opt.transform = [&hover_ref](InputState state) {
            hover_ref = state.hovered;
            if (state.is_placeholder) {
                state.element |= color(Color::RGB(120, 120, 120));
            } else {
                state.element |= color(Color::RGB(230, 230, 230));
            }
            return state.element;
        };
        auto inp = Input(&val_ref, opt);
        raw_out = inp;
        return CatchEvent(inp, [&, field_name](Event event) {
            if (event == Event::ArrowUp) {
                auto hist = mqtt_tui::load_history(field_name);
                if (!hist.empty()) {
                    if (hist_idx == -1) {
                        hist_idx = (int)hist.size() - 1;
                    } else if (hist_idx > 0) {
                        hist_idx--;
                    }
                    val_ref = hist[hist_idx];
                    return true;
                }
            } else if (event == Event::ArrowDown) {
                auto hist = mqtt_tui::load_history(field_name);
                if (!hist.empty() && hist_idx != -1) {
                    if (hist_idx < (int)hist.size() - 1) {
                        hist_idx++;
                        val_ref = hist[hist_idx];
                    } else {
                        hist_idx = -1;
                        val_ref = "";
                    }
                    return true;
                }
            }
            return false;
        });
    };

    // Connect Tab components
    auto inp_host = make_history_input(host_val, "Host Address (DEFAULT = 127.0.0.1)", "host", host_hist_idx, raw_inp_host, hover_host);
    auto inp_port = make_history_input(port_val, "Port (DEFAULT = 1883|9001)", "port", port_hist_idx, raw_inp_port, hover_port);

    auto btn_start_broker = Button("▶ Start Mosquitto", [&] {
        if (broker_pid != -1) {
            append_log("[" + get_current_time_str() + "] Broker already running (PID: " + std::to_string(broker_pid) + ")");
            return;
        }
        int pipe_fd[2];
        if (pipe(pipe_fd) == -1) {
            append_log("[" + get_current_time_str() + "] Failed to create pipe");
            return;
        }

        pid_t pid = fork();
        if (pid == 0) { // Child
            close(pipe_fd[0]);
            dup2(pipe_fd[1], STDOUT_FILENO);
            dup2(pipe_fd[1], STDERR_FILENO);
            close(pipe_fd[1]);

            execlp("mosquitto", "mosquitto", "-v", nullptr);
            _exit(127);
        } else if (pid > 0) { // Parent
            close(pipe_fd[1]);
            broker_pid = pid;
            update_broker_status("Running (PID " + std::to_string(pid) + ")", Color::RGB(46, 213, 115));
            append_log("[" + get_current_time_str() + "] Started mosquitto broker (PID: " + std::to_string(pid) + ")");

            std::thread([pipe_fd, pid, &broker_pid, &append_log, &update_broker_status]() {
                FILE* f = fdopen(pipe_fd[0], "r");
                if (f) {
                    char buf[1024];
                    while (fgets(buf, sizeof(buf), f)) {
                        std::string line(buf);
                        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                        if (!line.empty()) {
                            append_log("[" + get_current_time_str() + "] [mosquitto] " + line);
                        }
                    }
                    fclose(f);
                }
                int status;
                waitpid(pid, &status, 0);
                broker_pid = -1;
                update_broker_status("Stopped", Color::RGB(255, 71, 87));
                append_log("[" + get_current_time_str() + "] Mosquitto broker process exited.");
            }).detach();
        }
    }, make_badge_btn_option());

    auto btn_stop_broker = Button("⏹ Stop Mosquitto", [&] {
        if (broker_pid != -1) {
            pid_t pid = broker_pid;
            kill(pid, SIGTERM);
            append_log("[" + get_current_time_str() + "] Sent SIGTERM to broker (PID: " + std::to_string(pid) + ")");
        } else {
            append_log("[" + get_current_time_str() + "] No broker process managed by this TUI to stop.");
        }
    }, make_badge_btn_option());

    auto btn_ping = Button("⚡ Check/Ping", [&] {
        std::string h = trim_str(host_val);
        if (h.empty()) h = "127.0.0.1";
        int p = 1883;
        try {
            std::string p_trimmed = trim_str(port_val);
            if (!p_trimmed.empty()) p = std::stoi(p_trimmed);
        } catch (...) {}

        mqtt_tui::save_history("host", h);
        mqtt_tui::save_history("port", std::to_string(p));

        std::thread([h, p, &append_log, &update_broker_status]() {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                append_log("[" + get_current_time_str() + "] Failed to create socket for ping");
                return;
            }
            struct sockaddr_in serv_addr;
            std::memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(p);

            struct timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

            if (inet_pton(AF_INET, h.c_str(), &serv_addr.sin_addr) <= 0) {
                close(sock);
                append_log("[" + get_current_time_str() + "] Invalid IP address: " + h);
                return;
            }

            if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0) {
                close(sock);
                update_broker_status("Connected (" + h + ":" + std::to_string(p) + ")", Color::RGB(46, 213, 115));
                append_log("[" + get_current_time_str() + "] Mosquitto ping SUCCESS: broker is accepting connections on " + h + ":" + std::to_string(p));
            } else {
                close(sock);
                update_broker_status("Offline/Unreachable", Color::RGB(255, 71, 87));
                append_log("[" + get_current_time_str() + "] Mosquitto ping FAILED: cannot connect to " + h + ":" + std::to_string(p));
            }
        }).detach();
    }, make_badge_btn_option());

    auto btn_connect_clear = Button("🧹 Clear", clear_logs, make_badge_btn_option());

    // Subscribe Tab components
    auto inp_sub_topics = make_history_input(sub_topics_val, "Topics (Comma Separated, DEFAULT = #)", "sub_topics", sub_topics_hist_idx, raw_inp_sub_topics, hover_sub_topics);
    auto inp_sub_port = make_history_input(sub_port_val, "Port (DEFAULT = 1883|9001)", "sub_port", sub_port_hist_idx, raw_inp_sub_port, hover_sub_port);

    auto start_subscriber_func = [&] {
        if (sub_pid != -1) {
            append_log("[" + get_current_time_str() + "] Subscriber already active (PID " + std::to_string(sub_pid) + ")");
            return;
        }

        std::string raw_topics = sub_topics_val.empty() ? "#" : sub_topics_val;
        std::string p_str = sub_port_val.empty() ? "1883" : sub_port_val;
        std::string h_str = host_val.empty() ? "127.0.0.1" : host_val;

        mqtt_tui::save_history("sub_topics", raw_topics);
        mqtt_tui::save_history("sub_port", p_str);

        // Parse comma separated topics
        std::vector<std::string> topics;
        std::stringstream ss(raw_topics);
        std::string item;
        while (std::getline(ss, item, ',')) {
            size_t start = item.find_first_not_of(" \t");
            if (start != std::string::npos) {
                size_t end = item.find_last_not_of(" \t");
                topics.push_back(item.substr(start, end - start + 1));
            }
        }
        if (topics.empty()) topics.push_back("#");

        int pipe_fd[2];
        if (pipe(pipe_fd) == -1) {
            append_log("[" + get_current_time_str() + "] Failed to create subscriber pipe");
            return;
        }

        pid_t pid = fork();
        if (pid == 0) { // Child
            close(pipe_fd[0]);
            dup2(pipe_fd[1], STDOUT_FILENO);
            dup2(pipe_fd[1], STDERR_FILENO);
            close(pipe_fd[1]);

            std::vector<const char*> args;
            args.push_back("mosquitto_sub");
            args.push_back("-h");
            args.push_back(h_str.c_str());
            args.push_back("-p");
            args.push_back(p_str.c_str());
            args.push_back("-v");
            for (const auto& t : topics) {
                args.push_back("-t");
                args.push_back(t.c_str());
            }
            args.push_back(nullptr);

            execvp("mosquitto_sub", const_cast<char* const*>(args.data()));
            _exit(127);
        } else if (pid > 0) {
            close(pipe_fd[1]);
            sub_pid = pid;
            append_log("[" + get_current_time_str() + "] Subscribed to [" + raw_topics + "] on " + h_str + ":" + p_str + " (PID " + std::to_string(pid) + ")");
            screen.PostEvent(Event::Custom);

            std::thread([pipe_fd, pid, &sub_pid, &append_log, &screen]() {
                FILE* f = fdopen(pipe_fd[0], "r");
                if (f) {
                    char buf[4096];
                    while (fgets(buf, sizeof(buf), f)) {
                        std::string line(buf);
                        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                        if (!line.empty()) {
                            append_log("[" + get_current_time_str() + "] " + line);
                        }
                    }
                    fclose(f);
                }
                int status;
                waitpid(pid, &status, 0);
                sub_pid = -1;
                append_log("[" + get_current_time_str() + "] mosquitto_sub exited.");
                screen.PostEvent(Event::Custom);
            }).detach();
        }
    };

    auto stop_subscriber_func = [&] {
        if (sub_pid != -1) {
            pid_t pid = sub_pid;
            kill(pid, SIGTERM);
            append_log("[" + get_current_time_str() + "] Sent SIGTERM to subscriber (PID " + std::to_string(pid) + ")");
            sub_pid = -1;
            screen.PostEvent(Event::Custom);
        } else {
            append_log("[" + get_current_time_str() + "] No subscriber process active.");
        }
    };

    ButtonOption opt_start_sub;
    opt_start_sub.transform = [&](const EntryState& s) {
        bool is_running = (sub_pid != -1);
        std::string lbl = is_running ? " ● Subscribing " : " ▶ Start Sub ";
        auto elem = text(lbl);
        if (is_running) {
            return elem | bgcolor(Color::RGB(46, 213, 115)) | color(Color::Black) | bold;
        }
        if (s.focused) {
            return elem | bgcolor(Color::RGB(210, 210, 210)) | color(Color::Black) | bold;
        }
        return elem | bgcolor(Color::RGB(128, 128, 128)) | color(Color::RGB(28, 28, 28)) | bold;
    };
    auto btn_start_sub = Button("▶ Start Sub", start_subscriber_func, opt_start_sub);

    ButtonOption opt_stop_sub;
    opt_stop_sub.transform = [&](const EntryState& s) {
        bool is_running = (sub_pid != -1);
        auto elem = text(" ⏹ Stop Sub ");
        if (is_running) {
            if (s.focused) {
                return elem | bgcolor(Color::RGB(255, 100, 100)) | color(Color::Black) | bold;
            }
            return elem | bgcolor(Color::RGB(230, 80, 80)) | color(Color::White) | bold;
        }
        return elem | bgcolor(Color::RGB(80, 80, 80)) | color(Color::RGB(150, 150, 150));
    };
    auto btn_stop_sub = Button("⏹ Stop Sub", stop_subscriber_func, opt_stop_sub);
    auto btn_sub_beautify = Button("💋 Beautify JSON", do_beautify_logs, make_badge_btn_option());
    auto btn_sub_clear = Button("🧹 Clear", clear_logs, make_badge_btn_option());

    // Publish Tab components
    auto inp_pub_topic = make_history_input(pub_topic_val, "Topic (DEFAULT = test/topic)", "pub_topic", pub_topic_hist_idx, raw_inp_pub_topic, hover_pub_topic);
    auto inp_pub_msg = make_history_input(pub_msg_val, "Message", "pub_message", pub_msg_hist_idx, raw_inp_pub_msg, hover_pub_msg);

    auto btn_publish = Button("🚀 Publish Now", [&] {
        std::string t = pub_topic_val.empty() ? "test/topic" : pub_topic_val;
        std::string m = pub_msg_val;
        std::string h = host_val.empty() ? "127.0.0.1" : host_val;
        std::string p = port_val.empty() ? "1883" : port_val;

        mqtt_tui::save_history("pub_topic", t);
        mqtt_tui::save_history("pub_message", m);

        std::thread([h, p, t, m, &append_log]() {
            std::string cmd = "mosquitto_pub -h " + h + " -p " + p + " -t '" + t + "' -m '" + m + "'";
            int res = std::system(cmd.c_str());
            if (res == 0) {
                append_log("[" + get_current_time_str() + "] Published to " + t + " (" + std::to_string(m.size()) + " bytes)");
            } else {
                append_log("[" + get_current_time_str() + "] Publish error (exit code: " + std::to_string(res) + ")");
            }
        }).detach();
    }, make_badge_btn_option());

    auto btn_pub_beautify = Button("💋 Beautify JSON", do_beautify_logs, make_badge_btn_option());
    auto btn_pub_clear = Button("🧹 Clear", clear_logs, make_badge_btn_option());

    // Config Tab components
    InputOption cfg_opt;
    cfg_opt.multiline = true;
    cfg_opt.transform = [&](InputState state) {
        hover_cfg = state.hovered;
        state.element |= color(Color::RGB(230, 230, 230));
        return state.element;
    };
    inp_config = Input(&config_val, cfg_opt);

    auto btn_save_cfg = Button("💾 Save Config", [&] {
        std::ofstream cfg_out(mqtt_tui::get_config_file_path());
        if (cfg_out.is_open()) {
            cfg_out << config_val;
            append_log("[" + get_current_time_str() + "] Saved configuration to " + mqtt_tui::get_config_file_path());
        } else {
            append_log("[" + get_current_time_str() + "] Error saving config to " + mqtt_tui::get_config_file_path());
        }
    }, make_badge_btn_option());

    auto btn_reload_cfg = Button("🔄 Reload", [&] {
        std::ifstream cfg_in(mqtt_tui::get_config_file_path());
        if (cfg_in.is_open()) {
            std::stringstream ss;
            ss << cfg_in.rdbuf();
            config_val = ss.str();
            append_log("[" + get_current_time_str() + "] Reloaded configuration from " + mqtt_tui::get_config_file_path());
        } else {
            append_log("[" + get_current_time_str() + "] Config file not found at " + mqtt_tui::get_config_file_path());
        }
    }, make_badge_btn_option());

    auto btn_cfg_clear = Button("🧹 Clear", clear_logs, make_badge_btn_option());

    // Separate Containers per tab (Zero sharing of button components!)
    auto tab_connect_container = Container::Vertical({
        inp_host,
        inp_port,
        Container::Horizontal({
            btn_start_broker,
            btn_stop_broker,
            btn_ping,
            btn_connect_clear
        })
    });

    auto tab_sub_container = Container::Vertical({
        inp_sub_topics,
        inp_sub_port,
        Container::Horizontal({
            btn_start_sub,
            btn_stop_sub,
            btn_sub_beautify,
            btn_sub_clear
        })
    });

    auto tab_pub_container = Container::Vertical({
        inp_pub_topic,
        inp_pub_msg,
        Container::Horizontal({
            btn_publish,
            btn_pub_beautify,
            btn_pub_clear
        })
    });

    auto tab_cfg_container = Container::Vertical({
        inp_config,
        Container::Horizontal({
            btn_save_cfg,
            btn_reload_cfg,
            btn_cfg_clear
        })
    });

    auto tab_content_container = Container::Tab({
        tab_connect_container,
        tab_sub_container,
        tab_pub_container,
        tab_cfg_container
    }, &active_tab);

    auto main_container = Container::Vertical({
        Container::Horizontal({
            tab_connect_btn,
            tab_sub_btn,
            tab_pub_btn,
            tab_cfg_btn
        }),
        tab_content_container
    });

    // Custom CatchEvent for global shortcuts and Tab navigation
    auto get_focus_chain = [&]() -> std::vector<Component> {
        if (active_tab == 0) {
            return { raw_inp_host, raw_inp_port, btn_start_broker, btn_stop_broker, btn_ping, btn_connect_clear, tab_connect_btn, tab_sub_btn, tab_pub_btn, tab_cfg_btn };
        } else if (active_tab == 1) {
            return { raw_inp_sub_topics, raw_inp_sub_port, btn_start_sub, btn_stop_sub, btn_sub_beautify, btn_sub_clear, tab_connect_btn, tab_sub_btn, tab_pub_btn, tab_cfg_btn };
        } else if (active_tab == 2) {
            return { raw_inp_pub_topic, raw_inp_pub_msg, btn_publish, btn_pub_beautify, btn_pub_clear, tab_connect_btn, tab_sub_btn, tab_pub_btn, tab_cfg_btn };
        } else {
            return { inp_config, btn_save_cfg, btn_reload_cfg, btn_cfg_clear, tab_connect_btn, tab_sub_btn, tab_pub_btn, tab_cfg_btn };
        }
    };

    auto root_component = CatchEvent(main_container, [&](Event event) {
        if (event == Event::Tab) {
            auto chain = get_focus_chain();
            int current_idx = -1;
            for (int i = 0; i < (int)chain.size(); ++i) {
                if (chain[i]->Focused()) {
                    current_idx = i;
                    break;
                }
            }
            int next_idx = (current_idx + 1) % chain.size();
            chain[next_idx]->TakeFocus();
            return true;
        }
        if (event == Event::TabReverse) {
            auto chain = get_focus_chain();
            int current_idx = 0;
            for (int i = 0; i < (int)chain.size(); ++i) {
                if (chain[i]->Focused()) {
                    current_idx = i;
                    break;
                }
            }
            int prev_idx = (current_idx - 1 + (int)chain.size()) % chain.size();
            chain[prev_idx]->TakeFocus();
            return true;
        }
        // Mouse wheel scrolling
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                if (active_tab == 3 && inp_config && event.mouse().y < screen.dimy() - 7) {
                    for (int k = 0; k < 3; ++k) inp_config->OnEvent(Event::ArrowUp);
                    return true;
                }
                std::lock_guard<std::mutex> lock(log_mutex);
                int total = (int)log_lines.size();
                if (total > 0) {
                    if (log_scroll_line == -1) {
                        log_scroll_line = std::max(0, total - 4);
                    } else {
                        log_scroll_line = std::max(0, log_scroll_line - 3);
                    }
                    return true;
                }
            }
            if (event.mouse().button == Mouse::WheelDown) {
                if (active_tab == 3 && inp_config && event.mouse().y < screen.dimy() - 7) {
                    for (int k = 0; k < 3; ++k) inp_config->OnEvent(Event::ArrowDown);
                    return true;
                }
                std::lock_guard<std::mutex> lock(log_mutex);
                int total = (int)log_lines.size();
                if (total > 0 && log_scroll_line != -1) {
                    log_scroll_line += 3;
                    if (log_scroll_line >= total - 1) {
                        log_scroll_line = -1; // resume auto-scroll at bottom
                    }
                    return true;
                }
            }
        }
        // Keyboard scrolling
        if (active_tab == 3 && inp_config && inp_config->Focused()) {
            if (event == Event::PageUp) {
                for (int k = 0; k < 15; ++k) inp_config->OnEvent(Event::ArrowUp);
                return true;
            }
            if (event == Event::PageDown) {
                for (int k = 0; k < 15; ++k) inp_config->OnEvent(Event::ArrowDown);
                return true;
            }
            if (event == Event::Home || event == Event::End) {
                return false; // let inp_config handle Home and End
            }
        } else {
            if (event == Event::PageUp) {
                std::lock_guard<std::mutex> lock(log_mutex);
                int total = (int)log_lines.size();
                if (total > 0) {
                    if (log_scroll_line == -1) {
                        log_scroll_line = std::max(0, total - 15);
                    } else {
                        log_scroll_line = std::max(0, log_scroll_line - 15);
                    }
                    return true;
                }
            }
            if (event == Event::PageDown) {
                std::lock_guard<std::mutex> lock(log_mutex);
                int total = (int)log_lines.size();
                if (total > 0 && log_scroll_line != -1) {
                    log_scroll_line += 15;
                    if (log_scroll_line >= total - 1) {
                        log_scroll_line = -1; // resume auto-scroll
                    }
                    return true;
                }
            }
            if (event == Event::Home) {
                std::lock_guard<std::mutex> lock(log_mutex);
                if (!log_lines.empty()) {
                    log_scroll_line = 0;
                    return true;
                }
            }
            if (event == Event::End) {
                log_scroll_line = -1;
                return true;
            }
        }
        if (event == Event::Special({13}) || event == Event::Character('\r') ||
            event == Event::F5 || event == Event::Special("\x1b[15~")) {
            return main_container->OnEvent(Event::Return);
        }
        bool in_input = (inp_config && inp_config->Focused()) ||
                        (raw_inp_host && raw_inp_host->Focused()) ||
                        (raw_inp_port && raw_inp_port->Focused()) ||
                        (raw_inp_sub_topics && raw_inp_sub_topics->Focused()) ||
                        (raw_inp_sub_port && raw_inp_sub_port->Focused()) ||
                        (raw_inp_pub_topic && raw_inp_pub_topic->Focused()) ||
                        (raw_inp_pub_msg && raw_inp_pub_msg->Focused());

        if (event == Event::F1 || event == Event::Special("\x1bOP") || event == Event::Special("\x1b[11~") || event == Event::Special("\0331") ||
            (!in_input && event == Event::Character('1'))) {
            active_tab = 0;
            if (raw_inp_host) raw_inp_host->TakeFocus();
            return true;
        }
        if (event == Event::F2 || event == Event::Special("\x1bOQ") || event == Event::Special("\x1b[12~") || event == Event::Special("\0332") ||
            (!in_input && event == Event::Character('2'))) {
            active_tab = 1;
            if (raw_inp_sub_topics) raw_inp_sub_topics->TakeFocus();
            return true;
        }
        if (event == Event::F3 || event == Event::Special("\x1bOR") || event == Event::Special("\x1b[13~") || event == Event::Special("\0333") ||
            (!in_input && event == Event::Character('3'))) {
            active_tab = 2;
            if (raw_inp_pub_topic) raw_inp_pub_topic->TakeFocus();
            return true;
        }
        if (event == Event::F4 || event == Event::Special("\x1bOS") || event == Event::Special("\x1b[14~") || event == Event::Special("\0334") ||
            (!in_input && event == Event::Character('4'))) {
            active_tab = 3;
            if (inp_config) inp_config->TakeFocus();
            return true;
        }
        if (event == Event::Special({17})) { // Ctrl+Q
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Special({12})) { // Ctrl+L
            clear_logs();
            return true;
        }
        return false;
    });

    // Renderer
    auto renderer = Renderer(root_component, [&] {
        // Top Header
        auto header = hbox({
            vbox({
                text(" "),
                hbox({
                    text(" 🦟 MQTT : ") | color(Color::RGB(255, 177, 66)) | bold,
                    text("Server ") | color(Color::RGB(46, 213, 115)) | bold,
                    text("| ") | color(Color::RGB(100, 100, 100)),
                    text("Subscribe ") | color(Color::RGB(84, 160, 255)) | bold,
                    text("| ") | color(Color::RGB(100, 100, 100)),
                    text("Publish   ") | color(Color::RGB(224, 86, 253)) | bold,
                }),
                text(" ")
            }),
            filler(),
            tab_connect_btn->Render(),
            separatorEmpty(),
            tab_sub_btn->Render(),
            separatorEmpty(),
            tab_pub_btn->Render(),
            separatorEmpty(),
            tab_cfg_btn->Render(),
            text(" ")
        });

        // Helper to produce a clean, perfectly aligned labeled input row with dynamic border brightness
        auto make_labeled_box = [](const std::string& icon, Component input_comp, bool is_hovered, int h = 0) {
            Color border_col;
            if (input_comp->Focused()) {
                border_col = Color::RGB(255, 255, 255); // 100% white when active
            } else if (is_hovered) {
                border_col = Color::RGB(153, 153, 153); // 60% white when hovered
            } else {
                border_col = Color::RGB(102, 102, 102); // 40% white when normal
            }

            Element left_col;
            if (h > 0) {
                left_col = vbox({
                    text(" "),
                    text("  " + icon + "  ") | bold,
                    filler()
                }) | size(WIDTH, EQUAL, 6);
                return hbox({
                    left_col,
                    input_comp->Render() | borderRounded | color(border_col) | size(HEIGHT, EQUAL, h) | flex
                });
            } else if (h < 0) {
                left_col = vbox({
                    text(" "),
                    text("  " + icon + "  ") | bold,
                    filler()
                }) | size(WIDTH, EQUAL, 6);
                return hbox({
                    left_col,
                    input_comp->Render() | borderRounded | color(border_col) | flex
                }) | flex;
            } else {
                left_col = vbox({
                    text(" "),
                    text("  " + icon + "  ") | bold,
                    text(" ")
                }) | size(WIDTH, EQUAL, 6);
                return hbox({
                    left_col,
                    input_comp->Render() | borderRounded | color(border_col) | flex
                });
            }
        };

        // Tab Content
        Elements tab_elements;
        if (active_tab == 0) {
            tab_elements.push_back(make_labeled_box("🔗", inp_host, hover_host));
            tab_elements.push_back(make_labeled_box("🏗️", inp_port, hover_port));
        } else if (active_tab == 1) {
            tab_elements.push_back(make_labeled_box("💬", inp_sub_topics, hover_sub_topics));
            tab_elements.push_back(make_labeled_box("🏗️", inp_sub_port, hover_sub_port));
        } else if (active_tab == 2) {
            tab_elements.push_back(make_labeled_box("💬", inp_pub_topic, hover_pub_topic));
            tab_elements.push_back(make_labeled_box("💬", inp_pub_msg, hover_pub_msg, 5));
        } else if (active_tab == 3) {
            // In Config tab, config editor expands (flex) to fill available space
            tab_elements.push_back(make_labeled_box("🎛️", inp_config, hover_cfg, -1));
        }

        // Logs Header Bar
        Elements log_action_btns;
        std::string status_label = "";
        Color status_color = Color::White;

        if (active_tab == 0) {
            status_label = "● Broker: " + broker_status;
            status_color = broker_status_color;
            log_action_btns = {
                btn_start_broker->Render(),
                separatorEmpty(),
                btn_stop_broker->Render(),
                separatorEmpty(),
                btn_ping->Render(),
                separatorEmpty(),
                btn_connect_clear->Render()
            };
        } else if (active_tab == 1) {
            if (sub_pid != -1) {
                status_label = "● Subscriber: Active (PID " + std::to_string(sub_pid) + ")";
                status_color = Color::RGB(46, 213, 115);
            } else {
                status_label = "● Subscriber: Idle";
                status_color = Color::RGB(180, 180, 180);
            }
            log_action_btns = {
                btn_start_sub->Render(),
                separatorEmpty(),
                btn_stop_sub->Render(),
                separatorEmpty(),
                btn_sub_beautify->Render(),
                separatorEmpty(),
                btn_sub_clear->Render()
            };
        } else if (active_tab == 2) {
            status_label = "● Broker: " + broker_status;
            status_color = broker_status_color;
            log_action_btns = {
                btn_publish->Render(),
                separatorEmpty(),
                btn_pub_beautify->Render(),
                separatorEmpty(),
                btn_pub_clear->Render()
            };
        } else {
            status_label = "● Config: " + mqtt_tui::get_config_file_path();
            status_color = Color::RGB(255, 165, 2);
            log_action_btns = {
                btn_save_cfg->Render(),
                separatorEmpty(),
                btn_reload_cfg->Render(),
                separatorEmpty(),
                btn_cfg_clear->Render()
            };
        }

        auto logs_header = hbox({
            text(" 📝 Logs ") | bold,
            text("  " + status_label + " ") | color(status_color),
            filler(),
            hbox(std::move(log_action_btns)),
            text(" ")
        }) | bgcolor(Color::RGB(55, 55, 55));

        // Logs Content area with auto-scrolling & interactive scroll support
        Elements rendered_lines;
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            if (log_lines.empty()) {
                rendered_lines.push_back(text("(no logs yet)") | dim);
            } else {
                int target_focus = log_scroll_line;
                if (target_focus == -1 || target_focus >= (int)log_lines.size()) {
                    target_focus = (int)log_lines.size() - 1;
                }
                for (int i = 0; i < (int)log_lines.size(); ++i) {
                    if (i == target_focus) {
                        rendered_lines.push_back(text(log_lines[i]) | focus);
                    } else {
                        rendered_lines.push_back(text(log_lines[i]));
                    }
                }
            }
        }

        Element logs_box;
        if (active_tab == 3) {
            // In Config tab, log panel is much smaller (fixed height 5) at bottom!
            logs_box = vbox(std::move(rendered_lines)) | vscroll_indicator | yframe | size(HEIGHT, EQUAL, 5) | borderRounded | color(Color::RGB(102, 102, 102));
        } else {
            // In other tabs, logs box fills the remaining space with smooth scroll indicator
            logs_box = vbox(std::move(rendered_lines)) | vscroll_indicator | yframe | flex | borderRounded | color(Color::RGB(102, 102, 102));
        }

        // Bottom status footer
        auto footer = hbox({
            text(" F1/1 Connect  F2/2 Subscribe  F3/3 Publish  F4/4 Config | Tab Next  Enter/F5 Action  Ctrl+L Clear  Ctrl+Q Quit") | dim
        });

        Element tab_body;
        if (active_tab == 3) {
            tab_body = vbox(std::move(tab_elements)) | flex;
        } else {
            tab_body = vbox(std::move(tab_elements));
        }

        return vbox({
            header,
            separatorEmpty(),
            tab_body,
            separatorEmpty(),
            logs_header,
            logs_box,
            footer
        });
    });

    if (raw_inp_host) {
        raw_inp_host->TakeFocus();
    }

    screen.Loop(renderer);

    // Cleanup background processes if running
    if (sub_pid != -1) {
        kill(sub_pid, SIGTERM);
    }
    if (broker_pid != -1) {
        kill(broker_pid, SIGTERM);
    }

    return 0;
}
