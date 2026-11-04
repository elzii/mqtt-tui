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

    // Subscribe Tab inputs
    std::string sub_topics_val = "#";
    std::string sub_port_val = "1883";
    int sub_topics_hist_idx = -1;
    int sub_port_hist_idx = -1;

    // Publish Tab inputs
    std::string pub_topic_val = "test/topic";
    std::string pub_msg_val = "{\"timestamp\": 1793124973.167886, \"numeric\": 15, \"value\": 15}";
    int pub_topic_hist_idx = -1;
    int pub_msg_hist_idx = -1;

    // Config Tab content
    std::string config_val = "";
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
    int log_scroll = 0;

    auto append_log = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_lines.push_back(msg);
        log_scroll = (int)log_lines.size();
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
    auto make_badge_btn_option = []() {
        ButtonOption opt;
        opt.transform = [](const EntryState& s) {
            auto elem = text(" " + s.label + " ");
            if (s.focused) {
                return elem | bgcolor(Color::RGB(200, 200, 200)) | color(Color::Black) | bold;
            }
            return elem | bgcolor(Color::RGB(128, 128, 128)) | color(Color::RGB(28, 28, 28)) | bold;
        };
        return opt;
    };

    // Helper: Tab button options
    auto make_tab_btn = [&](const std::string& label, int tab_id, Color active_col) {
        ButtonOption opt;
        opt.transform = [&, tab_id, active_col](const EntryState& s) {
            bool is_active = (active_tab == tab_id);
            auto elem = text(" " + label + " ");
            if (is_active) {
                return elem | bgcolor(active_col) | color(Color::Black) | bold;
            }
            if (s.focused) {
                return elem | border | color(active_col) | bold;
            }
            return elem | border | color(Color::RGB(100, 100, 100));
        };
        return Button(label, [&, tab_id] { active_tab = tab_id; }, opt);
    };

    auto tab_connect_btn = make_tab_btn("Connect", 0, Color::RGB(46, 213, 115));
    auto tab_sub_btn = make_tab_btn("Subscribe", 1, Color::RGB(84, 160, 255));
    auto tab_pub_btn = make_tab_btn("Publish", 2, Color::RGB(224, 86, 253));
    auto tab_cfg_btn = make_tab_btn("🎛️", 3, Color::RGB(255, 165, 2));

    // Inputs with history handler decorator
    auto make_history_input = [&](std::string& val_ref, const std::string& placeholder, const std::string& field_name, int& hist_idx) {
        InputOption opt;
        opt.placeholder = placeholder;
        auto inp = Input(&val_ref, opt);
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
    auto inp_host = make_history_input(host_val, "Host Address (DEFAULT = 127.0.0.1)", "host", host_hist_idx);
    auto inp_port = make_history_input(port_val, "Port (DEFAULT = 1883|9001)", "port", port_hist_idx);

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
        std::string h = host_val.empty() ? "127.0.0.1" : host_val;
        int p = 1883;
        try {
            if (!port_val.empty()) p = std::stoi(port_val);
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

            // Set timeout
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

    auto btn_clear = Button("🧹 Clear", [&] {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_lines.clear();
        log_scroll = 0;
        screen.PostEvent(Event::Custom);
    }, make_badge_btn_option());

    // Subscribe Tab components
    auto inp_sub_topics = make_history_input(sub_topics_val, "Topics (Comma Separated, DEFAULT = #)", "sub_topics", sub_topics_hist_idx);
    auto inp_sub_port = make_history_input(sub_port_val, "Port (DEFAULT = 1883|9001)", "sub_port", sub_port_hist_idx);

    auto btn_start_sub = Button("▶ Start Sub", [&] {
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

            std::thread([pipe_fd, pid, &sub_pid, &append_log]() {
                FILE* f = fdopen(pipe_fd[0], "r");
                if (f) {
                    char buf[2048];
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
            }).detach();
        }
    }, make_badge_btn_option());

    auto btn_stop_sub = Button("⏹ Stop Sub", [&] {
        if (sub_pid != -1) {
            pid_t pid = sub_pid;
            kill(pid, SIGTERM);
            append_log("[" + get_current_time_str() + "] Sent SIGTERM to subscriber (PID " + std::to_string(pid) + ")");
        } else {
            append_log("[" + get_current_time_str() + "] No subscriber process active.");
        }
    }, make_badge_btn_option());

    auto btn_beautify = Button("💋 Beautify JSON", [&] {
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
        log_scroll = (int)log_lines.size();
        screen.PostEvent(Event::Custom);
    }, make_badge_btn_option());

    // Publish Tab components
    auto inp_pub_topic = make_history_input(pub_topic_val, "Topic (DEFAULT = test/topic)", "pub_topic", pub_topic_hist_idx);
    auto inp_pub_msg = make_history_input(pub_msg_val, "Message", "pub_message", pub_msg_hist_idx);

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

    // Config Tab components
    InputOption cfg_opt;
    cfg_opt.multiline = true;
    auto inp_config = Input(&config_val, cfg_opt);

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

    // Containers per tab
    auto tab_connect_container = Container::Vertical({
        inp_host,
        inp_port,
        Container::Horizontal({
            btn_start_broker,
            btn_stop_broker,
            btn_ping,
            btn_clear
        })
    });

    auto tab_sub_container = Container::Vertical({
        inp_sub_topics,
        inp_sub_port,
        Container::Horizontal({
            btn_start_sub,
            btn_stop_sub,
            btn_beautify,
            btn_clear
        })
    });

    auto tab_pub_container = Container::Vertical({
        inp_pub_topic,
        inp_pub_msg,
        Container::Horizontal({
            btn_publish,
            btn_beautify,
            btn_clear
        })
    });

    auto tab_cfg_container = Container::Vertical({
        inp_config,
        Container::Horizontal({
            btn_save_cfg,
            btn_reload_cfg,
            btn_clear
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

    // Custom CatchEvent for global shortcuts
    auto root_component = CatchEvent(main_container, [&](Event event) {
        if (event == Event::F1 || event == Event::Character('1')) {
            active_tab = 0;
            return true;
        }
        if (event == Event::F2 || event == Event::Character('2')) {
            active_tab = 1;
            return true;
        }
        if (event == Event::F3 || event == Event::Character('3')) {
            active_tab = 2;
            return true;
        }
        if (event == Event::F4 || event == Event::Character('4')) {
            active_tab = 3;
            return true;
        }
        if (event == Event::Special({17})) { // Ctrl+Q
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Special({12})) { // Ctrl+L
            std::lock_guard<std::mutex> lock(log_mutex);
            log_lines.clear();
            log_scroll = 0;
            return true;
        }
        return false;
    });

    // Renderer
    auto renderer = Renderer(root_component, [&] {
        // Top Header
        auto header = hbox({
            text(" 🦟 MQTT : ") | color(Color::RGB(255, 177, 66)) | bold,
            text("Server ") | color(Color::RGB(46, 213, 115)) | bold,
            text("| ") | color(Color::RGB(100, 100, 100)),
            text("Subscribe ") | color(Color::RGB(84, 160, 255)) | bold,
            text("| ") | color(Color::RGB(100, 100, 100)),
            text("Publish   ") | color(Color::RGB(224, 86, 253)) | bold,
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

        // Tab Content
        Elements tab_elements;
        if (active_tab == 0) {
            tab_elements.push_back(
                hbox({
                    text("  🔗  ") | bold,
                    inp_host->Render() | border | flex
                })
            );
            tab_elements.push_back(
                hbox({
                    text("  🏗️  ") | bold,
                    inp_port->Render() | border | flex
                })
            );
        } else if (active_tab == 1) {
            tab_elements.push_back(
                hbox({
                    text("  💬  ") | bold,
                    inp_sub_topics->Render() | border | flex
                })
            );
            tab_elements.push_back(
                hbox({
                    text("  🏗️  ") | bold,
                    inp_sub_port->Render() | border | flex
                })
            );
        } else if (active_tab == 2) {
            tab_elements.push_back(
                hbox({
                    text("  💬  ") | bold,
                    inp_pub_topic->Render() | border | flex
                })
            );
            tab_elements.push_back(
                hbox({
                    text("  💬  ") | bold,
                    inp_pub_msg->Render() | border | size(HEIGHT, EQUAL, 5) | flex
                })
            );
        } else if (active_tab == 3) {
            tab_elements.push_back(
                hbox({
                    text("  🎛️  ") | bold,
                    inp_config->Render() | border | size(HEIGHT, EQUAL, 7) | flex
                })
            );
        }

        // Logs Header Bar
        Elements log_action_btns;
        if (active_tab == 0) {
            log_action_btns = {
                btn_start_broker->Render(),
                separatorEmpty(),
                btn_stop_broker->Render(),
                separatorEmpty(),
                btn_ping->Render(),
                separatorEmpty(),
                btn_clear->Render()
            };
        } else if (active_tab == 1) {
            log_action_btns = {
                btn_start_sub->Render(),
                separatorEmpty(),
                btn_stop_sub->Render(),
                separatorEmpty(),
                btn_beautify->Render(),
                separatorEmpty(),
                btn_clear->Render()
            };
        } else if (active_tab == 2) {
            log_action_btns = {
                btn_publish->Render(),
                separatorEmpty(),
                btn_beautify->Render(),
                separatorEmpty(),
                btn_clear->Render()
            };
        } else {
            log_action_btns = {
                btn_save_cfg->Render(),
                separatorEmpty(),
                btn_reload_cfg->Render(),
                separatorEmpty(),
                btn_clear->Render()
            };
        }

        auto logs_header = hbox({
            text(" 📝 Logs ") | bold,
            text("  ● Broker: " + broker_status + " ") | color(broker_status_color),
            filler(),
            hbox(std::move(log_action_btns)),
            text(" ")
        }) | bgcolor(Color::RGB(55, 55, 55));

        // Logs Content area
        Elements rendered_lines;
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            int start_idx = 0;
            if ((int)log_lines.size() > 50) {
                start_idx = (int)log_lines.size() - 50;
            }
            for (size_t i = start_idx; i < log_lines.size(); ++i) {
                rendered_lines.push_back(text(log_lines[i]));
            }
        }
        if (rendered_lines.empty()) {
            rendered_lines.push_back(text("(no logs yet)") | dim);
        }

        auto logs_box = vbox(std::move(rendered_lines)) | frame | flex | border;

        // Bottom status footer
        auto footer = hbox({
            text(" F1/1 Connect  F2/2 Subscribe  F3/3 Publish  F4/4 Config | Tab Next  Enter/F5 Action  Ctrl+L Clear  Ctrl+Q Quit") | dim
        });

        return vbox({
            header,
            separatorEmpty(),
            vbox(std::move(tab_elements)),
            separatorEmpty(),
            logs_header,
            logs_box,
            footer
        });
    });

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
