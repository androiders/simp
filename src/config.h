#include <string>
#include <unordered_map>
#include <filesystem>

#include "events.h"
#include "actions.h"

using EventActionMap = std::unordered_map<EventType, ActionType>;

class Settings;

class Config 
{
    public:
        Config() = default;
        ~Config() = default;    
    
        bool load(const std::string &path);

        bool applyConfigFromSettings(const Settings & settings);

        ActionType get(EventType e) const;

        double panStartThresholdPx = 2.0;
        double pinchToWheelFactor = 0.02;
        double wheelStep = 1.0;

        bool enableThumbZone = true;
        double thumbZoneWFrac = 0.25;
        double thumbZoneHFrac = 0.25;

        EventActionMap eaMap;

        std::string activeJson;

};


