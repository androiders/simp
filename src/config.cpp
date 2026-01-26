#include <optional>
#include <fstream>
#include "config.h"
#include "json_helpers.h"
#include "spdlog/spdlog.h"


std::string Config::getUserConfigPath()
{
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfigHome != nullptr) {
        return std::string(xdgConfigHome) + "/" + APP_NAME;
    } else {
        // Fall back to ~/.config
        const char* homeDir = std::getenv("HOME");
        if (homeDir == nullptr) {
            throw std::runtime_error("HOME environment variable not set!");
        }
        return std::string(homeDir) + "/.config/" + APP_NAME;
    }
}

static std::string trim(std::string s) {
  auto isspace_ = [](unsigned char c){ return std::isspace(c); };
  while (!s.empty() && isspace_((unsigned char)s.front())) s.erase(s.begin());
  while (!s.empty() && isspace_((unsigned char)s.back())) s.pop_back();
  return s;
}

static std::optional<std::string> read_default_conf(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return std::nullopt;

  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (!line.empty() && line[0] == '#') continue;
    return line; // first usable line is the json path
  }
  return std::nullopt;
}



ActionType Config::get(EventType e) const
{
    auto it = eaMap.find(e);
    return (it == eaMap.end()) ? ActionType::NoType : it->second;
}


bool Config::load(const std::string &path)
{
    json_error_t err{};
    json_t *root = json_load_file(path.c_str(), 0, &err);
    if (!root)
    {
        std::fprintf(stderr, "config: failed to load %s (%d:%d): %s\n",
                     path.c_str(), err.line, err.column, err.text);
        return false;
    }
    if (!json_is_object(root))
    {
        std::fprintf(stderr, "config: %s root is not an object\n", path.c_str());
        json_decref(root);
        return false;
    }

    // gesture
    if (json_t *g = json_object_get(root, "gesture"); g && json_is_object(g))
    {
        json_get_number(g, "panStartThresholdPx", this->panStartThresholdPx);
        json_get_number(g, "pinchToWheelFactor", this->pinchToWheelFactor);
        json_get_number(g, "wheelStep", this->wheelStep);
        json_get_bool(g, "enableThumbZone", this->enableThumbZone);
        json_get_number(g, "thumbZoneWFrac", this->thumbZoneWFrac);
        json_get_number(g, "thumbZoneHFrac", this->thumbZoneHFrac);
    }

    // mappings
    if (json_t *m = json_object_get(root, "mappings"); m && json_is_object(m))
    {
        // clear old mappings and rebuild
        this->eaMap.clear();

        const char *key = nullptr;
        json_t *val = nullptr;
        json_object_foreach(m, key, val)
        {
            if (!json_is_string(val))
                continue;

            EventType e = eventFromString(key);
            ActionType a = actionFromString(json_string_value(val));
            if (e == EventType::Unknown || a == ActionType::NoType)
            {
                std::fprintf(stderr, "config: unknown mapping '%s' -> '%s'\n",
                             key, json_string_value(val));
                continue;
            }
            this->eaMap[e] = a;
        }
    }

    json_decref(root);
    return true;
}


  bool Config::apply_config_from_default_conf() {
    std::string defaultConfPath = getUserConfigPath() + "/" + DEFAULT_CONFIG_FILE;
    auto p = read_default_conf(defaultConfPath);
    if (!p) {
        spdlog::error("config: default.conf unreadable or empty: {}", defaultConfPath);
        return false;
    }

    std::string newJson = *p;
    if (newJson == this->activeJson) {
      // same profile; still ok
      return true;
    }

    if (!this->load(newJson)) {
        spdlog::error("config: failed to load new config: {}", newJson);
        return false;
    }

    //settings = newS;
    //mappings = newM;
    this->activeJson = newJson;
    spdlog::info("config: switched to {}", this->activeJson);
    return true;
  }

