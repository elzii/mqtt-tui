#include "beautify.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>
#include <algorithm>

static inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string beautify_json(const std::string& raw_text) {
    if (trim(raw_text).empty()) {
        return raw_text;
    }

    std::stringstream ss(raw_text);
    std::string line;
    std::vector<std::string> new_lines;
    bool modified = false;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            new_lines.push_back(line);
            continue;
        }

        std::string prefix = "";
        std::string json_candidate = "";

        // Check if line starts with timestamp like [11:34:50]
        if (line.rfind("[", 0) == 0 && line.find(']') != std::string::npos) {
            size_t cb = line.find(']');
            std::string inside = line.substr(1, cb - 1);
            if (inside.find(':') != std::string::npos) {
                prefix = trim(line.substr(0, cb + 1));
                std::string rem = trim(line.substr(cb + 1));
                size_t fb = rem.find('{');
                if (fb != std::string::npos) {
                    std::string topic_part = trim(rem.substr(0, fb));
                    if (!topic_part.empty()) {
                        prefix += " " + topic_part;
                    }
                    json_candidate = trim(rem.substr(fb));
                } else {
                    json_candidate = rem;
                }
            }
        }

        if (json_candidate.empty()) {
            size_t fb = line.find('{');
            if (fb != std::string::npos) {
                prefix = trim(line.substr(0, fb));
                json_candidate = trim(line.substr(fb));
            }
        }

        if (!json_candidate.empty() && json_candidate.front() == '{' && json_candidate.back() == '}') {
            try {
                auto parsed = nlohmann::ordered_json::parse(json_candidate);
                std::string formatted = parsed.dump(2);
                if (!prefix.empty()) {
                    new_lines.push_back(prefix);
                }
                std::stringstream fss(formatted);
                std::string fline;
                while (std::getline(fss, fline)) {
                    if (!fline.empty() && fline.back() == '\r') fline.pop_back();
                    new_lines.push_back(fline);
                }
                modified = true;
                continue;
            } catch (...) {
                // Not valid JSON, keep as is
            }
        }

        new_lines.push_back(line);
    }

    if (!modified) {
        return raw_text;
    }

    std::ostringstream result;
    for (size_t i = 0; i < new_lines.size(); ++i) {
        result << new_lines[i];
        if (i + 1 < new_lines.size()) {
            result << "\n";
        }
    }
    return result.str();
}
