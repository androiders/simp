#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <poll.h>
// #include <jansson.h>

#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <optional>
#include <string>
#include <fstream>
#include <sstream>
#include "config.h"

// ----------------- utilities -----------------

static void die(const char* msg) {
  std::fprintf(stderr, "fatal: %s\n", msg);
  std::exit(1);
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

// ----------------- config model -----------------

// enum class ActionType {
//   NoType = 0,
//   PanMMBDrag,
//   ZoomCtrlWheel
// };

// static ActionType actionFromString(const std::string& s) {
//   if (s == "pan_mmb_drag") return ActionType::PanMMBDrag;
//   if (s == "zoom_ctrl_wheel") return ActionType::ZoomCtrlWheel;
//   return ActionType::NoType;
// }

// enum class EventType {
//   Unknown = 0,
//   TwoFingerDrag,
//   Pinch
// };

// static EventType eventFromString(const std::string& s) {
//   if (s == "two_finger_drag") return EventType::TwoFingerDrag;
//   if (s == "pinch") return EventType::Pinch;
//   return EventType::Unknown;
// }

// struct Settings {
//   double panStartThresholdPx = 2.0;
//   double pinchToWheelFactor  = 0.02;
//   double wheelStep           = 1.0;

//   bool enableThumbZone = true;
//   double thumbZoneWFrac = 0.25;
//   double thumbZoneHFrac = 0.25;
// };

// struct MappingTable {
//   std::unordered_map<EventType, ActionType> map;
//   ActionType get(EventType e) const {
//     auto it = map.find(e);
//     return (it == map.end()) ? ActionType::NoType : it->second;
//   }
// };

// static bool json_get_number(json_t* obj, const char* key, double& out) {
//   json_t* v = json_object_get(obj, key);
//   if (!v || !json_is_number(v)) return false;
//   out = json_number_value(v);
//   return true;
// }
// static bool json_get_bool(json_t* obj, const char* key, bool& out) {
//   json_t* v = json_object_get(obj, key);
//   if (!v || !json_is_boolean(v)) return false;
//   out = json_is_true(v);
//   return true;
// }

// static bool load_json_config(const std::string& path, Settings& s, MappingTable& mt) {
//   json_error_t err{};
//   json_t* root = json_load_file(path.c_str(), 0, &err);
//   if (!root) {
//     std::fprintf(stderr, "config: failed to load %s (%d:%d): %s\n",
//                  path.c_str(), err.line, err.column, err.text);
//     return false;
//   }
//   if (!json_is_object(root)) {
//     std::fprintf(stderr, "config: %s root is not an object\n", path.c_str());
//     json_decref(root);
//     return false;
//   }

//   // gesture
//   if (json_t* g = json_object_get(root, "gesture"); g && json_is_object(g)) {
//     json_get_number(g, "panStartThresholdPx", s.panStartThresholdPx);
//     json_get_number(g, "pinchToWheelFactor",  s.pinchToWheelFactor);
//     json_get_number(g, "wheelStep",           s.wheelStep);
//     json_get_bool(g,   "enableThumbZone",     s.enableThumbZone);
//     json_get_number(g, "thumbZoneWFrac",      s.thumbZoneWFrac);
//     json_get_number(g, "thumbZoneHFrac",      s.thumbZoneHFrac);
//   }

//   // mappings
//   if (json_t* m = json_object_get(root, "mappings"); m && json_is_object(m)) {
//     // clear old mappings and rebuild
//     mt.map.clear();

//     const char* key = nullptr;
//     json_t* val = nullptr;
//     json_object_foreach(m, key, val) {
//       if (!json_is_string(val)) continue;

//       EventType e = eventFromString(key);
//       ActionType a = actionFromString(json_string_value(val));
//       if (e == EventType::Unknown || a == ActionType::NoType) {
//         std::fprintf(stderr, "config: unknown mapping '%s' -> '%s'\n",
//                      key, json_string_value(val));
//         continue;
//       }
//       mt.map[e] = a;
//     }
//   }

//   json_decref(root);
//   return true;
// }

// ----------------- daemon core (gesture MVP) -----------------

struct TouchPoint { int32_t id=-1; double x=0,y=0; bool down=false; };
struct GestureState {
  std::unordered_map<int32_t, TouchPoint> pts;
  double lastCx=0,lastCy=0;
  double lastPinchDist=0;
  double pinchAccum=0;
  bool mmbDown=false;
};

static double dist(double x1,double y1,double x2,double y2){ double dx=x1-x2,dy=y1-y2; return std::sqrt(dx*dx+dy*dy); }
static int countDown(const GestureState& gs){ int c=0; for(auto&kv:gs.pts) if(kv.second.down) c++; return c; }

static std::optional<std::pair<double,double>> centroid2(const GestureState& gs){
  int count=0; double sx=0,sy=0;
  for(auto&kv:gs.pts){ if(!kv.second.down) continue; sx+=kv.second.x; sy+=kv.second.y; if(++count==2) break; }
  if(count<2) return std::nullopt;
  return std::make_pair(sx/2.0, sy/2.0);
}
static std::optional<double> pinchDist2(const GestureState& gs){
  TouchPoint a,b; bool ga=false,gb=false;
  for(auto&kv:gs.pts){
    if(!kv.second.down) continue;
    if(!ga){ a=kv.second; ga=true; }
    else { b=kv.second; gb=true; break; }
  }
  if(!ga||!gb) return std::nullopt;
  return dist(a.x,a.y,b.x,b.y);
}

static void sendKey(Display* dpy, KeySym ks, bool down){
  KeyCode kc = XKeysymToKeycode(dpy, ks);
  if(kc==0) return;
  XTestFakeKeyEvent(dpy, kc, down?True:False, CurrentTime);
}
static void sendButton(Display* dpy, int button, bool down){
  XTestFakeButtonEvent(dpy, button, down?True:False, CurrentTime);
}
static void sendMouseRel(Display* dpy, int dx, int dy){
  XTestFakeRelativeMotionEvent(dpy, dx, dy, CurrentTime);
}
static void sendWheel(Display* dpy, int direction, int clicks, bool withCtrl){
  if(withCtrl) sendKey(dpy, XK_Control_L, true);
  int btn = (direction>0)?4:5;
  for(int i=0;i<clicks;i++){ sendButton(dpy, btn, true); sendButton(dpy, btn, false); }
  if(withCtrl) sendKey(dpy, XK_Control_L, false);
}

struct Daemon {
  Display* dpy=nullptr;
  int xi_opcode=-1;
  Window root=0;

  Settings settings;
  MappingTable mappings;
  GestureState gs;

  std::string defaultConfPath;
  std::string activeJsonPath;

  // inotify
  int inofd = -1;
  int watch = -1;

  bool apply_config_from_default_conf() {
    auto p = read_default_conf(defaultConfPath);
    if (!p) {
      std::fprintf(stderr, "config: default.conf unreadable or empty: %s\n", defaultConfPath.c_str());
      return false;
    }

    std::string newJson = *p;
    if (newJson == activeJsonPath) {
      // same profile; still ok
      return true;
    }

    Settings newS = settings;     // start from current as base
    MappingTable newM = mappings; // copy current

    if (!load_json_config(newJson, newS, newM)) {
      std::fprintf(stderr, "config: keeping existing config (failed to load %s)\n", newJson.c_str());
      return false;
    }

    settings = newS;
    mappings = newM;
    activeJsonPath = newJson;

    std::fprintf(stderr, "config: switched to %s\n", activeJsonPath.c_str());
    return true;
  }

  void setup_xi2() {
    dpy = XOpenDisplay(nullptr);
    if(!dpy) die("cannot open X display");

    int event=0, error=0;
    if(!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error))
      die("X Input extension not available");

    int major=2, minor=3;
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

  void setup_inotify() {
    inofd = inotify_init1(IN_NONBLOCK);
    if (inofd < 0) die("inotify_init1 failed");

    // Watch for atomic-save patterns too:
    // - IN_CLOSE_WRITE: file closed after write
    // - IN_MOVED_TO / IN_CREATE: editor wrote temp file then renamed
    // - IN_ATTRIB: sometimes touched by editors
    watch = inotify_add_watch(inofd, defaultConfPath.c_str(),
                              IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_ATTRIB);
    if (watch < 0) die("inotify_add_watch failed (check path exists)");
  }

  void handle_inotify() {
    // Drain inotify events; we don't care about details beyond "something changed".
    char buf[4096]
      __attribute__((aligned(__alignof__(struct inotify_event))));
    while (true) {
      ssize_t n = read(inofd, buf, sizeof(buf));
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        std::perror("inotify read");
        break;
      }
      if (n == 0) break;

      // We could parse each inotify_event here, but simplest is: "reload once"
      // after draining.
    }

    apply_config_from_default_conf();
  }

  void dispatch_cookie(XGenericEventCookie* cookie) {
    if(cookie->type != GenericEvent || cookie->extension != xi_opcode) return;
    if(!XGetEventData(dpy, cookie)) return;

  
    int t = cookie->evtype;
    if (t == XI_TouchBegin || t == XI_TouchUpdate || t == XI_TouchEnd) {
      auto* xiev = reinterpret_cast<XIDeviceEvent*>(cookie->data);
      int32_t tid = xiev->detail;
      TouchPoint& tp = gs.pts[tid];
      tp.id = tid;
      tp.x = xiev->event_x;
      tp.y = xiev->event_y;

      if (t == XI_TouchBegin) tp.down = true;
      else if (t == XI_TouchEnd) { tp.down = false; gs.pts.erase(tid); }

      std::fprintf(stderr,
  "XI event %d device=%d sourceid=%d\n",
  cookie->evtype,
  xiev->deviceid,
  xiev->sourceid);

      update_from_touches();
    } else if (t == XI_ButtonPress || t == XI_ButtonRelease) {
      auto* xiev = reinterpret_cast<XIDeviceEvent*>(cookie->data);
      bool down = (t == XI_ButtonPress);
      std::fprintf(stderr, "Button %s: detail=%u deviceid=%d\n",
        down ? "down" : "up", xiev->detail, xiev->deviceid);
    }

    XFreeEventData(dpy, cookie);
  }

  void drain_x_events() {
    XEvent ev;
    while (XPending(dpy) > 0) {
      XNextEvent(dpy, &ev);
      if (ev.type == GenericEvent) dispatch_cookie(&ev.xcookie);
    }
  }

  void update_from_touches() {
    int n = countDown(gs);
    if (n < 2) {
      if (gs.mmbDown) { sendButton(dpy, 2, false); gs.mmbDown = false; }
      gs.lastPinchDist = 0;
      gs.pinchAccum = 0;
      XFlush(dpy);
      return;
    }

    auto cd = centroid2(gs);
    auto pd = pinchDist2(gs);
    if (!cd || !pd) return;

    double cx = cd->first, cy = cd->second;
    double d  = *pd;

    if (gs.lastPinchDist == 0) {
      gs.lastCx = cx; gs.lastCy = cy;
      gs.lastPinchDist = d;
      return;
    }

    double dx = cx - gs.lastCx;
    double dy = cy - gs.lastCy;
    double dd = d  - gs.lastPinchDist;

    // event -> action mapping
    if (std::abs(dx) + std::abs(dy) >= settings.panStartThresholdPx) {
      if (mappings.get(EventType::TwoFingerDrag) == ActionType::PanMMBDrag) {
        if (!gs.mmbDown) { sendButton(dpy, 2, true); gs.mmbDown = true; }
        sendMouseRel(dpy, (int)std::lround(dx), (int)std::lround(dy));
      }
    }
    if (mappings.get(EventType::Pinch) == ActionType::ZoomCtrlWheel) {
      gs.pinchAccum += dd * settings.pinchToWheelFactor;
      while (gs.pinchAccum >= 1.0) { sendWheel(dpy, +1, (int)std::lround(settings.wheelStep), true); gs.pinchAccum -= 1.0; }
      while (gs.pinchAccum <= -1.0) { sendWheel(dpy, -1, (int)std::lround(settings.wheelStep), true); gs.pinchAccum += 1.0; }
    }

    gs.lastCx = cx; gs.lastCy = cy;
    gs.lastPinchDist = d;
    XFlush(dpy);
  }

  [[noreturn]] void run() {
    int xfd = ConnectionNumber(dpy);
    if (xfd < 0) die("ConnectionNumber failed");

    pollfd fds[2]{};
    fds[0].fd = xfd;
    fds[0].events = POLLIN;
    fds[1].fd = inofd;
    fds[1].events = POLLIN;

    while (true) {
      int r = ::poll(fds, 2, -1);
      if (r < 0) continue;

      if (fds[1].revents & POLLIN) handle_inotify();
      if (fds[0].revents & POLLIN) drain_x_events();

      if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) die("X connection poll error");
      if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) die("inotify poll error");
    }
  }
};

int main(int argc, char** argv) {
  
  std::string config = "./current.conf";

  if (argc == 2) {
    config = argv[1];
    // std::fprintf(stderr, "usage: %s /path/to/default.conf\n", argv[0]);
    // return 2;
  }

  Daemon d;
  d.defaultConfPath = config;

  // Default mappings if no config loads yet
  d.mappings.map[EventType::TwoFingerDrag] = ActionType::PanMMBDrag;
  d.mappings.map[EventType::Pinch] = ActionType::ZoomCtrlWheel;

  d.setup_xi2();
  d.setup_inotify();

  // Initial config load
  d.apply_config_from_default_conf();

  std::fprintf(stderr,
    "touchwm_daemon_hotconfig running.\n"
    "Watching: %s\n"
    "Active JSON: %s\n",
    d.defaultConfPath.c_str(),
    d.activeJsonPath.empty() ? "(none)" : d.activeJsonPath.c_str()
  );

  d.run();
}
