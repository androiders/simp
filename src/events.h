#include <string>

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
