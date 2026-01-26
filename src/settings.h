#include <string>
#include <fstream>
#include <filesystem>
#include "json_helpers.h"

const std::string APP_NAME = "simp";
const std::string SETTINGS_FILE = "settings.json";

class Settings {
public:

    static Settings reload()
    {
        return load();
    }       

    static Settings load(const std::filesystem::path &path = Settings::getSettingsFilePath())
    {
        Settings settings;
        json_error_t err{};

        json_t *root = json_load_file(path.c_str(), 0, &err);
        if (root) {
            if (json_t *cfg = json_object_get(root, "config"); cfg && json_is_string(cfg)) {
                settings.config = json_string_value(cfg);
            }
            if (json_t *lvl = json_object_get(root, "log_level"); lvl && json_is_string(lvl)) {
                settings.log_level = json_string_value(lvl);
            }
            if (json_t *lf = json_object_get(root, "log_file"); lf && json_is_string(lf)) {
                settings.log_file = json_string_value(lf);
            }
            json_decref(root);
        }
        return settings;
    }


    static std::filesystem::path getSettingsFilePath()
    {
        return user_config_path / std::filesystem::path(SETTINGS_FILE);
    }

    static std::filesystem::path getConfigFilePath()
    {
        return user_config_path / std::filesystem::path(config);
    }

    static std::string getConfig()
    {
        return config;
    }   

    static std::string getLogLevel()
    {
        return log_level;
    }

    static std::string getLogFile()
    {
        return log_file;
    }


private:

    static std::string config;
    static std::string log_level;
    static std::string log_file;
    static const std::filesystem::path user_config_path;


    static std::filesystem::path getUserConfigPath()
    {
        const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfigHome != nullptr) {
            return std::filesystem::path(xdgConfigHome) / std::filesystem::path(APP_NAME);
        } else {
            // Fall back to ~/.config
            const char* homeDir = std::getenv("HOME");
            if (homeDir == nullptr) {
                throw std::runtime_error("HOME environment variable not set!");
            }
            return std::filesystem::path(homeDir) / std::filesystem::path(".config") / std::filesystem::path(APP_NAME);
        }
    }
};