#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <poll.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
// #include <jansson.h>

#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "config.h"
#include "settings.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

// ----------------- utilities -----------------

static void die(const char *msg)
{
    spdlog::error("fatal: {}", msg);
    std::exit(1);
}

// ----------------- daemon core (gesture MVP) -----------------

struct TouchPoint
{
    int32_t id = -1;
    double x = 0, y = 0;
    bool down = false;
};
struct GestureState
{
    std::unordered_map<int32_t, TouchPoint> pts;
    double lastCx = 0, lastCy = 0;
    double lastPinchDist = 0;
    double pinchAccum = 0;
    bool mmbDown = false;
};

static double dist(double x1, double y1, double x2, double y2)
{
    double dx = x1 - x2, dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}
static int countDown(const GestureState &gs)
{
    int c = 0;
    for (auto &kv : gs.pts)
        if (kv.second.down)
            c++;
    return c;
}

static std::optional<std::pair<double, double>>
centroid2(const GestureState &gs)
{
    int count = 0;
    double sx = 0, sy = 0;
    for (auto &kv : gs.pts)
    {
        if (!kv.second.down)
            continue;
        sx += kv.second.x;
        sy += kv.second.y;
        if (++count == 2)
            break;
    }
    if (count < 2)
        return std::nullopt;
    return std::make_pair(sx / 2.0, sy / 2.0);
}
static std::optional<double> pinchDist2(const GestureState &gs)
{
    TouchPoint a, b;
    bool ga = false, gb = false;
    for (auto &kv : gs.pts)
    {
        if (!kv.second.down)
            continue;
        if (!ga)
        {
            a = kv.second;
            ga = true;
        }
        else
        {
            b = kv.second;
            gb = true;
            break;
        }
    }
    if (!ga || !gb)
        return std::nullopt;
    return dist(a.x, a.y, b.x, b.y);
}

static void sendKey(Display *dpy, KeySym ks, bool down)
{
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (kc == 0)
        return;
    XTestFakeKeyEvent(dpy, kc, down ? True : False, CurrentTime);
}
static void sendButton(Display *dpy, int button, bool down)
{
    XTestFakeButtonEvent(dpy, button, down ? True : False, CurrentTime);
}
static void sendMouseRel(Display *dpy, int dx, int dy)
{
    XTestFakeRelativeMotionEvent(dpy, dx, dy, CurrentTime);
}
static void sendWheel(Display *dpy, int direction, int clicks, bool withCtrl)
{
    if (withCtrl)
        sendKey(dpy, XK_Control_L, true);
    int btn = (direction > 0) ? 4 : 5;
    for (int i = 0; i < clicks; i++)
    {
        sendButton(dpy, btn, true);
        sendButton(dpy, btn, false);
    }
    if (withCtrl)
        sendKey(dpy, XK_Control_L, false);
}

struct Daemon
{
    Display *dpy = nullptr;
    int xi_opcode = -1;
    Window root = 0;

    Config cfg;

    //  Settings settings;
    //  MappingTable mappings;
    GestureState gs;

    std::string defaultSettingsPath;
    std::string activeConfigPath;

    // inotify
    int inofd = -1;
    int watch = -1;

    bool updateFromSettings(const Settings &settings)
    {
        return cfg.applyConfigFromPath(settings.getConfigFilePath());
    }

    // bool apply_config_from_default_conf(const Settings& settings)
    // {
    //   return cfg.applyConfigFromSettings(settings);
    // }

    void setup_xi2()
    {
        dpy = XOpenDisplay(nullptr);
        if (!dpy)
            die("cannot open X display");

        int event = 0, error = 0;
        if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event,
                             &error))
            die("X Input extension not available");

        int major = 2, minor = 3;
        if (XIQueryVersion(dpy, &major, &minor) != Success)
            die("XI2 not available");

        root = DefaultRootWindow(dpy);

        XIEventMask mask;
        unsigned char mask_data[XIMaskLen(XI_LASTEVENT)];
        std::memset(mask_data, 0, sizeof(mask_data));

        mask.deviceid = XIAllMasterDevices;
        mask.mask_len = sizeof(mask_data);
        mask.mask = mask_data;

        XISetMask(mask.mask, XI_TouchBegin);
        XISetMask(mask.mask, XI_TouchUpdate);
        XISetMask(mask.mask, XI_TouchEnd);
        XISetMask(mask.mask, XI_ButtonPress);
        XISetMask(mask.mask, XI_ButtonRelease);
        XISetMask(mask.mask, XI_RawTouchBegin);
        XISetMask(mask.mask, XI_RawTouchUpdate);
        XISetMask(mask.mask, XI_RawTouchEnd);

        XISelectEvents(dpy, root, &mask, 1);
        XFlush(dpy);
    }

    void setup_inotify()
    {
        inofd = inotify_init1(IN_NONBLOCK);
        if (inofd < 0)
            die("inotify_init1 failed");

        // Watch for atomic-save patterns too:
        // - IN_CLOSE_WRITE: file closed after write
        // - IN_MOVED_TO / IN_CREATE: editor wrote temp file then renamed
        // - IN_ATTRIB: sometimes touched by editors
        watch = inotify_add_watch(inofd, defaultSettingsPath.c_str(),
                                  IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE |
                                      IN_ATTRIB);
        if (watch < 0)
            die("inotify_add_watch failed (check path exists)");
    }

    void handle_inotify()
    {
        // Drain inotify events; we don't care about details beyond "something
        // changed".
        char buf[4096]
            __attribute__((aligned(__alignof__(struct inotify_event))));
        while (true)
        {
            ssize_t n = read(inofd, buf, sizeof(buf));
            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                std::perror("inotify read");
                break;
            }
            if (n == 0)
                break;

            // We could parse each inotify_event here, but simplest is: "reload
            // once" after draining.
        }

        spdlog::info("Configuration change detected, reloading...");
        Settings settings = Settings::reload();
        updateFromSettings(settings);
    }

    void dispatch_cookie(XGenericEventCookie *cookie)
    {
        if (cookie->type != GenericEvent || cookie->extension != xi_opcode)
            return;
        if (!XGetEventData(dpy, cookie))
            return;

        int t = cookie->evtype;
        if (t == XI_TouchBegin || t == XI_TouchUpdate || t == XI_TouchEnd)
        {
            auto *xiev = reinterpret_cast<XIDeviceEvent *>(cookie->data);
            int32_t tid = xiev->detail;
            TouchPoint &tp = gs.pts[tid];
            tp.id = tid;
            tp.x = xiev->event_x;
            tp.y = xiev->event_y;

            if (t == XI_TouchBegin)
                tp.down = true;
            else if (t == XI_TouchEnd)
            {
                tp.down = false;
                gs.pts.erase(tid);
            }

            spdlog::info("XI event: type {} device id {} source id {}",
                         cookie->evtype, xiev->deviceid, xiev->sourceid);
            update_from_touches();
        }
        else if (t == XI_ButtonPress || t == XI_ButtonRelease)
        {
            auto *xiev = reinterpret_cast<XIDeviceEvent *>(cookie->data);
            bool down = (t == XI_ButtonPress);
            spdlog::info("Button {}: detail={} deviceid={}",
                         down ? "down" : "up", xiev->detail, xiev->deviceid);
        }

        XFreeEventData(dpy, cookie);
    }

    void drain_x_events()
    {
        XEvent ev;
        while (XPending(dpy) > 0)
        {
            XNextEvent(dpy, &ev);
            if (ev.type == GenericEvent)
                dispatch_cookie(&ev.xcookie);
        }
    }

    void update_from_touches()
    {
        int n = countDown(gs);
        if (n < 2)
        {
            if (gs.mmbDown)
            {
                sendButton(dpy, 2, false);
                gs.mmbDown = false;
            }
            gs.lastPinchDist = 0;
            gs.pinchAccum = 0;
            XFlush(dpy);
            return;
        }

        auto cd = centroid2(gs);
        auto pd = pinchDist2(gs);
        if (!cd || !pd)
            return;

        double cx = cd->first, cy = cd->second;
        double d = *pd;

        if (gs.lastPinchDist == 0)
        {
            gs.lastCx = cx;
            gs.lastCy = cy;
            gs.lastPinchDist = d;
            return;
        }

        double dx = cx - gs.lastCx;
        double dy = cy - gs.lastCy;
        double dd = d - gs.lastPinchDist;

        // event -> action mapping
        if (std::abs(dx) + std::abs(dy) >= cfg.panStartThresholdPx)
        {
            if (cfg.get(EventType::TwoFingerDrag) == ActionType::PanMMBDrag)
            {
                if (!gs.mmbDown)
                {
                    sendButton(dpy, 2, true);
                    gs.mmbDown = true;
                }
                sendMouseRel(dpy, (int)std::lround(dx), (int)std::lround(dy));
            }
        }
        if (cfg.get(EventType::Pinch) == ActionType::ZoomCtrlWheel)
        {
            gs.pinchAccum += dd * cfg.pinchToWheelFactor;
            while (gs.pinchAccum >= 1.0)
            {
                sendWheel(dpy, +1, (int)std::lround(cfg.wheelStep), true);
                gs.pinchAccum -= 1.0;
            }
            while (gs.pinchAccum <= -1.0)
            {
                sendWheel(dpy, -1, (int)std::lround(cfg.wheelStep), true);
                gs.pinchAccum += 1.0;
            }
        }

        gs.lastCx = cx;
        gs.lastCy = cy;
        gs.lastPinchDist = d;
        XFlush(dpy);
    }

    [[noreturn]] void run()
    {
        int xfd = ConnectionNumber(dpy);
        if (xfd < 0)
            die("ConnectionNumber failed");

        pollfd fds[2]{};
        fds[0].fd = xfd;
        fds[0].events = POLLIN;
        fds[1].fd = inofd;
        fds[1].events = POLLIN;

        while (true)
        {
            int r = ::poll(fds, 2, -1);
            if (r < 0)
                continue;

            if (fds[1].revents & POLLIN)
                handle_inotify();
            if (fds[0].revents & POLLIN)
                drain_x_events();

            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
                die("X connection poll error");
            if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
                die("inotify poll error");
        }
    }
};

int main(int argc, char **argv)
{

    std::string settingsFile = "./settings.json";

    if (argc == 2)
    {
        settingsFile = argv[1];
        // std::fprintf(stderr, "usage: %s /path/to/default.conf\n", argv[0]);
        // return 2;
    }

    Settings settings = Settings::load(settingsFile);

    Daemon d;
    d.defaultSettingsPath = settings.getSettingsFilePath().string();

    // Default mappings if no config loads yet
    // d.cfg.map[EventType::TwoFingerDrag] = ActionType::PanMMBDrag;
    // d.mappings.map[EventType::Pinch] = ActionType::ZoomCtrlWheel;

    //  auto console = spdlog::stdout_color_mt("console");
    // auto err_logger = spdlog::stderr_color_mt("stderr");
    // spdlog::get("console")->info("loggers can be retrieved from a global
    // registry using the spdlog::get(logger_name)");

    d.setup_xi2();
    d.setup_inotify();
    // Initial config load
    if (!d.updateFromSettings(settings))
    {
        spdlog::error("touchwm_daemon: no valid config loaded at startup.");
        exit(1);
    }
    spdlog::info("touchwm_daemon_hotconfig running.");
    spdlog::info("watching {}", d.defaultSettingsPath);
    spdlog::info("active JSON: {}",
                 d.activeConfigPath.empty() ? "(none)" : d.activeConfigPath);

    d.run();
}
