#include <string>

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
