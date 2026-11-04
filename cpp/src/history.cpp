#include "history.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

namespace mqtt_tui {

static std::mutex g_history_mutex;

std::string get_history_file_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.config/mqtttui/history.csv";
}

std::string get_config_file_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.config/mosquitto/mosquitto.tui.conf";
}

void ensure_history_file() {
    std::string path = get_history_file_path();
    fs::path p(path);
    if (fs::is_directory(p)) {
        try { fs::remove(p); } catch (...) {}
    }
    fs::create_directories(p.parent_path());
    if (!fs::exists(p)) {
        std::ofstream ofs(path);
        if (ofs.is_open()) {
            ofs << "timestamp,field,value\n";
        }
    }
}

static std::string escape_csv(const std::string& val) {
    bool need_quotes = (val.find(',') != std::string::npos ||
                        val.find('\"') != std::string::npos ||
                        val.find('\n') != std::string::npos);
    if (!need_quotes) return val;
    std::string res = "\"";
    for (char c : val) {
        if (c == '\"') res += "\"\"";
        else res += c;
    }
    res += "\"";
    return res;
}

static std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '\"') {
                if (i + 1 < line.size() && line[i + 1] == '\"') {
                    current += '\"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current += c;
            }
        } else {
            if (c == '\"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
    }
    fields.push_back(current);
    return fields;
}

std::vector<std::string> load_history(const std::string& field) {
    std::lock_guard<std::mutex> lock(g_history_mutex);
    ensure_history_file();
    std::vector<std::string> results;
    std::ifstream ifs(get_history_file_path());
    if (!ifs.is_open()) return results;

    std::string line;
    bool header = true;
    while (std::getline(ifs, line)) {
        if (header) {
            header = false;
            continue;
        }
        if (line.empty()) continue;
        auto row = parse_csv_line(line);
        if (row.size() >= 3 && row[1] == field && !row[2].empty()) {
            results.push_back(row[2]);
        }
    }
    return results;
}

void save_history(const std::string& field, const std::string& value) {
    if (value.empty()) return;
    std::lock_guard<std::mutex> lock(g_history_mutex);
    ensure_history_file();

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");

    std::ofstream ofs(get_history_file_path(), std::ios::app);
    if (ofs.is_open()) {
        ofs << ss.str() << "," << escape_csv(field) << "," << escape_csv(value) << "\n";
    }
}

} // namespace mqtt_tui
