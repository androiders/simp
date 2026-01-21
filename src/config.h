#include <string>
#include <unordered_map>

#include "events.h"
#include "actions.h"

using EventActionMap = std::unordered_map<EventType, ActionType>;

class Config 
{
    public:
        Config() = default;
        ~Config() = default;    
    
        bool load(const std::string &path);
        
        bool apply_config_from_default_conf();

        ActionType get(EventType e) const;

        double panStartThresholdPx = 2.0;
        double pinchToWheelFactor = 0.02;
        double wheelStep = 1.0;

        bool enableThumbZone = true;
        double thumbZoneWFrac = 0.25;
        double thumbZoneHFrac = 0.25;

        EventActionMap eaMap;

};


