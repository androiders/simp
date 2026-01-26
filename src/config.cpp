#include <optional>
#include <fstream>
#include "config.h"
//#include "json_helpers.h"
#include "spdlog/spdlog.h"
#include "settings.h"


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
        spdlog::error("config: failed to load {} ({}:{}): {}", path, err.line, err.column, err.text);
        return false;
    }
    if (!json_is_object(root))
    {
        spdlog::error("config: {} root is not an object", path);
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
                spdlog::warn("config: unknown mapping '{} -> {}'", key, json_string_value(val));
                continue;
            }
            this->eaMap[e] = a;
        }
    }

    json_decref(root);
    return true;
}


  bool Config::applyConfigFromSettings(const Settings & settings) {
    auto conf = settings.getConfig();

    if (conf == this->activeJson) {
      // same profile; still ok
      return true;
    }

    std::string confPath = settings.getConfigFilePath().string();
    if (!this->load(confPath)) {
        spdlog::error("config: failed to load new config: {}", confPath);
        return false;
    }

    this->activeJson = conf;
    spdlog::info("config: switched to {}", this->activeJson);
    return true;
  }

