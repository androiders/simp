#include "settings.h"


std::string Settings::config = "default.json";
std::string Settings::log_level = "info";
std::string Settings::log_file = "touchwm_daemon.log";
const std::filesystem::path Settings::user_config_path = Settings::getUserConfigPath();