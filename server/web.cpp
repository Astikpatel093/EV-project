// web.cpp - EV33 web server for hosting (for example on Render).
//
// Every page is generated here in C++: HTML for the layout, SVG for the map and the charts, CSS as
// a string below. There is no JavaScript: clicks are links and forms, and the live day plays by
// reloading the page once a second. The numbers come from the C engine through ../shared (the same
// layer the desktop app uses).
//
//   ev33_web [--port N] [--data DIR] [--results DIR] [--assets DIR]     (PORT in the environment wins)
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSESOCK close
#endif

#include "content.h"
#include "engine.h"
#include "fmt.h"

#ifndef EV33_SOURCE_DIR
#define EV33_SOURCE_DIR "."
#endif

using namespace ev33;

/* ======================================================================== small helpers */
static void appf(std::string &o, const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof buf) { o.append(buf, (size_t)n); return; }
    std::string big((size_t)n + 1, '\0');
    va_start(ap, fmt);
    std::vsnprintf(&big[0], big.size(), fmt, ap);
    va_end(ap);
    o.append(big.c_str(), (size_t)n);
}

static std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&#39;"; break;
        default: o += c;
        }
    }
    return o;
}

// **bold** -> <b>, newlines -> <br>, everything else escaped
static std::string md(const std::string &s) {
    std::string e = esc(s), o;
    bool b = false;
    for (size_t i = 0; i < e.size(); i++) {
        if (e.compare(i, 2, "**") == 0) { o += b ? "</b>" : "<b>"; b = !b; i++; continue; }
        if (e[i] == '\n') { o += "<br>"; continue; }
        o += e[i];
    }
    if (b) o += "</b>";
    return o;
}

static std::string urlenc(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

static std::string urldec(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else o += s[i];
    }
    return o;
}

/* ======================================================================== request state */
typedef std::map<std::string, std::string> Params;

struct Page {
    const char *path, *name, *title;
};
static const Page PAGES[] = {
    { "/", "Planner", "Planner" },
    { "/live", "Live day", "A day on the network" },
    { "/strategies", "Strategies", "Same city, fifteen strategies" },
    { "/capacity", "Capacity", "Ports and grid under load" },
    { "/algorithms", "Algorithms", "Every decision and the algorithm behind it" },
    { "/model", "Data model", "Records, trees and graphs" },
    { "/benchmarks", "Benchmarks", "How each algorithm scales" },
};
static const int NPAGES = (int)(sizeof PAGES / sizeof PAGES[0]);

// Keys carried from page to page so the plan follows the visitor.
static const char *CARRY[] = { "city", "sel", "route", "budget", "edit", "base", "policy", "traffic", "trip", "station", "t", "speed", "play", "metric", "family" };

struct Ctx {
    Engine &E;
    Params q;
    std::string path;
    std::string url(const std::string &page, const Params &over = {}) const {
        Params m;
        for (const char *k : CARRY) { auto it = q.find(k); if (it != q.end() && !it->second.empty()) m[k] = it->second; }
        for (auto &kv : over) { if (kv.second.empty()) m.erase(kv.first); else m[kv.first] = kv.second; }
        std::string u = page, sep = "?";
        for (auto &kv : m) { u += sep + kv.first + "=" + urlenc(kv.second); sep = "&"; }
        return esc(u);
    }
    std::string get(const char *k, const char *def = "") const { auto it = q.find(k); return it == q.end() ? def : it->second; }
};

static std::string editKey(const ev_scenario &sc, const Edits &e) {
    std::vector<std::string> t;
    for (int i : e.lock) t.push_back("l" + std::to_string(sc.stations[i].id));
    for (int i : e.ban) t.push_back("b" + std::to_string(sc.stations[i].id));
    for (auto &kv : e.ports) t.push_back("p" + std::to_string(sc.stations[kv.first].id) + "=" + std::to_string(kv.second));
    std::sort(t.begin(), t.end());
    std::string o;
    for (auto &s : t) { if (!o.empty()) o += ","; o += s; }
    return o;
}

static int stationIndex(const ev_scenario &sc, long id) {
    for (int i = 0; i < sc.ns; i++) if (sc.stations[i].id == id) return i;
    return -1;
}

static Edits parseEdits(const ev_scenario &sc, const std::string &s) {
    Edits e;
    size_t p = 0;
    int count = 0;
    while (p < s.size() && count < 64) {
        size_t c = s.find(',', p);
        std::string tok = s.substr(p, c == std::string::npos ? std::string::npos : c - p);
        p = c == std::string::npos ? s.size() : c + 1;
        if (tok.size() < 2) continue;
        int i = stationIndex(sc, std::atol(tok.c_str() + 1));
        if (i < 0) continue;
        count++;
        if (tok[0] == 'l') { e.ban.erase(i); e.lock.insert(i); }
        else if (tok[0] == 'b') { e.lock.erase(i); e.ban.insert(i); }
        else if (tok[0] == 'p') {
            size_t eq = tok.find('=');
            if (eq == std::string::npos) continue;
            int n = std::max(1, std::min(64, std::atoi(tok.c_str() + eq + 1)));
            if (n != sc.stations[i].ports) e.ports[i] = n;
        }
    }
    return e;
}

// sel:route:budget:edits
static std::string planCode(const ev_scenario &sc, const Plan &p) {
    return std::string(ev_sel_name((ev_sel_mode)p.sel)) + ":" + ev_route_name((ev_route_mode)p.route) + ":" + std::to_string(p.budget) + ":" + editKey(sc, p.edits);
}

static std::map<std::string, Baseline> g_baselines;   // keyed by city + plan code, keeps simulated waits

static void applyRequest(Ctx &c) {
    Engine &E = c.E;
    int city = 0;
    std::string cn = c.get("city", "medium");
    for (int i = 0; i < (int)E.cities.size(); i++) if (E.cities[(size_t)i].name == cn) city = i;
    if (!E.load(city)) city = 0;
    Plan p;
    p.city = city;
    const City &C = E.cities[(size_t)city];
    int s = ev_sel_parse(c.get("sel", "dp").c_str());
    p.sel = s >= 0 ? s : EV_SEL_DP;
    int r = ev_route_parse(c.get("route", "dijkstra").c_str());
    p.route = r >= 0 ? r : EV_ROUTE_DIJKSTRA;
    if (p.route == EV_ROUTE_FLOYD && C.sc.nn > 1600) p.route = EV_ROUTE_DIJKSTRA;
    std::string b = c.get("budget");
    p.budget = b.empty() ? C.default_budget : std::max(0, std::min(1000000, std::atoi(b.c_str())));
    p.edits = parseEdits(C.sc, c.get("edit"));
    E.plan = p;
    // baseline
    E.baseline = Baseline();
    std::string base = c.get("base");
    if (!base.empty()) {
        std::string key = C.name + "|" + base;
        auto it = g_baselines.find(key);
        if (it == g_baselines.end()) {
            Plan bp;
            bp.city = city;
            std::vector<std::string> f;
            size_t q = 0;
            for (int k = 0; k < 3; k++) { size_t col = base.find(':', q); f.push_back(base.substr(q, col == std::string::npos ? std::string::npos : col - q)); q = col == std::string::npos ? base.size() : col + 1; }
            std::string ed = q < base.size() ? base.substr(q) : "";
            int bs = ev_sel_parse(f[0].c_str()), br = ev_route_parse(f.size() > 1 ? f[1].c_str() : "");
            bp.sel = bs >= 0 ? bs : EV_SEL_DP;
            bp.route = br >= 0 ? br : EV_ROUTE_DIJKSTRA;
            bp.budget = f.size() > 2 && !f[2].empty() ? std::atoi(f[2].c_str()) : C.default_budget;
            bp.edits = parseEdits(C.sc, ed);
            Baseline B;
            B.set = true;
            B.plan = bp;
            B.kpi = E.kpiFor(bp, &B.cost, &B.stations);
            B.label = E.planLabel(bp);
            if (g_baselines.size() > 64) g_baselines.clear();
            it = g_baselines.emplace(key, B).first;
        }
        E.baseline = it->second;
    }
}

static void saveBaseline(Ctx &c) {   // keep simulated waits computed during this request
    if (!c.E.baseline.set) return;
    std::string key = c.E.city().name + "|" + c.get("base");
    g_baselines[key] = c.E.baseline;
}

/* ======================================================================== CSS */
static const char *CSS = R"CSS(
@font-face{font-family:Plex;src:url(/fonts/IBMPlexSans-Regular.ttf);font-weight:400}
@font-face{font-family:Plex;src:url(/fonts/IBMPlexSans-SemiBold.ttf);font-weight:600}
@font-face{font-family:Plex;src:url(/fonts/IBMPlexSans-Bold.ttf);font-weight:700}
@font-face{font-family:PlexMono;src:url(/fonts/IBMPlexMono-Regular.ttf)}
:root{--paper:#F2F4F0;--paper2:#E6EBE3;--paper3:#D9E0D5;--ink:#15233B;--ink2:#4A5970;--ink3:#7A879A;--line:#C6D0C3;--contour:#A7B7A2;
--cobalt:#2C55E8;--cobaltSoft:#D9E1FB;--amber:#B97710;--amberSoft:#F4E3C2;--red:#C2433A;--green:#2D7D53;--greenSoft:#D6EBDD;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--paper:#0E1722;--paper2:#132030;--paper3:#1B2C40;--ink:#E5EBF1;--ink2:#A3B0C0;--ink3:#74839A;--line:#26384C;
--contour:#35554A;--cobalt:#7E9CFF;--cobaltSoft:#1E2D57;--amber:#F0B54D;--amberSoft:#3A2C12;--red:#F2786C;--green:#62C792;--greenSoft:#15362A;color-scheme:dark}}
*{box-sizing:border-box}
body{margin:0;background:var(--paper);color:var(--ink);font:15px/1.5 Plex,"Helvetica Neue",Arial,sans-serif}
a{color:var(--cobalt)}
h1{font-size:1.6rem;margin:0 0 4px;line-height:1.15}h2{font-size:1.25rem;margin:22px 0 6px}
p{margin:0 0 10px}.muted{color:var(--ink3)}.small{font-size:.86rem}.lede{color:var(--ink2);max-width:72ch;margin-bottom:14px}
code,.mono{font-family:PlexMono,ui-monospace,Menlo,Consolas,monospace;font-size:.85em}
header.top{display:flex;align-items:center;gap:22px;flex-wrap:wrap;padding:10px max(16px,env(safe-area-inset-left));border-bottom:1px solid var(--line);background:var(--paper)}
.brand{display:flex;align-items:center;gap:8px;text-decoration:none;color:var(--ink);font-weight:700;font-size:1.1rem}.brand span{font-weight:400;color:var(--ink2)}
nav.tabs{display:flex;gap:4px;flex-wrap:wrap}nav.tabs a{padding:6px 12px;border-radius:6px;text-decoration:none;color:var(--ink2)}
nav.tabs a:hover{background:var(--paper2)}nav.tabs a.on{background:var(--paper2);color:var(--ink);font-weight:600;box-shadow:inset 0 -2px 0 var(--cobalt)}
.layout{display:grid;grid-template-columns:290px minmax(0,1fr);gap:22px;padding:16px max(16px,env(safe-area-inset-right)) 32px max(16px,env(safe-area-inset-left))}
aside{display:grid;gap:14px;align-content:start}aside h3{font-size:.95rem;margin:0 0 6px}
.cities{display:grid;gap:6px}.city{display:block;padding:7px 10px;border:1px solid var(--line);border-radius:6px;text-decoration:none;color:var(--ink);background:var(--paper)}
.city b{display:block}.city span{font-size:.8rem;color:var(--ink3)}.city.on{border-color:var(--cobalt);box-shadow:inset 3px 0 0 var(--cobalt);background:var(--cobaltSoft)}
.stack{display:grid;gap:2px}.opt{display:block;padding:5px 10px;border:1px solid var(--line);border-radius:5px;text-decoration:none;color:var(--ink);background:var(--paper)}
.opt.on{background:var(--ink);color:var(--paper);border-color:var(--ink)}.opt.off{color:var(--ink3);pointer-events:none;opacity:.6}
.seg{display:inline-flex;flex-wrap:wrap;border:1px solid var(--line);border-radius:6px;overflow:hidden}.seg a{padding:5px 12px;text-decoration:none;color:var(--ink);border-right:1px solid var(--line)}
.seg a:last-child{border-right:0}.seg a.on{background:var(--ink);color:var(--paper)}
form.inline{display:flex;gap:6px;align-items:center;flex-wrap:wrap}input,select,button{font:inherit;color:var(--ink);background:var(--paper);border:1px solid var(--line);border-radius:6px;padding:5px 8px}
button,.btn{cursor:pointer;background:var(--paper2);text-decoration:none;color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:5px 11px;display:inline-block}
.btn.primary{background:var(--cobalt);color:#fff;border-color:var(--cobalt)}
.chips{display:flex;flex-wrap:wrap;gap:6px}.chip{padding:3px 10px;border-radius:999px;background:var(--paper3);text-decoration:none;color:var(--ink);font-size:.85rem}
.chip.lock{background:var(--cobaltSoft)}.chip.ban{background:var(--amberSoft)}
.kpis{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:10px;margin-bottom:12px}.kpi{border:1px solid var(--line);border-radius:10px;padding:10px 12px;background:var(--paper)}
.kpi .t{font-size:.8rem;color:var(--ink3)}.kpi .v{font-size:1.55rem;font-weight:700;line-height:1.2}.kpi .s{font-size:.82rem;color:var(--ink2)}
.pill{display:inline-block;padding:1px 8px;border-radius:999px;font-size:.78rem;font-weight:600;background:var(--paper3);color:var(--ink2)}
.pill.up{background:var(--greenSoft);color:var(--green)}.pill.down{background:var(--amberSoft);color:var(--red)}.pill.ok{background:var(--greenSoft);color:var(--green)}.pill.bad{background:var(--amberSoft);color:var(--red)}
.row{display:grid;grid-template-columns:minmax(0,1fr) 360px;gap:16px;align-items:start}.panel{border:1px solid var(--line);border-radius:10px;padding:12px 14px;background:var(--paper)}
.map{width:100%;height:auto;display:block;border:1px solid var(--line);border-radius:10px;background:var(--paper)}
.legend{display:flex;flex-wrap:wrap;gap:4px 16px;font-size:.82rem;color:var(--ink2);margin:6px 2px}.legend i{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:5px;vertical-align:-1px}
.terrain{fill:var(--contour)}.contour{stroke:var(--contour);fill:none}.road{stroke:var(--ink3);stroke-opacity:.6;fill:none}
.tr0{stroke:var(--ink3);stroke-opacity:.25}.tr1{stroke:var(--cobalt);stroke-opacity:.4}.tr2{stroke:var(--cobalt);stroke-opacity:.75}.tr3{stroke:var(--cobalt)}.tr4{stroke:var(--red)}
.site{fill:var(--paper);stroke:var(--ink3);stroke-width:1.3}.built{fill:var(--cobalt)}.halo{fill:var(--cobalt);fill-opacity:.16}.short{fill:none;stroke:var(--amber);stroke-width:2.2}
.sel{fill:none;stroke:var(--ink);stroke-width:2}.lock{fill:none;stroke:var(--ink);stroke-width:1.6}.ban{stroke:var(--red);stroke-width:2.2}
.route{fill:none;stroke:var(--cobalt);stroke-width:3.4;stroke-linejoin:round}.route.bad{stroke:var(--red)}.start{fill:var(--paper);stroke:var(--cobalt);stroke-width:2.5}.end{fill:var(--ink)}
.badge{fill:var(--ink)}.badgeT{fill:var(--paper);font-size:11px;font-weight:700}.lbl{fill:var(--ink);font-size:11px}
.car{fill:var(--cobalt)}.car.low{fill:var(--amber)}.st{fill:var(--paper);stroke:var(--ink2);stroke-width:1.4}.occ{fill:none;stroke:var(--cobalt);stroke-width:3.2}.occ.full{stroke:var(--amber)}
.wait{fill:var(--amber)}.waitT{fill:var(--paper);font-size:10px;font-weight:700}
.axis{stroke:var(--line)}.grid{stroke:var(--line);stroke-opacity:.6}.tick{fill:var(--ink2);font-size:11px}.ln{fill:none;stroke-width:2}
.bar{fill:var(--cobalt)}.bar.over{fill:var(--amber)}.supply{stroke:var(--ink);stroke-dasharray:6 4;stroke-width:1.3}.now{stroke:var(--amber);stroke-width:2}
.elev{fill:var(--contour);fill-opacity:.35}.batt{fill:none;stroke:var(--cobalt);stroke-width:2.5}.reserve{stroke:var(--red);stroke-width:1.4}.target{stroke:var(--ink3);stroke-width:1}
table{border-collapse:collapse;width:100%}th,td{text-align:left;vertical-align:top;padding:7px 10px 7px 0;border-bottom:1px solid var(--line);font-size:.92rem}
thead th{font-size:.8rem;color:var(--ink3);font-weight:600;border-bottom:1px solid var(--ink3)}tr.fam th{background:var(--paper3);padding-left:8px}
.scroll{overflow-x:auto}.matrix td{padding:9px 12px}.matrix .v{font-weight:600;display:block}.matrix .s{font-size:.8rem}
.trips{display:grid;gap:2px;max-height:230px;overflow:auto;border:1px solid var(--line);border-radius:8px;padding:4px}
.trips a{display:grid;grid-template-columns:90px 1fr 70px;padding:3px 8px;border-radius:5px;text-decoration:none;color:var(--ink)}.trips a.on{background:var(--cobaltSoft)}
.trips .k{font-size:.8rem;color:var(--cobalt)}.trips .k.x{color:var(--red)}.trips .km{text-align:right;color:var(--ink3)}
ol.it{list-style:none;padding:0;margin:8px 0}ol.it li{display:grid;grid-template-columns:48px 1fr;gap:6px;font-size:.88rem;margin-bottom:4px}ol.it .tm{color:var(--ink3)}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:0;border:1px solid var(--line);border-radius:10px;overflow:hidden;margin-bottom:12px}
.stats>div{padding:8px 12px;border-bottom:1px solid var(--line)}.stats>div:nth-child(odd){border-right:1px solid var(--line)}.stats>div:nth-last-child(-n+2){border-bottom:0}.stats .t{font-size:.78rem;color:var(--ink3)}.stats .v{font-size:1.4rem;font-weight:700}.hot{color:var(--amber)}
.busy{display:grid;gap:6px}.busy .bar2{height:5px;border-radius:3px;background:var(--paper3);overflow:hidden}.busy .bar2 i{display:block;height:100%;background:var(--cobalt)}.busy .bar2 i.full{background:var(--amber)}
.clock{font-size:2.6rem;font-weight:700;line-height:1}.controls{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:8px 0}
.stbar{display:grid;grid-template-columns:100px 1fr 120px;gap:8px;align-items:center;font-size:.9rem}.stbar .b{height:10px;background:var(--paper2);border-radius:3px;position:relative;overflow:hidden}
.stbar .b i{position:absolute;top:0;bottom:0;background:var(--cobalt)}.stbar .b i.l{background:var(--amber)}.stbar .short{color:var(--red)}
.cards{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:12px}
details{margin-top:14px}pre{white-space:pre-wrap;font-family:PlexMono,ui-monospace,monospace;font-size:.8rem;background:var(--paper2);padding:12px;border-radius:8px}
footer{padding:16px max(16px,env(safe-area-inset-left)) 28px;color:var(--ink3);font-size:.82rem;border-top:1px solid var(--line)}
@media (max-width:1100px){.kpis{grid-template-columns:repeat(3,minmax(0,1fr))}.row{grid-template-columns:minmax(0,1fr)}.cards{grid-template-columns:repeat(2,minmax(0,1fr))}}
@media (max-width:820px){.layout{grid-template-columns:minmax(0,1fr)}main{order:-1}.kpis{grid-template-columns:repeat(2,minmax(0,1fr))}}
)CSS";

/* ======================================================================== map */
struct MapT {
    double minx, maxy, s, ox, oy;
    int W, H;
    double X(double x) const { return ox + (x - minx) * s; }
    double Y(double y) const { return oy + (maxy - y) * s; }
};

static MapT mapFor(const City &C, int W) {
    MapT m;
    double dw = std::max(1e-6, C.maxx - C.minx), dh = std::max(1e-6, C.maxy - C.miny);
    m.W = W;
    m.H = (int)std::max(260.0, std::min(760.0, W * dh / dw));
    double pad = 18;
    m.s = std::min((W - 2 * pad) / dw, (m.H - 2 * pad) / dh);
    m.minx = C.minx;
    m.maxy = C.maxy;
    m.ox = (W - dw * m.s) / 2;
    m.oy = (m.H - dh * m.s) / 2;
    return m;
}

static void mapBase(std::string &o, const City &C, const MapT &m, const Run *R, bool traffic) {
    const ev_scenario &sc = C.sc;
    const Terrain &T = C.terrain;
    double dw = C.maxx - C.minx, dh = C.maxy - C.miny;
    if (T.ok) {
        // terrain tint on a coarser grid
        int g = 3, gw = T.gw / g, gh = T.gh / g;
        o += "<g class=\"terrain\">";
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                double v = 0;
                for (int yy = 0; yy < g; yy++) for (int xx = 0; xx < g; xx++) v += T.V[(size_t)((y * g + yy) * T.gw + x * g + xx)];
                v /= g * g;
                double t = (v - T.zmin) / std::max(1.0f, T.zmax - T.zmin);
                double x0 = m.X(C.minx + (double)x / gw * dw), y0 = m.Y(C.maxy - (double)y / gh * dh);
                double cw = dw / gw * m.s, ch = dh / gh * m.s;
                appf(o, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill-opacity=\"%.2f\"/>", x0, y0, cw + 0.6, ch + 0.6, 0.06 + 0.5 * t);
            }
        o += "</g>";
        for (const auto &lev : T.levels) {
            appf(o, "<path class=\"contour\" stroke-width=\"%s\" stroke-opacity=\"%s\" d=\"", lev.major ? "1.1" : "0.6", lev.major ? "0.95" : "0.6");
            for (size_t k = 0; k + 3 < lev.segs.size(); k += 4)
                appf(o, "M%.0f %.0fL%.0f %.0f", m.X(lev.segs[k]), m.Y(lev.segs[k + 1]), m.X(lev.segs[k + 2]), m.Y(lev.segs[k + 3]));
            o += "\"/>";
        }
    }
    if (traffic && R) {
        std::string buckets[5];
        double wid[5] = { 1.0, 2.0, 3.4, 4.8, 6.5 };
        for (int e = 0; e < sc.ne; e++) {
            double r = (double)R->edge_load[(size_t)e] / R->edge_load_max;
            int b = r <= 0 ? 0 : r < 0.25 ? 1 : r < 0.5 ? 2 : r < 0.75 ? 3 : 4;
            const ev_node &a = sc.nodes[sc.edges[e].u], &n = sc.nodes[sc.edges[e].v];
            appf(buckets[b], "M%.1f %.1fL%.1f %.1f", m.X(a.x), m.Y(a.y), m.X(n.x), m.Y(n.y));
        }
        for (int b = 0; b < 5; b++)
            if (!buckets[b].empty()) appf(o, "<path class=\"tr%d\" fill=\"none\" stroke-linecap=\"round\" stroke-width=\"%.1f\" d=\"%s\"/>", b, wid[b], buckets[b].c_str());
    } else {
        o += "<path class=\"road\" stroke-width=\"1.1\" d=\"";
        for (int e = 0; e < sc.ne; e++) {
            const ev_node &a = sc.nodes[sc.edges[e].u], &n = sc.nodes[sc.edges[e].v];
            appf(o, "M%.1f %.1fL%.1f %.1f", m.X(a.x), m.Y(a.y), m.X(n.x), m.Y(n.y));
        }
        o += "\"/>";
    }
}

/* ======================================================================== page frame */
static std::string frame(Ctx &c, int page, const std::string &body, const std::string &head_extra = "") {
    Engine &E = c.E;
    std::string o;
    o += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    appf(o, "<title>EV33 %s</title>", PAGES[page].name);
    o += "<link rel=\"icon\" href=\"data:,\">";
    o += head_extra;
    o += "<style>";
    o += CSS;
    o += "</style></head><body><header class=\"top\"><a class=\"brand\" href=\"" + c.url("/") + "\">"
         "<svg width=\"28\" height=\"28\" viewBox=\"0 0 32 32\" aria-hidden=\"true\"><path d=\"M4 22c5-9 9-12 12-12s6 3 12-6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\"/><circle cx=\"16\" cy=\"10\" r=\"4.2\" fill=\"var(--cobalt)\"/></svg>"
         "EV33 <span>Charge Atlas</span></a><nav class=\"tabs\">";
    for (int i = 0; i < NPAGES; i++) o += "<a href=\"" + c.url(PAGES[i].path) + "\"" + (i == page ? " class=\"on\"" : "") + ">" + PAGES[i].name + "</a>";
    o += "</nav></header><div class=\"layout\"><aside>";

    // ---- the plan
    const City &C = E.city();
    const ev_scenario &sc = C.sc;
    std::string here = PAGES[page].path;
    o += "<div><h3>City</h3><div class=\"cities\">";
    for (int i = 0; i < (int)E.cities.size(); i++) {
        City &ci = E.cities[(size_t)i];
        E.load(i);
        o += "<a class=\"city" + std::string(i == E.plan.city ? " on" : "") + "\" title=\"" + esc(ci.blurb) + "\" href=\"" +
             c.url(here, { { "city", ci.name }, { "budget", "" }, { "edit", "" }, { "base", "" }, { "trip", "" }, { "station", "" } }) + "\"><b>" + esc(ci.label) + "</b><span>" +
             fint(ci.sc.nn) + " junctions, " + fint(ci.sc.ns) + " sites, " + fint(ci.sc.nt) + " trips</span></a>";
    }
    o += "</div></div><div><h3>Site selection</h3><div class=\"stack\">";
    for (int k = 0; k < EV_SEL_COUNT; k++) {
        int m = SEL_ORDER[k];
        o += "<a class=\"opt" + std::string(m == E.plan.sel ? " on" : "") + "\" href=\"" + c.url(here, { { "sel", ev_sel_name((ev_sel_mode)m) } }) + "\">" + SELECT_LONG[m] + "</a>";
    }
    o += "</div></div><div><h3>Routing</h3><div class=\"stack\">";
    for (int r = 0; r < EV_ROUTE_COUNT; r++) {
        bool off = r == EV_ROUTE_FLOYD && sc.nn > 1600;
        o += "<a class=\"opt" + std::string(r == E.plan.route ? " on" : "") + (off ? " off" : "") + "\" href=\"" + c.url(here, { { "route", ev_route_name((ev_route_mode)r) } }) + "\">" + ROUTE_LABEL[r] + "</a>";
    }
    o += "</div><p class=\"small muted\">" + esc(ROUTE_HINT[E.plan.route]) + "</p></div>";
    // budget
    o += "<div><h3>Installation budget: " + esc(rupees(E.plan.budget)) + "</h3><form class=\"inline\" method=\"get\" action=\"" + here + "\">";
    for (const char *k : CARRY) {
        if (!std::strcmp(k, "budget")) continue;
        std::string v = c.get(k);
        if (!v.empty()) o += "<input type=\"hidden\" name=\"" + std::string(k) + "\" value=\"" + esc(v) + "\">";
    }
    appf(o, "<input type=\"number\" name=\"budget\" min=\"0\" max=\"1000000\" step=\"10\" value=\"%d\" style=\"width:110px\"> <button>Apply</button></form>", E.plan.budget);
    int step = std::max(10, (C.default_budget / 10 + 5) / 10 * 10);
    o += "<p class=\"small\"><a href=\"" + c.url(here, { { "budget", std::to_string(std::max(0, E.plan.budget - step)) } }) + "\">\xE2\x88\x92" + std::to_string(step) + "</a> \xC2\xB7 <a href=\"" +
         c.url(here, { { "budget", "" } }) + "\">default " + esc(rupees(C.default_budget)) + "</a> \xC2\xB7 <a href=\"" + c.url(here, { { "budget", std::to_string(E.plan.budget + step) } }) + "\">+" + std::to_string(step) + "</a></p></div>";
    // edits
    if (!E.plan.edits.empty()) {
        o += "<div><h3>What-if edits <a class=\"small\" href=\"" + c.url(here, { { "edit", "" } }) + "\">clear all</a></h3><div class=\"chips\">";
        auto chip = [&](const char *cls, const std::string &label, const Edits &without) {
            o += "<a class=\"chip " + std::string(cls) + "\" title=\"Remove this edit\" href=\"" + c.url(here, { { "edit", editKey(sc, without) } }) + "\">" + esc(label) + " \xC3\x97</a>";
        };
        for (int i : E.plan.edits.lock) { Edits w = E.plan.edits; w.lock.erase(i); chip("lock", "Always build " + std::to_string(sc.stations[i].id), w); }
        for (int i : E.plan.edits.ban) { Edits w = E.plan.edits; w.ban.erase(i); chip("ban", "Never build " + std::to_string(sc.stations[i].id), w); }
        for (auto &kv : E.plan.edits.ports) { Edits w = E.plan.edits; w.ports.erase(kv.first); chip("", "Station " + std::to_string(sc.stations[kv.first].id) + ": " + std::to_string(kv.second) + " ports", w); }
        o += "</div></div>";
    }
    o += "<p class=\"small muted\">" + esc(E.status) + "</p></aside><main>";
    o += body;
    o += "</main></div><footer>EV33, Project 33. Engine in C11; this site is generated by a C++ server on that engine (no JavaScript). "
         "The same engine also runs as a desktop app (ev33_gui).</footer></body></html>";
    return o;
}

/* ======================================================================== planner */
static std::string pagePlanner(Ctx &c) {
    Engine &E = c.E;
    Run *R = E.run();
    if (!R) return "<p>The engine could not load this city.</p>";
    const City &C = E.city();
    const ev_scenario &sc = C.sc;
    const ev_kpi &k = R->run.kpi;
    const ev_selection &s = R->run.sel;
    const Baseline &BL = E.baseline;
    std::string o = "<h1>Planner</h1><p class=\"lede\">Choose a plan in the sidebar. The map shows the sites the engine builds; click a site to force it in, rule it out or change its ports. Pick a trip to replay it.</p>";

    auto delta = [&](double d, bool higher, const std::string &unit, int digits) {
        if (!BL.set) return std::string();
        double v = std::fabs(d) < std::pow(10.0, -digits) / 2 ? 0 : d;
        if (v == 0) return std::string(" <span class=\"pill\">same as baseline</span>");
        bool good = (v > 0) == higher;
        return std::string(" <span class=\"pill ") + (good ? "up" : "down") + "\">" + (v > 0 ? "+" : "\xE2\x88\x92") + (digits ? f2(std::fabs(v)) : fint(std::fabs(v))) + esc(unit) + " vs baseline</span>";
    };
    auto kpi = [&](const char *t, const std::string &v, const std::string &sub) {
        o += "<div class=\"kpi\"><div class=\"t\">" + std::string(t) + "</div><div class=\"v\">" + esc(v) + "</div><div class=\"s\">" + sub + "</div></div>";
    };
    o += "<div class=\"kpis\">";
    kpi("Trips completed", f1(k.feasible_pct) + "%", esc(fint(k.feasible) + " of " + fint(k.trips) + " trips") + delta(k.feasible - BL.kpi.feasible, true, " trips", 0));
    kpi("Needed a charge and got one", fint(k.charged_ok), esc("of " + fint(k.need_charge) + " that could not finish on their starting charge") + delta(k.charged_ok - BL.kpi.charged_ok, true, "", 0));
    kpi("Stations built", fint(s.n), esc(rupees(s.cost) + " of " + rupees(E.plan.budget) + (s.optimal ? ", provably optimal" : "") + (s.over_budget ? ". Locked sites exceed the budget." : "")) +
                                          delta((double)(s.cost - BL.cost), false, " lakh", 0));
    kpi("Average trip", f1(k.avg_km) + " km", esc(f2(k.avg_kwh) + " kWh, " + f2(k.avg_stops) + " stops, " + f1(k.avg_charge_min) + " min charging") + delta(k.avg_kwh - BL.kpi.avg_kwh, false, " kWh", 2));
    kpi("Engine time", fms(R->total_ms), esc(fint((double)ev_router_sssp_runs(R->run.router)) + " shortest-path trees"));
    o += "</div><p class=\"controls\">";
    if (BL.set) o += "<a class=\"btn\" href=\"" + c.url("/", { { "base", "" } }) + "\">Clear baseline</a> <span class=\"muted\">Baseline: " + esc(BL.label) + ".</span>";
    else o += "<a class=\"btn\" href=\"" + c.url("/", { { "base", planCode(sc, E.plan) } }) + "\">Pin as baseline</a> <span class=\"muted\">Pin a plan to compare later runs against it.</span>";
    o += " <a class=\"btn\" href=\"" + c.url("/sites.csv") + "\">Download built sites (CSV)</a> ";
    bool traffic = c.get("traffic") == "1";
    o += "<a class=\"btn\" href=\"" + c.url("/", { { "traffic", traffic ? "" : "1" } }) + "\">" + (traffic ? "Hide traffic" : "Show traffic") + "</a></p>";

    // which trip and station
    int trip = -1;
    std::string tq = c.get("trip");
    if (!tq.empty()) for (int i = 0; i < sc.nt; i++) if (sc.trips[i].id == std::atoi(tq.c_str())) trip = i;
    if (trip < 0 && !R->samples.empty()) trip = R->samples[0].trip;
    int station = c.get("station").empty() ? -1 : stationIndex(sc, std::atol(c.get("station").c_str()));

    // map
    MapT m = mapFor(C, 900);
    std::string svg;
    appf(svg, "<svg class=\"map\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"Road network with candidate and built charging sites\">", m.W, m.H);
    mapBase(svg, C, m, R, traffic);
    const TripDetail *D = trip >= 0 ? &E.trip(trip) : nullptr;
    if (D && D->valid && D->path.size() >= 2) {
        svg += std::string("<polyline class=\"route") + (D->plan.feasible ? "" : " bad") + "\" points=\"";
        for (int v : D->path) appf(svg, "%.1f,%.1f ", m.X(sc.nodes[v].x), m.Y(sc.nodes[v].y));
        svg += "\"/>";
        const ev_node &a = sc.nodes[D->path.front()], &b = sc.nodes[D->path.back()];
        appf(svg, "<circle class=\"start\" cx=\"%.1f\" cy=\"%.1f\" r=\"6\"/><circle class=\"end\" cx=\"%.1f\" cy=\"%.1f\" r=\"6\"/>", m.X(a.x), m.Y(a.y), m.X(b.x), m.Y(b.y));
    }
    for (int i = 0; i < sc.ns; i++) {
        const ev_node &n = sc.nodes[sc.stations[i].node];
        double x = m.X(n.x), y = m.Y(n.y);
        bool built = R->built[(size_t)i];
        svg += "<a href=\"" + c.url("/", { { "station", std::to_string(sc.stations[i].id) } }) + "\"><title>Station " + std::to_string(sc.stations[i].id) + ", " +
               esc(sc.nodes[sc.stations[i].node].name) + ": " + std::to_string(E.effectivePorts(i)) + " ports, " + esc(rupees(sc.stations[i].cost)) + (built ? ", built" : ", not built") + "</title>";
        if (built) {
            double r = 3.8 + std::sqrt((double)E.effectivePorts(i)) * 1.5;
            appf(svg, "<circle class=\"halo\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/><circle class=\"built\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", x, y, r + 4, x, y, r);
            if (R->short_of[(size_t)i] > 0) appf(svg, "<circle class=\"short\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", x, y, r + 2.5);
        } else appf(svg, "<circle class=\"site\" cx=\"%.1f\" cy=\"%.1f\" r=\"4\"/><circle cx=\"%.1f\" cy=\"%.1f\" r=\"9\" fill=\"transparent\"/>", x, y, x, y);
        svg += "</a>";
        if (E.plan.edits.lock.count(i)) appf(svg, "<rect class=\"lock\" x=\"%.1f\" y=\"%.1f\" width=\"18\" height=\"18\" rx=\"2\"/>", x - 9, y - 9);
        if (E.plan.edits.ban.count(i)) appf(svg, "<path class=\"ban\" d=\"M%.1f %.1fL%.1f %.1fM%.1f %.1fL%.1f %.1f\"/>", x - 6, y - 6, x + 6, y + 6, x - 6, y + 6, x + 6, y - 6);
        if (E.plan.edits.ports.count(i)) appf(svg, "<text class=\"lbl\" x=\"%.1f\" y=\"%.1f\">%dp</text>", x + 9, y - 10, E.effectivePorts(i));
        if (i == station) appf(svg, "<circle class=\"sel\" cx=\"%.1f\" cy=\"%.1f\" r=\"13\"/>", x, y);
    }
    if (D && D->valid)
        for (int q = 0; q < D->plan.n_stops; q++) {
            const ev_node &n = sc.nodes[sc.stations[D->plan.stop[q]].node];
            appf(svg, "<circle class=\"badge\" cx=\"%.1f\" cy=\"%.1f\" r=\"8\"/><text class=\"badgeT\" x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\">%d</text>", m.X(n.x) - 12,
                 m.Y(n.y) - 12, m.X(n.x) - 12, m.Y(n.y) - 8, q + 1);
        }
    svg += "</svg>";

    o += "<div class=\"row\"><div>" + svg;
    o += std::string("<div class=\"legend\">") + (traffic ? "<span><i style=\"background:var(--red);border-radius:2px\"></i>Busier road</span>" : "<span><i style=\"background:var(--ink3);height:2px;border-radius:0\"></i>Road</span>") +
         "<span><i style=\"border:1.5px solid var(--ink3)\"></i>Candidate site</span><span><i style=\"background:var(--cobalt)\"></i>Built station</span>"
         "<span><i style=\"background:var(--cobalt);box-shadow:0 0 0 2px var(--amber)\"></i>Short of chargers</span><span><i style=\"background:var(--contour);height:2px;border-radius:0\"></i>Contour, 50 m</span></div>";

    // station panel
    if (station >= 0) {
        const ev_station &st = sc.stations[station];
        bool built = R->built[(size_t)station];
        int mode = E.plan.edits.lock.count(station) ? 1 : E.plan.edits.ban.count(station) ? 2 : 0;
        int ports = E.effectivePorts(station);
        o += "<div class=\"panel\"><h2 style=\"margin-top:0\">Station " + std::to_string(st.id) + " <span class=\"pill\">" +
             (built ? (mode == 1 ? "Built, by your edit" : "Built by the engine") : mode == 2 ? "Never built, by your edit" : "Not built") + "</span> <a class=\"small\" href=\"" +
             c.url("/", { { "station", "" } }) + "\">close</a></h2><p class=\"muted\">Junction " + esc(sc.nodes[st.node].name) + "</p><table><tbody>";
        auto row = [&](const char *k2, const std::string &v) { o += "<tr><th>" + std::string(k2) + "</th><td><b>" + esc(v) + "</b></td></tr>"; };
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d at %g kW", ports, st.power_kw);
        row("Ports", buf);
        row("Cost", rupees(st.cost));
        row("Service value", fint(st.value));
        for (int q = 0; q < R->ses.n; q++)
            if (R->ses.st[q].station == station)
                row("Sessions", fint(R->ses.st[q].sessions) + ", needs " + std::to_string(R->ses.st[q].chargers_needed) + (R->ses.st[q].chargers_needed == 1 ? " charger" : " chargers"));
        if (built) {
            SimView *sv = E.sim(0);
            auto it = sv ? sv->local.find(station) : std::map<int, int>::iterator();
            if (sv && it != sv->local.end()) {
                const ev_sim_station &ss = sv->sim.st[it->second];
                row("Queueing", ss.waited ? fint(ss.waited) + " drivers, " + fdur(ss.wait_min) + " in all" : "nobody waits");
            }
        }
        o += "</tbody></table><p class=\"controls\"><span class=\"seg\">";
        static const char *ml[] = { "Engine decides", "Always build", "Never build" };
        for (int mm = 0; mm < 3; mm++) {
            Edits w = E.plan.edits;
            w.lock.erase(station);
            w.ban.erase(station);
            if (mm == 1) w.lock.insert(station);
            if (mm == 2) w.ban.insert(station);
            o += "<a" + std::string(mm == mode ? " class=\"on\"" : "") + " href=\"" + c.url("/", { { "edit", editKey(sc, w) } }) + "\">" + ml[mm] + "</a>";
        }
        o += "</span></p><p class=\"controls\">Ports: ";
        auto portsLink = [&](int n, const char *label) {
            Edits w = E.plan.edits;
            n = std::max(1, std::min(64, n));
            if (n == E.basePorts(station)) w.ports.erase(station); else w.ports[station] = n;
            o += "<a class=\"btn\" href=\"" + c.url("/", { { "edit", editKey(sc, w) } }) + "\">" + label + "</a>";
        };
        portsLink(ports - 1, "\xE2\x88\x92");
        o += " <b>" + std::to_string(ports) + " ports</b> ";
        portsLink(ports + 1, "+");
        o += "</p></div>";
    }
    o += "</div><div>";

    // trip inspector
    o += "<h2 style=\"margin-top:0\">Trip inspector</h2><div class=\"trips\">";
    for (auto &smp : R->samples) {
        const char *kind = smp.kind == 2 ? "stops" : smp.kind == 1 ? "1 stop" : smp.kind == 0 ? "Direct" : "Not possible";
        std::string badge = smp.kind == 2 ? std::to_string(smp.stops) + " " + kind : kind;
        o += "<a" + std::string(smp.trip == trip ? " class=\"on\"" : "") + " href=\"" + c.url("/", { { "trip", std::to_string(sc.trips[smp.trip].id) } }) + "\"><span class=\"k" +
             (smp.kind == -1 ? " x" : "") + "\">" + badge + "</span><span>Trip " + std::to_string(sc.trips[smp.trip].id) + "</span><span class=\"km\">" + f1(smp.km) + " km</span></a>";
    }
    if (R->samples.empty()) o += "<p class=\"muted\">This plan has no trips to replay.</p>";
    o += "</div>";
    if (D && D->valid) {
        const ev_trip &tr = sc.trips[D->trip];
        o += "<h3>Trip " + std::to_string(tr.id) + ": " + esc(sc.nodes[tr.src].name) + " to " + esc(sc.nodes[tr.dst].name) + "</h3><p class=\"small\">" + md(tripFacts(sc, *D)) + "</p>";
        // battery chart
        double B = sc.cfg.battery_kwh, total = D->profile.empty() ? 1 : std::max(1e-6, D->profile.back()[0]);
        double emin = 1e9, emax = -1e9;
        for (auto &p : D->profile) { emin = std::min(emin, p[2]); emax = std::max(emax, p[2]); }
        if (emax <= emin) emax = emin + 1;
        int W = 360, H = 190, l = 34, r = 10, t = 10, b = 26;
        auto X = [&](double v) { return l + v / total * (W - l - r); };
        auto Y = [&](double v) { return t + (1 - v / B) * (H - t - b); };
        auto YE = [&](double v) { return t + (1 - (v - emin) / ((emax - emin) * 2.5)) * (H - t - b); };
        appf(o, "<svg viewBox=\"0 0 %d %d\" width=\"100%%\" role=\"img\" aria-label=\"Battery and elevation along the trip\">", W, H);
        for (int g = 0; g <= 3; g++) appf(o, "<line class=\"grid\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tick\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%g</text>", l, W - r, Y(B * g / 3), Y(B * g / 3), l - 4, Y(B * g / 3) + 4, B * g / 3);
        o += "<path class=\"elev\" d=\"M" + std::to_string(l) + " " + std::to_string(H - b);
        for (auto &p : D->profile) appf(o, "L%.1f %.1f", X(p[0]), YE(p[2]));
        appf(o, "L%d %dZ\"/>", W - r, H - b);
        appf(o, "<line class=\"reserve\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><line class=\"target\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/>", l, W - r,
             Y(sc.cfg.reserve_frac * B), Y(sc.cfg.reserve_frac * B), l, W - r, Y(sc.cfg.target_frac * B), Y(sc.cfg.target_frac * B));
        o += "<polyline class=\"batt\" points=\"";
        for (auto &p : D->profile) appf(o, "%.1f,%.1f ", X(p[0]), Y(std::max(0.0, p[1])));
        o += "\"/>";
        for (int g = 0; g <= 4; g++)
            appf(o, "<text class=\"tick\" x=\"%.1f\" y=\"%d\" text-anchor=\"%s\">%.0f%s</text>", X(total * g / 4), H - 8, g == 4 ? "end" : "middle", total * g / 4, g == 4 ? " km" : "");
        o += "</svg>";
        o += "<p class=\"small muted\">Blue: battery (kWh). Red: reserve. Grey: charge target. Shaded: elevation.</p><ol class=\"it\">";
        for (auto &st : tripItinerary(sc, *D)) o += "<li><span class=\"tm\">" + esc(st.first) + "</span><span>" + md(st.second) + "</span></li>";
        o += "</ol>";
    }
    o += "</div></div>";
    return o;
}

/* ======================================================================== live day */
static std::string pageLive(Ctx &c, std::string &head) {
    Engine &E = c.E;
    Run *R = E.run();
    int policy = c.get("policy") == "divert" ? 1 : 0;
    SimView *sv = E.sim(policy);
    if (!R || !sv) return "<p>The engine could not run this plan.</p>";
    const City &C = E.city();
    const ev_scenario &sc = C.sc;
    std::string ts = c.get("t");
    double T = 510;
    if (!ts.empty()) {
        size_t colon = ts.find(':');
        T = colon == std::string::npos ? std::atof(ts.c_str()) : std::atoi(ts.c_str()) * 60.0 + std::atof(ts.c_str() + colon + 1);
    }
    if (!(T >= 0)) T = 0;
    if (T > sv->end_min) T = std::fmod(T, sv->end_min);
    double speed = c.get("speed").empty() ? 10 : std::atof(c.get("speed").c_str());
    if (!(speed > 0) || speed > 240) speed = 10;
    bool play = c.get("play") == "1";
    auto tstr = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); std::string s = b; while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); return s; };
    if (play) {
        double nt = T + speed;
        if (nt > sv->end_min) nt = 0;
        head += "<meta http-equiv=\"refresh\" content=\"1;url=" + c.url("/live", { { "t", tstr(nt) } }) + "\">";
    }
    LiveState st;
    sv->at(T, false, st);

    std::string o = "<h1>A day on the network</h1><p class=\"lede\">The engine replays the whole day with the ports actually installed. When every port at a station is busy, drivers queue and the rest of their trip runs late.</p>";
    o += "<div class=\"row\"><div>";
    // controls
    int day = (int)std::floor(T / 1440) + 1;
    o += "<div class=\"controls\"><span class=\"clock\">" + fclock(T) + "</span><span class=\"muted\">" + (day > 1 ? "Day " + std::to_string(day) + ": queues from yesterday" : std::string("Day 1")) + "</span> ";
    o += "<a class=\"btn primary\" href=\"" + c.url("/live", { { "play", play ? "" : "1" }, { "t", tstr(T) } }) + "\">" + (play ? "Pause" : "Play") + "</a> ";
    o += "<a class=\"btn\" href=\"" + c.url("/live", { { "t", tstr(std::max(0.0, T - 60)) }, { "play", "" } }) + "\">\xE2\x88\x92" "1 h</a> ";
    o += "<a class=\"btn\" href=\"" + c.url("/live", { { "t", tstr(std::max(0.0, T - 10)) }, { "play", "" } }) + "\">\xE2\x88\x92" "10 min</a> ";
    o += "<a class=\"btn\" href=\"" + c.url("/live", { { "t", tstr(std::min(sv->end_min, T + 10)) }, { "play", "" } }) + "\">+10 min</a> ";
    o += "<a class=\"btn\" href=\"" + c.url("/live", { { "t", tstr(std::min(sv->end_min, T + 60)) }, { "play", "" } }) + "\">+1 h</a> ";
    {
        std::time_t now = std::time(nullptr);
        std::tm *lt = std::localtime(&now);
        double nowm = lt->tm_hour * 60 + lt->tm_min + lt->tm_sec / 60.0;
        o += "<a class=\"btn\" href=\"" + c.url("/live", { { "t", tstr(nowm) }, { "speed", tstr(1.0 / 60) }, { "play", "1" } }) + "\">Now</a></div>";
    }
    o += "<div class=\"controls\">Speed <span class=\"seg\">";
    static const double SP[] = { 1.0 / 60, 1, 10, 60 };
    static const char *SPL[] = { "Real time", "1 min/s", "10 min/s", "1 h/s" };
    for (int k = 0; k < 4; k++) o += "<a" + std::string(std::fabs(speed - SP[k]) < 1e-6 ? " class=\"on\"" : "") + " href=\"" + c.url("/live", { { "speed", tstr(SP[k]) }, { "t", tstr(T) } }) + "\">" + SPL[k] + "</a>";
    o += "</span> <form class=\"inline\" method=\"get\" action=\"/live\" style=\"display:inline-flex\">";
    for (const char *k : CARRY) {
        if (!std::strcmp(k, "t") || !std::strcmp(k, "play")) continue;
        std::string v = c.get(k);
        if (!v.empty()) o += "<input type=\"hidden\" name=\"" + std::string(k) + "\" value=\"" + esc(v) + "\">";
    }
    o += "<input name=\"t\" placeholder=\"time, e.g. 18:30\" style=\"width:130px\"> <button>Go</button></form></div>";

    // map
    MapT m = mapFor(C, 900);
    appf(o, "<svg class=\"map\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"Vehicles and station occupancy at %s\">", m.W, m.H, fclock(T).c_str());
    mapBase(o, C, m, R, false);
    for (int k = 0; k < sv->sim.nst; k++) {
        int s = sv->sim.st[k].station;
        const ev_node &n = sc.nodes[sc.stations[s].node];
        double x = m.X(n.x), y = m.Y(n.y), r = 7.5;
        int ports = R->ports[(size_t)s], busy = st.charging[(size_t)k], wait = st.waiting[(size_t)k];
        appf(o, "<g><title>Station %d: %d of %d ports busy, %d waiting</title><circle class=\"st\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", sc.stations[s].id, busy, ports, wait, x, y, r);
        if (busy > 0) {
            double f = std::min(1.0, (double)busy / std::max(1, ports)), rr = r + 3.2;
            if (f >= 0.999) appf(o, "<circle class=\"occ full\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", x, y, rr);
            else {
                double a = -M_PI / 2 + 2 * M_PI * f;
                appf(o, "<path class=\"occ\" d=\"M%.1f %.1fA%.1f %.1f 0 %d 1 %.1f %.1f\"/>", x, y - rr, rr, rr, f > 0.5 ? 1 : 0, x + rr * std::cos(a), y + rr * std::sin(a));
            }
        }
        if (wait > 0) appf(o, "<rect class=\"wait\" x=\"%.1f\" y=\"%.1f\" width=\"%d\" height=\"16\" rx=\"8\"/><text class=\"waitT\" x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\">%d</text>",
                           x + r + 2, y - r - 12, wait >= 10 ? 22 : 16, x + r + 2 + (wait >= 10 ? 11 : 8), y - r - 0.5, wait);
        o += "</g>";
    }
    for (int v = 0; v < st.ncars; v++)
        appf(o, "<circle class=\"car%s\" cx=\"%.1f\" cy=\"%.1f\" r=\"2.6\"/>", st.cars[(size_t)v * 3 + 2] < 0.3f ? " low" : "", m.X(st.cars[(size_t)v * 3]), m.Y(st.cars[(size_t)v * 3 + 1]));
    o += "</svg><div class=\"legend\"><span><i style=\"background:var(--cobalt)\"></i>Vehicle</span><span><i style=\"background:var(--amber)\"></i>Battery under 30%</span>"
         "<span><i style=\"border:3px solid var(--cobalt)\"></i>Ports in use</span><span><i style=\"background:var(--amber);border-radius:4px\"></i>Drivers waiting</span></div>";

    // grid chart (click an hour to jump)
    {
        int W = 900, H = 170, l = 46, r = 10, t = 10, b = 26;
        double grid = sc.cfg.grid_kw, ymax = grid * 1.15;
        for (double v : sv->series_kw) ymax = std::max(ymax, v * 1.1);
        double hours = sv->end_min / 60;
        auto X = [&](double h) { return l + h / hours * (W - l - r); };
        auto Y = [&](double v) { return t + (1 - v / ymax) * (H - t - b); };
        appf(o, "<svg viewBox=\"0 0 %d %d\" width=\"100%%\" role=\"img\" aria-label=\"Charging power drawn from the grid over the day\">", W, H);
        for (int g = 0; g <= 3; g++) appf(o, "<line class=\"grid\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tick\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(ymax * g / 3), Y(ymax * g / 3), l - 4, Y(ymax * g / 3) + 4, fint(ymax * g / 3).c_str());
        o += "<path class=\"elev\" style=\"fill:var(--cobalt);fill-opacity:.18\" d=\"M" + std::to_string(l) + " " + std::to_string(H - b);
        for (size_t i = 0; i < sv->series_t.size(); i++) appf(o, "L%.1f %.1f", X(sv->series_t[i]), Y(sv->series_kw[i]));
        appf(o, "L%.1f %dZ\"/>", X(sv->series_t.back()), H - b);
        o += "<polyline class=\"ln\" style=\"stroke:var(--cobalt)\" points=\"";
        for (size_t i = 0; i < sv->series_t.size(); i++) appf(o, "%.1f,%.1f ", X(sv->series_t[i]), Y(sv->series_kw[i]));
        o += "\"/>";
        appf(o, "<line class=\"supply\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tick\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">grid supply %s kW</text>", l, W - r, Y(grid), Y(grid), W - r, Y(grid) - 5, fint(grid).c_str());
        appf(o, "<line class=\"now\" x1=\"%.1f\" x2=\"%.1f\" y1=\"%d\" y2=\"%d\"/>", X(T / 60), X(T / 60), t, H - b);
        for (int h = 0; h < (int)hours; h++) {
            if (h % 3 == 0) appf(o, "<text class=\"tick\" x=\"%.1f\" y=\"%d\" text-anchor=\"middle\">%02d:00</text>", X(h), H - 8, h % 24);
            o += "<a href=\"" + c.url("/live", { { "t", std::to_string(h * 60) }, { "play", "" } }) + "\"><rect x=\"" + f1(X(h)) + "\" y=\"" + std::to_string(t) + "\" width=\"" + f1(X(1) - X(0)) +
                 "\" height=\"" + std::to_string(H - t - b) + "\" fill=\"transparent\"><title>" + fclock(h * 60.0) + "</title></rect></a>";
        }
        o += "</svg><p class=\"small muted\">Charging power drawn from the grid. Click an hour to jump there.</p>";
    }
    o += "</div><div>";
    // side: policy, stats, busiest stations, summary
    o += "<h3 style=\"margin-top:0\">When every port at a station is busy</h3><p><span class=\"seg\"><a" + std::string(policy == 0 ? " class=\"on\"" : "") + " href=\"" +
         c.url("/live", { { "policy", "" }, { "t", tstr(T) } }) + "\">Drivers wait</a><a" + (policy == 1 ? " class=\"on\"" : "") + " href=\"" + c.url("/live", { { "policy", "divert" }, { "t", tstr(T) } }) +
         "\">Drivers switch after 20 min</a></span></p>";
    auto stat = [&](const char *k, const std::string &v, bool hot = false) { o += "<div><div class=\"t\">" + std::string(k) + "</div><div class=\"v" + (hot ? " hot" : "") + "\">" + esc(v) + "</div></div>"; };
    o += "<div class=\"stats\">";
    stat("On the road", fint(st.road));
    stat("Charging", fint(st.charging_total));
    stat("Waiting for a port", fint(st.waiting_total), st.waiting_total > 0);
    stat("Arrived", fint(st.arrived) + " of " + fint(sv->sim.nv));
    stat("Grid draw now", st.kw >= 1000 ? f2(st.kw / 1000) + " MW" : fint(st.kw) + " kW");
    stat("Energy delivered", st.kwh >= 1000 ? f1(st.kwh / 1000) + " MWh" : fint(st.kwh) + " kWh");
    o += "</div><h3>Busiest stations right now</h3><div class=\"busy\">";
    struct Row { int k, busy, wait, ports; };
    std::vector<Row> rows;
    for (int k = 0; k < sv->sim.nst; k++) {
        int s = sv->sim.st[k].station;
        if (st.charging[(size_t)k] || st.waiting[(size_t)k]) rows.push_back({ k, st.charging[(size_t)k], st.waiting[(size_t)k], R->ports[(size_t)s] });
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.wait != b.wait ? a.wait > b.wait : (double)a.busy / a.ports > (double)b.busy / b.ports; });
    if (rows.size() > 6) rows.resize(6);
    if (rows.empty()) o += "<p class=\"muted\">Every station is idle at this hour.</p>";
    for (auto &r : rows) {
        int s = sv->sim.st[r.k].station;
        Edits w = E.plan.edits;
        int np = std::min(64, r.ports + 2);
        if (np == E.basePorts(s)) w.ports.erase(s); else w.ports[s] = np;
        o += "<div>Station " + std::to_string(sc.stations[s].id) + ": " + std::to_string(r.busy) + " of " + std::to_string(r.ports) + " ports busy" +
             (r.wait ? " <span class=\"hot\">" + std::to_string(r.wait) + " waiting</span>" : "") + " <a class=\"small\" href=\"" + c.url("/live", { { "edit", editKey(sc, w) }, { "t", tstr(T) } }) +
             "\">+2 ports</a><div class=\"bar2\"><i" + (r.busy >= r.ports ? " class=\"full\"" : "") + " style=\"width:" + f1(std::min(100.0, 100.0 * r.busy / r.ports)) + "%\"></i></div></div>";
    }
    o += "</div><p class=\"small\" style=\"margin-top:12px\">" + md(liveSummary(E, *sv, E.sim(1 - policy), policy)) + "</p></div></div>";
    return o;
}

/* ======================================================================== strategies */
static std::string pageStrategies(Ctx &c) {
    Engine &E = c.E;
    Compare &C = E.compare(E.plan.city);
    while (!C.done) E.stepCompare(C);
    int metric = std::max(0, std::min(3, std::atoi(c.get("metric", "0").c_str())));
    std::string o = "<h1>Same city, fifteen strategies</h1><p class=\"lede\">Every pairing of a site-selection method with a routing method, run on identical data at the city's default budget. Darker cells are better for the measure you choose.</p>";
    static const char *ML[] = { "Trips completed", "Energy per trip", "Routing time", "Selection value" };
    o += "<p><span class=\"seg\">";
    for (int k = 0; k < 4; k++) o += "<a" + std::string(k == metric ? " class=\"on\"" : "") + " href=\"" + c.url("/strategies", { { "metric", std::to_string(k) } }) + "\">" + ML[k] + "</a>";
    o += "</span></p>";
    auto val = [&](const CompareRow &r) {
        switch (metric) {
        case 0: return (double)r.feasible;
        case 1: return r.avg_kwh;
        case 2: return std::log10(std::max(r.route_ms, 1e-3));
        default: return (double)r.value;
        }
    };
    bool higher = metric == 0 || metric == 3;
    double lo = 1e300, hi = -1e300;
    for (auto &r : C.rows) { lo = std::min(lo, val(r)); hi = std::max(hi, val(r)); }
    o += "<div class=\"scroll\"><table class=\"matrix\"><thead><tr><th>Site selection</th>";
    for (int r = 0; r < EV_ROUTE_COUNT; r++) o += std::string("<th>") + ROUTE_LABEL[r] + "</th>";
    o += "</tr></thead><tbody>";
    for (int k = 0; k < EV_SEL_COUNT; k++) {
        int sm = SEL_ORDER[k];
        o += std::string("<tr><th>") + SELECT_LABEL[sm] + "</th>";
        for (int rm = 0; rm < EV_ROUTE_COUNT; rm++) {
            const CompareRow *r = findCompare(C, sm, rm);
            if (!r) { o += "<td><span class=\"v\">\xE2\x80\x93</span><span class=\"s muted\">not offered at this size</span></td>"; continue; }
            double sc = hi == lo ? 1 : higher ? (val(*r) - lo) / (hi - lo) : (hi - val(*r)) / (hi - lo);
            int pct = (int)std::lround(6 + 60 * sc);
            std::string v, sub;
            switch (metric) {
            case 0: v = fint(r->feasible); sub = f1(r->feasible_pct) + "% of trips"; break;
            case 1: v = f2(r->avg_kwh) + " kWh"; sub = f1(r->avg_km) + " km per trip"; break;
            case 2: v = fms(r->route_ms); sub = fint((double)r->sssp) + " shortest-path trees"; break;
            default: v = fint((double)r->value); sub = std::to_string(r->stations) + " stations" + (r->search_nodes ? ", " + fint((double)r->search_nodes) + " nodes" : ""); break;
            }
            appf(o, "<td style=\"background:color-mix(in srgb,var(--cobalt) %d%%,var(--paper));color:%s\"><span class=\"v\">%s</span><span class=\"s\">%s</span></td>", pct,
                 sc > 0.62 ? "var(--paper)" : "var(--ink)", esc(v).c_str(), esc(sub).c_str());
        }
        o += "</tr>";
    }
    o += "</tbody></table></div><p style=\"margin-top:12px;max-width:80ch\">" + md(strategiesFinding(C)) + "</p>";
    return o;
}

/* ======================================================================== capacity */
static std::string pageCapacity(Ctx &c) {
    Engine &E = c.E;
    Run *R = E.run();
    if (!R) return "";
    const ev_scenario &sc = E.city().sc;
    const ev_sessions &se = R->ses;
    std::string o = "<h1>Ports and grid under load</h1><p class=\"lede\">Charging sessions from the current plan, hour by hour against grid supply, and station by station against installed ports.</p><div class=\"row\"><div class=\"panel\"><h3 style=\"margin-top:0\">Charging energy by hour</h3>";
    int n = sc.cfg.horizon_h, W = 620, H = 300, l = 50, r = 10, t = 14, b = 28;
    double grid = sc.cfg.grid_kw, ymax = grid * 1.12;
    for (int h = 0; h < n; h++) ymax = std::max(ymax, se.hourly_kwh[h] * 1.12);
    auto X = [&](double h) { return l + h / n * (W - l - r); };
    auto Y = [&](double v) { return t + (1 - v / ymax) * (H - t - b); };
    appf(o, "<svg viewBox=\"0 0 %d %d\" width=\"100%%\" role=\"img\" aria-label=\"Charging energy per hour against grid supply\">", W, H);
    for (int g = 0; g <= 4; g++) appf(o, "<line class=\"grid\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tick\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(ymax * g / 4), Y(ymax * g / 4), l - 4, Y(ymax * g / 4) + 4, fint(ymax * g / 4).c_str());
    for (int h = 0; h < n; h++) {
        double v = se.hourly_kwh[h], under = std::min(v, grid), bw = (W - l - r) / (double)n;
        double x = X(h) + bw * 0.16, w = bw * 0.68;
        appf(o, "<g><title>%02d:00, %s kWh</title><rect class=\"bar\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/>", h, fint(v).c_str(), x, Y(under), w, Y(0) - Y(under));
        if (v > grid) appf(o, "<rect class=\"bar over\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/>", x, Y(v), w, Y(grid) - Y(v));
        o += "</g>";
        if (h % 3 == 0) appf(o, "<text class=\"tick\" x=\"%.1f\" y=\"%d\" text-anchor=\"middle\">%02d:00</text>", X(h + 0.5), H - 8, h);
    }
    appf(o, "<line class=\"supply\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tick\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">Grid supply %s kWh per hour</text></svg>", l, W - r, Y(grid), Y(grid), W - r, Y(grid) - 6, fint(grid).c_str());
    o += "<p class=\"small\">" + md(surgeNote(se)) + "</p></div><div class=\"panel\"><h3 style=\"margin-top:0\">Busiest stations</h3><p class=\"small muted\">Blue: sessions the installed ports can serve. Amber: sessions they cannot.</p><div class=\"busy\">";
    std::vector<int> order;
    for (int q = 0; q < se.n; q++) if (se.st[q].sessions > 0) order.push_back(q);
    std::sort(order.begin(), order.end(), [&](int a, int b2) { return se.st[a].sessions > se.st[b2].sessions; });
    if (order.size() > 12) order.resize(12);
    int mx = 1;
    for (int q : order) mx = std::max(mx, se.st[q].sessions);
    for (int q : order) {
        const ev_station_load &L = se.st[q];
        int ports = R->ports[(size_t)L.station];
        appf(o, "<div class=\"stbar\"><span>Station %d</span><span class=\"b\" title=\"%d of %d sessions servable\"><i style=\"left:0;width:%.1f%%\"></i><i class=\"l\" style=\"left:%.1f%%;width:%.1f%%\"></i></span><span class=\"%s\">needs %d, has %d</span></div>",
             sc.stations[L.station].id, L.served, L.sessions, 100.0 * L.served / mx, 100.0 * L.served / mx, 100.0 * (L.sessions - L.served) / mx, L.chargers_needed > ports ? "short" : "", L.chargers_needed, ports);
    }
    if (order.empty()) o += "<p class=\"muted\">No charging sessions in this plan.</p>";
    o += "</div></div></div>";
    const ev_capacity &cp = R->cap;
    bool agree = cp.flow_dfs == cp.flow_bfs && cp.flow_bfs == cp.matching;
    auto card = [&](const char *t2, long v, const std::string &sub) { o += "<div class=\"kpi\"><div class=\"t\">" + std::string(t2) + "</div><div class=\"v\">" + fint((double)v) + "</div><div class=\"s\">" + esc(sub) + "</div></div>"; };
    o += "<div class=\"cards\">";
    card("Ford\xE2\x80\x93" "Fulkerson, depth-first", cp.flow_dfs, fms(cp.ms_dfs) + ", " + fint((double)cp.aug_dfs) + " augmenting paths");
    card("Edmonds\xE2\x80\x93Karp, breadth-first", cp.flow_bfs, fms(cp.ms_bfs) + ", " + fint((double)cp.aug_bfs) + " augmenting paths");
    card("Bipartite matching to port slots", cp.matching, fms(cp.ms_match));
    o += std::string("<div class=\"kpi\"><span class=\"pill ") + (agree ? "ok\">All three agree" : "bad\">Results differ") + "</span><div class=\"s\" style=\"margin-top:6px\">" + md(flowNote(sc, cp)) + "</div></div></div>";
    return o;
}

/* ======================================================================== algorithms */
static std::string pageAlgorithms(Ctx &c) {
    Engine &E = c.E;
    Analysis &A = E.analysis(E.plan.city);
    if (!A.done) E.computeAnalysis(E.plan.city);
    std::string o = "<h1>Every decision and the algorithm behind it</h1><p class=\"lede\">The engine checks each answer against a second, independent algorithm on the same city. This is its report for " + esc(lowerLabel(E.city())) + ".</p>";
    o += "<div class=\"scroll\"><table><thead><tr><th>Decision</th><th>Algorithm</th><th>Result on this city</th><th>Check</th></tr></thead><tbody>";
    for (const AlgoRow &r : algorithmRows(E.city(), E.run(), A)) {
        if (r.family) { o += "<tr class=\"fam\"><th colspan=\"4\">" + esc(r.decision) + "</th></tr>"; continue; }
        const char *cls = r.check == CHECK_OK ? "ok" : r.check == CHECK_BAD ? "bad" : "";
        o += "<tr><th>" + esc(r.decision) + "</th><td class=\"muted\">" + esc(r.algorithm) + "</td><td>" + md(r.result) + "</td><td><span class=\"pill " + cls + "\">" + checkLabel(r.check) + "</span></td></tr>";
    }
    o += "</tbody></table></div><details><summary>Show the engine's own report</summary><pre>" + esc(A.text) + "</pre></details>";
    return o;
}

/* ======================================================================== data model */
static std::string pageModel(Ctx &c) {
    Engine &E = c.E;
    Run *R = E.run();
    if (!R) return "";
    Analysis &A = E.analysis(E.plan.city);
    if (!A.done) E.computeAnalysis(E.plan.city);
    int t0 = R->samples.empty() ? -1 : R->samples[0].trip;
    const TripDetail *D = t0 >= 0 ? &E.trip(t0) : nullptr;
    std::string o = "<h1>Records, trees and graphs</h1><p class=\"lede\">How the system represents road nodes, trips, batteries, stations and installation budgets: the typed C records each city loads into, and the arrays, trees and weighted or capacitated graphs the engine builds from them.</p>";
    o += "<h2>Typed records</h2><p class=\"small muted\">Each CSV file of a city loads into a typed C record. These are the real values for " + esc(lowerLabel(E.city())) + ".</p>";
    o += "<div class=\"scroll\"><table><thead><tr><th>Represents</th><th>C record</th><th>Holds</th><th>In this city</th><th>Example</th><th>Defined in</th></tr></thead><tbody>";
    for (auto &x : modelRecords(E, *R, D))
        o += "<tr><th>" + esc(x.what) + "</th><td><code>" + esc(x.type) + "</code></td><td>" + esc(x.holds) + "</td><td>" + esc(x.count) + "</td><td class=\"mono\">" + esc(x.example) + "</td><td><code>" + esc(x.file) + "</code></td></tr>";
    o += "</tbody></table></div><h2>Structures built from them</h2><p class=\"small muted\">The engine turns those records into arrays, trees and weighted or capacitated graphs. Sizes are for the current plan.</p>";
    o += "<div class=\"scroll\"><table><thead><tr><th>Structure</th><th>Built from</th><th>Size here</th><th>What it does</th><th>Code</th></tr></thead><tbody>";
    for (auto &x : modelStructures(E, *R, A))
        o += "<tr><th>" + esc(x.name) + "</th><td>" + esc(x.from) + "</td><td>" + esc(x.size) + "</td><td>" + esc(x.what) + "</td><td><code>" + esc(x.code) + "</code></td></tr>";
    o += "</tbody></table></div>";
    return o;
}

/* ======================================================================== benchmarks */
static std::string pageBench(Ctx &c) {
    Engine &E = c.E;
    const auto &rows = E.bench();
    std::string o = "<h1>How each algorithm scales</h1><p class=\"lede\">Measured run time against input size on logarithmic axes, from results/bench.csv. A line that bends upward faster is the one that will not survive a bigger city.</p>";
    if (rows.empty()) return o + "<p>" + esc(E.bench_error) + "</p>";
    int fam = std::max(0, std::min((int)FAMILIES.size() - 1, std::atoi(c.get("family", "0").c_str())));
    o += "<p class=\"chips\">";
    for (int f = 0; f < (int)FAMILIES.size(); f++)
        o += "<a class=\"opt" + std::string(f == fam ? " on" : "") + "\" style=\"display:inline-block\" href=\"" + c.url("/benchmarks", { { "family", std::to_string(f) } }) + "\">" + FAMILIES[(size_t)f].label + "</a>";
    o += "</p>";
    const Family &F = FAMILIES[(size_t)fam];
    std::map<std::string, std::vector<std::pair<double, double>>> series;
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    for (auto &r : rows) {
        if (std::find(F.fam.begin(), F.fam.end(), r.family) == F.fam.end() || benchSkip(r.dataset) || r.ms <= 0 || r.n <= 0) continue;
        series[r.alg].push_back({ r.n, r.ms });
        xmin = std::min(xmin, r.n); xmax = std::max(xmax, r.n); ymin = std::min(ymin, r.ms); ymax = std::max(ymax, r.ms);
    }
    o += "<div class=\"row\"><div>";
    if (!series.empty()) {
        double lx0 = std::floor(std::log10(xmin)), lx1 = std::ceil(std::log10(xmax)), ly0 = std::floor(std::log10(ymin)), ly1 = std::ceil(std::log10(ymax));
        if (lx1 <= lx0) lx1 = lx0 + 1;
        if (ly1 <= ly0) ly1 = ly0 + 1;
        int W = 640, H = 400, l = 64, r = 16, t = 14, b = 40;
        auto X = [&](double v) { return l + (std::log10(v) - lx0) / (lx1 - lx0) * (W - l - r); };
        auto Y = [&](double v) { return t + (1 - (std::log10(v) - ly0) / (ly1 - ly0)) * (H - t - b); };
        appf(o, "<svg viewBox=\"0 0 %d %d\" width=\"100%%\" role=\"img\" aria-label=\"Run time against input size, log-log\">", W, H);
        for (double e = lx0; e <= lx1; e++) appf(o, "<line class=\"grid\" x1=\"%.1f\" x2=\"%.1f\" y1=\"%d\" y2=\"%d\"/><text class=\"tick\" x=\"%.1f\" y=\"%d\" text-anchor=\"middle\">%s</text>", X(std::pow(10, e)), X(std::pow(10, e)), t, H - b, X(std::pow(10, e)), H - b + 16, fint(std::pow(10, e)).c_str());
        for (double e = ly0; e <= ly1; e++) {
            char lab[32];
            std::snprintf(lab, sizeof lab, "%g", std::pow(10, e));
            appf(o, "<line class=\"grid\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tick\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(std::pow(10, e)), Y(std::pow(10, e)), l - 6, Y(std::pow(10, e)) + 4, lab);
        }
        appf(o, "<text class=\"tick\" x=\"%d\" y=\"%d\" text-anchor=\"middle\">input size n</text><text class=\"tick\" x=\"14\" y=\"%d\" transform=\"rotate(-90 14 %d)\" text-anchor=\"middle\">time (ms)</text>", (l + W - r) / 2, H - 4, H / 2, H / 2);
        static const char *PAL[] = { "#2C55E8", "#B97710", "#2D7D53", "#C2433A", "#7A4FC2", "#15233B", "#2A9BB0" };
        int k = 0;
        std::string legend;
        for (auto &kv : series) {
            auto pts = kv.second;
            std::sort(pts.begin(), pts.end());
            const char *colr = PAL[k % 7];
            appf(o, "<polyline class=\"ln\" stroke=\"%s\" points=\"", colr);
            for (auto &p : pts) appf(o, "%.1f,%.1f ", X(p.first), Y(p.second));
            o += "\"/>";
            for (auto &p : pts) appf(o, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"3\" fill=\"%s\"><title>%s: n = %s, %s</title></circle>", X(p.first), Y(p.second), colr, esc(algName(kv.first)).c_str(), fint(p.first).c_str(), fms(p.second).c_str());
            legend += "<span><i style=\"background:" + std::string(colr) + "\"></i>" + esc(algName(kv.first)) + "</span>";
            k++;
        }
        o += "</svg><div class=\"legend\">" + legend + "</div>";
    }
    o += "</div><div><h3 style=\"margin-top:0\">Inputs built to break them</h3>";
    for (auto &nt : adversarialNotes(rows)) o += "<p><b>" + esc(nt.first) + "</b><br><span class=\"small\">" + esc(nt.second) + "</span></p>";
    o += "</div></div>";
    return o;
}

/* ======================================================================== HTTP */
struct Response { int status = 200; std::string type = "text/html; charset=utf-8", body; bool cache = false; };

static std::string g_assets;

static Response serveFont(const std::string &name) {
    Response r;
    bool ok = !name.empty() && name.find("..") == std::string::npos && name.find('/') == std::string::npos && name.size() > 4 && name.compare(name.size() - 4, 4, ".ttf") == 0;
    FILE *f = ok ? std::fopen((g_assets + "/" + name).c_str(), "rb") : nullptr;
    if (!f) { r.status = 404; r.type = "text/plain"; r.body = "not found"; return r; }
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) r.body.append(buf, n);
    std::fclose(f);
    r.type = "font/ttf";
    r.cache = true;
    return r;
}

static Response handle(Engine &E, const std::string &target) {
    Response res;
    std::string path = target, qs;
    size_t qm = target.find('?');
    if (qm != std::string::npos) { path = target.substr(0, qm); qs = target.substr(qm + 1); }
    if (path == "/health") { res.type = "text/plain"; res.body = "ok"; return res; }
    if (path.rfind("/fonts/", 0) == 0) return serveFont(path.substr(7));
    Ctx c{ E, {}, path };
    size_t p = 0;
    while (p < qs.size()) {
        size_t a = qs.find('&', p);
        std::string kv = qs.substr(p, a == std::string::npos ? std::string::npos : a - p);
        p = a == std::string::npos ? qs.size() : a + 1;
        size_t eq = kv.find('=');
        std::string k = urldec(kv.substr(0, eq)), v = eq == std::string::npos ? "" : urldec(kv.substr(eq + 1));
        if (k.size() < 32 && v.size() < 2048) c.q[k] = v;
    }
    applyRequest(c);
    if (path == "/sites.csv") {
        res.type = "text/csv; charset=utf-8";
        res.body = E.sitesCsv();
        return res;
    }
    int page = -1;
    for (int i = 0; i < NPAGES; i++) if (path == PAGES[i].path) page = i;
    if (page < 0) {
        res.status = 404;
        res.body = frame(c, 0, "<h1>Page not found</h1><p><a href=\"" + c.url("/") + "\">Open the planner</a></p>");
        return res;
    }
    std::string head, body;
    switch (page) {
    case 0: body = pagePlanner(c); break;
    case 1: body = pageLive(c, head); break;
    case 2: body = pageStrategies(c); break;
    case 3: body = pageCapacity(c); break;
    case 4: body = pageAlgorithms(c); break;
    case 5: body = pageModel(c); break;
    default: body = pageBench(c); break;
    }
    saveBaseline(c);
    res.body = frame(c, page, body, head);
    return res;
}

static const char *statusText(int s) { return s == 200 ? "OK" : s == 404 ? "Not Found" : s == 400 ? "Bad Request" : s == 405 ? "Method Not Allowed" : "Error"; }

static void sendAll(sock_t s, const char *p, size_t n) {
    while (n > 0) {
#ifdef _WIN32
        int k = send(s, p, (int)n, 0);
#else
        ssize_t k = send(s, p, n, MSG_NOSIGNAL);
#endif
        if (k <= 0) return;
        p += k;
        n -= (size_t)k;
    }
}

static void client(Engine &E, sock_t s) {
    std::string req;
    char buf[4096];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
        int n = (int)recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
    }
    size_t sp1 = req.find(' '), sp2 = sp1 == std::string::npos ? std::string::npos : req.find(' ', sp1 + 1);
    Response r;
    bool head = false;
    if (sp2 == std::string::npos) { r.status = 400; r.type = "text/plain"; r.body = "bad request"; }
    else {
        std::string method = req.substr(0, sp1), target = req.substr(sp1 + 1, sp2 - sp1 - 1);
        head = method == "HEAD";
        if (method != "GET" && !head) { r.status = 405; r.type = "text/plain"; r.body = "GET only"; }
        else {
            double t0 = ev_now_ms();
            r = handle(E, target);
            std::fprintf(stderr, "%s %s -> %d (%.1f ms, %zu bytes)\n", method.c_str(), target.substr(0, 160).c_str(), r.status, ev_now_ms() - t0, r.body.size());
        }
    }
    std::string hdr;
    appf(hdr, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n%s\r\n", r.status, statusText(r.status), r.type.c_str(),
         r.body.size(), r.cache ? "Cache-Control: public, max-age=86400\r\n" : "Cache-Control: no-store\r\n");
    sendAll(s, hdr.data(), hdr.size());
    if (!head) sendAll(s, r.body.data(), r.body.size());
    CLOSESOCK(s);
}

static bool exists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

int main(int argc, char **argv) {
    std::string data, results, assets;
    int port = 8033;
    for (int i = 1; i + 1 < argc; i++) {
        std::string a = argv[i];
        if (a == "--port") port = std::atoi(argv[++i]);
        else if (a == "--data") data = argv[++i];
        else if (a == "--results") results = argv[++i];
        else if (a == "--assets") assets = argv[++i];
    }
    if (const char *pe = std::getenv("PORT")) port = std::atoi(pe);
    std::vector<std::string> roots = { ".", "..", EV33_SOURCE_DIR };
    auto find = [&](const char *sub, const char *probe) {
        for (auto &r : roots) if (exists(r + "/" + sub + "/" + probe)) return r + "/" + sub;
        return std::string(sub);
    };
    if (data.empty()) data = find("data", "medium/scenario.cfg");
    if (results.empty()) results = find("results", "bench.csv");
    g_assets = assets.empty() ? find("assets/fonts", "IBMPlexSans-Regular.ttf") : assets + "/fonts";

    Engine E;
    if (!E.init(data, results)) { std::fprintf(stderr, "cannot load cities from %s\n", data.c_str()); return 1; }
    for (int i = 0; i < (int)E.cities.size(); i++) E.load(i);

#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (sockaddr *)&addr, sizeof addr) != 0 || listen(srv, 64) != 0) { std::perror("bind/listen"); return 1; }
    std::fprintf(stderr, "EV33 web server on port %d (data %s, results %s)\n", port, data.c_str(), results.c_str());
    for (;;) {
        sock_t c = accept(srv, nullptr, nullptr);
#ifdef _WIN32
        if (c == INVALID_SOCKET) continue;
        DWORD tv = 10000;
#else
        if (c < 0) { if (errno == EINTR) continue; continue; }
        timeval tv{ 10, 0 };
#endif
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        client(E, c);
    }
}
