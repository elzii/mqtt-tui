#pragma once

#include <string>
#include <vector>

namespace mqtt_tui {

std::string get_history_file_path();
std::string get_config_file_path();
void ensure_history_file();

std::vector<std::string> load_history(const std::string& field);
void save_history(const std::string& field, const std::string& value);

} // namespace mqtt_tui
