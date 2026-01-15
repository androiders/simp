#include <jansson.h>
#include <string>
#include <unordered_map>

enum class ActionType
{
    NoType = 0,
    PanMMBDrag,
    ZoomCtrlWheel
};

static ActionType actionFromString(const std::string &s)
{
    if (s == "pan_mmb_drag")
        return ActionType::PanMMBDrag;
    if (s == "zoom_ctrl_wheel")
        return ActionType::ZoomCtrlWheel;
    return ActionType::NoType;
}

enum class EventType
{
    Unknown = 0,
    TwoFingerDrag,
    Pinch
};

static EventType eventFromString(const std::string &s)
{
    if (s == "two_finger_drag")
        return EventType::TwoFingerDrag;
    if (s == "pinch")
        return EventType::Pinch;
    return EventType::Unknown;
}

struct Settings
{
    double panStartThresholdPx = 2.0;
    double pinchToWheelFactor = 0.02;
    double wheelStep = 1.0;

    bool enableThumbZone = true;
    double thumbZoneWFrac = 0.25;
    double thumbZoneHFrac = 0.25;
};

struct MappingTable
{
    std::unordered_map<EventType, ActionType> map;
    ActionType get(EventType e) const
    {
        auto it = map.find(e);
        return (it == map.end()) ? ActionType::NoType : it->second;
    }
};

static bool json_get_number(json_t *obj, const char *key, double &out)
{
    json_t *v = json_object_get(obj, key);
    if (!v || !json_is_number(v))
        return false;
    out = json_number_value(v);
    return true;
}
static bool json_get_bool(json_t *obj, const char *key, bool &out)
{
    json_t *v = json_object_get(obj, key);
    if (!v || !json_is_boolean(v))
        return false;
    out = json_is_true(v);
    return true;
}

static bool load_json_config(const std::string &path, Settings &s, MappingTable &mt)
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
        json_get_number(g, "panStartThresholdPx", s.panStartThresholdPx);
        json_get_number(g, "pinchToWheelFactor", s.pinchToWheelFactor);
        json_get_number(g, "wheelStep", s.wheelStep);
        json_get_bool(g, "enableThumbZone", s.enableThumbZone);
        json_get_number(g, "thumbZoneWFrac", s.thumbZoneWFrac);
        json_get_number(g, "thumbZoneHFrac", s.thumbZoneHFrac);
    }

    // mappings
    if (json_t *m = json_object_get(root, "mappings"); m && json_is_object(m))
    {
        // clear old mappings and rebuild
        mt.map.clear();

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
            mt.map[e] = a;
        }
    }

    json_decref(root);
    return true;
}
