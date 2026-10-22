// web.cpp - EV33 Charge Atlas on the web (deployed on Render).
//
// The website shows the same eight views as the desktop app (gui/), in the same layout and colours:
// Dashboard, Trip Planner, Station Placement, Road Network, Algorithms, Benchmarks, Data Management
// and Settings. Every page is generated here in C++: HTML for the layout, SVG for the map and the
// charts, CSS as a string below. There is no JavaScript: clicks are links and forms, and the day
// simulation plays by reloading the page once a second. Every number comes from the C++17 engine
// (include/ev, src/) through ../shared, the same layer the desktop app uses.
//
//   ev33_web [--port N] [--data DIR] [--results DIR] [--assets DIR]     (PORT in the environment wins)
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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

#include "catalog.h"
#include "content.h"
#include "engine.h"
#include "ev/algorithms/sorting.hpp"
#include "fmt.h"

#ifndef EV33_SOURCE_DIR
#define EV33_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;
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

static std::vector<std::string> words(const char *s) {
    std::vector<std::string> v;
    std::istringstream in(s);
    std::string w;
    while (in >> w) v.push_back(w);
    return v;
}

// a whole string as a number; false for empty text, trailing junk, inf or nan
static bool toNumber(const std::string &s, double &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    out = std::strtod(s.c_str(), &end);
    while (end && *end == ' ') end++;
    return end && *end == 0 && std::isfinite(out);
}

static std::string num(double v, int digits) {   // plain number for form fields (no grouping)
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", digits, v);
    return b;
}

/* ======================================================================== views and request state */
typedef std::map<std::string, std::string> Params;

enum { V_DASH = 0, V_TRIP, V_PLACE, V_NET, V_ALGO, V_BENCH, V_DATA, V_SET, V_COUNT };
struct ViewDef {
    const char *path, *name, *keys;   // keys: the page's own query parameters, kept while on that page
};
static const ViewDef VIEWS[V_COUNT] = {
    { "/", "Dashboard", "" },
    { "/trip", "Trip Planner", "trip from to ev bat chg cons res dep method calc" },
    { "/placement", "Station Placement", "tab filter sort dir" },
    { "/network", "Road Network", "mode station trip z cx cy lay ter jun nam exi can new rte trf time speed play" },
    { "/algorithms", "Algorithms", "" },
    { "/benchmarks", "Benchmarks", "family" },
    { "/data", "Data Management", "check" },
    { "/settings", "Settings", "" },
};
// Keys that follow the visitor from page to page: the plan and the settings.
static const char *PLAN_KEYS[] = { "city", "sel", "route", "budget", "edit", "theme", "dm", "policy" };

struct Ctx {
    Engine &E;
    Params q;
    std::string path;
    int view = 0;
    std::string url(const std::string &page, const Params &over = {}) const {
        Params m;
        for (const char *k : PLAN_KEYS) { auto it = q.find(k); if (it != q.end() && !it->second.empty()) m[k] = it->second; }
        if (page == path)
            for (const std::string &k : words(VIEWS[view].keys)) { auto it = q.find(k); if (it != q.end() && !it->second.empty()) m[k] = it->second; }
        for (auto &kv : over) { if (kv.second.empty()) m.erase(kv.first); else m[kv.first] = kv.second; }
        std::string u = page, sep = "?";
        for (auto &kv : m) { u += sep + kv.first + "=" + urlenc(kv.second); sep = "&"; }
        return esc(u);
    }
    std::string get(const char *k, const char *def = "") const { auto it = q.find(k); return it == q.end() || it->second.empty() ? def : it->second; }
    bool has(const char *k) const { auto it = q.find(k); return it != q.end() && !it->second.empty(); }
    // hidden form fields for the plan keys (and this page's keys), except those listed
    std::string hidden(std::initializer_list<const char *> except, bool page_keys = true) const {
        std::vector<std::string> keys(std::begin(PLAN_KEYS), std::end(PLAN_KEYS));
        if (page_keys) for (const std::string &k : words(VIEWS[view].keys)) keys.push_back(k);
        std::string o;
        for (const std::string &k : keys) {
            bool skip = false;
            for (const char *e : except) skip = skip || k == e;
            auto it = q.find(k);
            if (skip || it == q.end() || it->second.empty()) continue;
            o += "<input type=\"hidden\" name=\"" + k + "\" value=\"" + esc(it->second) + "\">";
        }
        return o;
    }
};

static std::string editKey(const Scene &sc, const Edits &e) {
    std::vector<std::string> t;
    for (int i : e.lock) t.push_back("l" + std::to_string(sc.stations[(size_t)i].id));
    for (int i : e.ban) t.push_back("b" + std::to_string(sc.stations[(size_t)i].id));
    for (auto &kv : e.ports) t.push_back("p" + std::to_string(sc.stations[(size_t)kv.first].id) + "=" + std::to_string(kv.second));
    std::sort(t.begin(), t.end());
    std::string o;
    for (auto &s : t) { if (!o.empty()) o += ","; o += s; }
    return o;
}

static int stationIndex(const Scene &sc, long id) {
    for (int i = 0; i < sc.ns; i++) if (sc.stations[(size_t)i].id == id) return i;
    return -1;
}

static int nodeById(const Scene &sc, long id) {
    for (int i = 0; i < sc.nn; i++) if (sc.nodes[(size_t)i].id == id) return i;
    return -1;
}

static Edits parseEdits(const Scene &sc, const std::string &s) {
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
        if (tok[0] == 'l' && !sc.stations[(size_t)i].existing) { e.ban.erase(i); e.lock.insert(i); }
        else if (tok[0] == 'b' && !sc.stations[(size_t)i].existing) { e.lock.erase(i); e.ban.insert(i); }
        else if (tok[0] == 'p') {
            size_t eq = tok.find('=');
            if (eq == std::string::npos) continue;
            int n = std::max(1, std::min(64, std::atoi(tok.c_str() + eq + 1)));
            if (n != sc.stations[(size_t)i].ports) e.ports[i] = n;
        }
    }
    return e;
}

static void applyRequest(Ctx &c) {
    Engine &E = c.E;
    int city = 0;
    std::string cn = c.get("city", "medium");
    for (int i = 0; i < (int)E.cities.size(); i++) if (E.cities[(size_t)i].name == cn) city = i;
    if (!E.load(city)) city = 0;
    Plan p;
    p.city = city;
    const City &C = E.cities[(size_t)city];
    int s = selParse(c.get("sel", "dp"));
    p.sel = s >= 0 ? s : SEL_DP;
    int r = routeParse(c.get("route", "dijkstra"));
    p.route = r >= 0 ? r : ROUTE_DIJKSTRA;
    if (p.route == ROUTE_FLOYD && C.sc.nn > FLOYD_MAX_NODES) p.route = ROUTE_DIJKSTRA;
    std::string b = c.get("budget");
    p.budget = b.empty() ? C.default_budget : std::max(0, std::min(10000000, std::atoi(b.c_str())));
    p.edits = parseEdits(C.sc, c.get("edit"));
    E.plan = p;
    E.baseline = Baseline();
}

/* ======================================================================== CSS */
// The same palette as the desktop app (gui/ui.cpp): warm light, or deep charcoal; forest-green
// primary, muted-blue secondary. "Auto" follows the visitor's system setting.
static const char *CSS = R"CSS(
@font-face{font-family:Plex;src:url(/fonts/IBMPlexSans-Regular.ttf);font-weight:400;font-display:swap}
@font-face{font-family:Plex;src:url(/fonts/IBMPlexSans-SemiBold.ttf);font-weight:600;font-display:swap}
@font-face{font-family:Plex;src:url(/fonts/IBMPlexSans-Bold.ttf);font-weight:700;font-display:swap}
@font-face{font-family:PlexMono;src:url(/fonts/IBMPlexMono-Regular.ttf);font-display:swap}
:root{--bg:#F3F1EC;--panel:#FBFAF7;--panel2:#EBE7DF;--line:#D8D2C6;--ink:#1E2421;--ink2:#4E5752;--ink3:#858B86;--contour:#C9C2B2;
--primary:#2E6A4F;--primarySoft:#DAE8DF;--secondary:#4B6D8E;--secondarySoft:#DDE6EF;--warn:#A4670F;--warnSoft:#F3E5C9;--error:#B13A31;--errorSoft:#F4DCD8;
--onPrimary:#FBFAF7;color-scheme:light}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#1A1D1C;--panel:#212524;--panel2:#2A2F2D;--line:#3A403D;--ink:#E6E8E3;--ink2:#B2B8B3;--ink3:#7E8681;
--contour:#3E4842;--primary:#6BB18B;--primarySoft:#20382D;--secondary:#89A9C8;--secondarySoft:#22303D;--warn:#E0A84A;--warnSoft:#3A2F1B;--error:#E4796E;--errorSoft:#3D2420;
--onPrimary:#1A1D1C;color-scheme:dark}}
:root[data-theme=dark]{--bg:#1A1D1C;--panel:#212524;--panel2:#2A2F2D;--line:#3A403D;--ink:#E6E8E3;--ink2:#B2B8B3;--ink3:#7E8681;
--contour:#3E4842;--primary:#6BB18B;--primarySoft:#20382D;--secondary:#89A9C8;--secondarySoft:#22303D;--warn:#E0A84A;--warnSoft:#3A2F1B;--error:#E4796E;--errorSoft:#3D2420;
--onPrimary:#1A1D1C;color-scheme:dark}
*{box-sizing:border-box}
html,body{margin:0}
body{background:var(--bg);color:var(--ink);font:15px/1.5 Plex,"Segoe UI",system-ui,sans-serif}
a{color:var(--secondary)}
.app{display:grid;grid-template-columns:262px minmax(0,1fr);min-height:100vh}
.side{background:var(--panel);border-right:1px solid var(--line);padding:20px 16px 16px;position:sticky;top:0;height:100vh;overflow:auto;display:flex;flex-direction:column;gap:12px}
.brand{display:flex;gap:10px;align-items:center;text-decoration:none;color:var(--ink)}
.brand b{display:block;font-size:1.3rem;line-height:1.1;font-weight:600}.brand span{font-size:.84rem;color:var(--ink2)}
.lbl{display:block;font-size:.84rem;color:var(--ink2);margin:0 0 4px}
.dsrow{display:flex;gap:6px}.dsrow select{flex:1;min-width:0}
.tag{display:inline-block;padding:2px 8px;border-radius:3px;font-size:.76rem;font-weight:600;letter-spacing:.03em;background:var(--panel2);color:var(--ink2);vertical-align:middle}
.tag.warn{background:var(--warnSoft);color:var(--warn)}.tag.ok{background:var(--primarySoft);color:var(--primary)}.tag.bad{background:var(--errorSoft);color:var(--error)}.tag.info{background:var(--secondarySoft);color:var(--secondary)}
nav.views{display:grid;gap:2px;margin-top:6px}
nav.views a{display:flex;justify-content:space-between;align-items:center;padding:9px 14px;border-radius:4px;text-decoration:none;color:var(--ink);border-left:3px solid transparent}
nav.views a span{color:var(--ink3);font-size:.85rem}
nav.views a:hover{background:var(--panel2)}
nav.views a.on{background:var(--primarySoft);border-left-color:var(--primary);font-weight:600}
.side .foot{margin-top:auto;border-top:1px solid var(--line);padding-top:12px;display:grid;gap:8px}
.plan{font-size:.84rem;color:var(--ink3)}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:4px;overflow:hidden;background:var(--panel);max-width:100%;flex-wrap:wrap}
.seg a{padding:6px 12px;text-decoration:none;color:var(--ink);border-right:1px solid var(--line);font-size:.92rem;white-space:nowrap}
.seg a:last-child{border-right:0}.seg a.on{background:var(--primary);color:var(--onPrimary)}.seg a.on.red{background:var(--error)}
.seg.full{display:flex}.seg.full a{flex:1;text-align:center}
.seg.sm a{padding:2px 9px;font-size:.84rem}
main{min-width:0;display:flex;flex-direction:column}
.work{padding:26px 30px 30px;flex:1;display:grid;gap:14px;align-content:start}
.status{position:sticky;bottom:0;display:flex;flex-wrap:wrap;justify-content:space-between;gap:4px 16px;padding:6px 30px;background:var(--panel2);border-top:1px solid var(--line);font-size:.85rem;color:var(--ink2)}
h1{font-size:1.65rem;margin:0;line-height:1.2;font-weight:600;text-wrap:balance}
.lede{color:var(--ink2);max-width:100ch;margin:4px 0 0}
h2{font-size:1.15rem;margin:0 0 4px;font-weight:600}
h3{font-size:1.02rem;margin:4px 0 0;font-weight:600}
.hint{font-size:.86rem;color:var(--ink3);margin:0 0 10px}
p{margin:0 0 8px}
.small{font-size:.86rem}.muted{color:var(--ink3)}.ink2{color:var(--ink2)}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:16px 18px;min-width:0}
.cols{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;align-items:start}
.metrics{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:10px}
.metrics.six{grid-template-columns:repeat(auto-fill,minmax(190px,1fr))}
.metric{background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:12px 16px;min-width:0}
.metric.p{border-left:3px solid var(--primary)}.metric.s{border-left:3px solid var(--secondary)}.metric.e{border-left:3px solid var(--error)}
.metric .ml{font-size:.86rem;color:var(--ink2)}
.metric .mv{font-family:PlexMono,ui-monospace,Menlo,Consolas,monospace;font-size:1.55rem;line-height:1.4;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.metric .mn{font-size:.82rem;color:var(--ink3);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.big{font-family:PlexMono,ui-monospace,Menlo,Consolas,monospace;font-size:1.55rem;margin-right:8px}
code,.mono{font-family:PlexMono,ui-monospace,Menlo,Consolas,monospace;font-size:.9em}td.mono{white-space:nowrap}
table{border-collapse:collapse;width:100%}
th,td{text-align:left;vertical-align:top;padding:7px 10px;border-bottom:1px solid var(--line);font-size:.92rem}
thead th{background:var(--panel2);font-weight:600;font-size:.88rem;white-space:nowrap;border-bottom:0}
thead th a{color:var(--ink);text-decoration:none}thead th a:hover{text-decoration:underline}
tbody tr:nth-child(even){background:color-mix(in srgb,var(--panel2) 40%,transparent)}
tr.fam th{background:var(--panel2)}
.kv th{font-weight:400;color:var(--ink2);width:46%}.kv td{font-family:PlexMono,ui-monospace,Menlo,Consolas,monospace;font-size:.88rem}
.scroll{overflow-x:auto}
input,select,button,textarea{font:inherit;color:var(--ink);background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:6px 9px;min-width:0}
input:focus-visible,select:focus-visible,button:focus-visible,a:focus-visible{outline:2px solid var(--secondary);outline-offset:1px}
.btn,button{display:inline-block;cursor:pointer;background:var(--panel2);color:var(--ink);text-decoration:none;border:1px solid var(--line);border-radius:4px;padding:6px 12px;font-size:.92rem;line-height:1.4}
.btn:hover,button:hover{border-color:var(--ink3)}
.btn.primary,button.primary{background:var(--primary);border-color:var(--primary);color:var(--onPrimary);font-weight:600}
.btn.sm{padding:1px 8px;font-size:.86rem}
.btn.dis{opacity:.45;pointer-events:none}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.field{display:grid;grid-template-columns:minmax(0,1fr);gap:4px;margin-bottom:10px}
.field label{font-size:.86rem;color:var(--ink2)}
.field .inl{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.field .inl input{width:140px}
.note{font-size:.83rem;color:var(--ink3)}
.wide{width:100%}
.trip{display:grid;grid-template-columns:350px minmax(0,1fr);gap:14px;align-items:start}
.netgrid{display:grid;grid-template-columns:minmax(0,1fr) 310px;gap:12px;align-items:start}.netgrid>aside{grid-template-columns:minmax(0,1fr)}
.map{display:block;width:100%;height:auto;background:var(--panel);border:1px solid var(--line);border-radius:4px}
.mapwrap{position:relative}
.mapctl{position:absolute;top:10px;right:10px;display:flex;gap:4px;flex-wrap:wrap;justify-content:flex-end;max-width:60%}
.legend{display:grid;gap:6px;font-size:.88rem;color:var(--ink2)}
.legend div{display:flex;gap:8px;align-items:center}
.legend svg{flex:none}
.tabs{display:flex;gap:2px;border-bottom:1px solid var(--line);flex-wrap:wrap}
.tabs a{padding:8px 14px;text-decoration:none;color:var(--ink2);border-bottom:2px solid transparent;margin-bottom:-1px}
.tabs a.on{color:var(--ink);border-bottom-color:var(--primary);font-weight:600}
.err{background:var(--errorSoft);border:1px solid var(--error);border-radius:4px;padding:12px 16px}
.err .t{color:var(--error);font-weight:600}
.good{color:var(--primary)}.bad{color:var(--error)}.warnc{color:var(--warn)}
.diamond{display:inline-block;width:8px;height:8px;background:var(--primary);transform:rotate(45deg);margin:0 7px 1px 2px}
.seq{color:var(--ink2);line-height:1.9}
.clock{font-family:PlexMono,ui-monospace,Menlo,Consolas,monospace;font-size:2.1rem;line-height:1.1}
details>summary{cursor:pointer;font-weight:600;font-size:1.02rem;padding:4px 0}
details[open]>summary{margin-bottom:8px}
.chart{display:block;width:100%;height:auto}
.gl{stroke:var(--line)}.tk{fill:var(--ink3);font-size:11px;font-family:Plex,sans-serif}.tkl{fill:var(--ink2);font-size:11px;font-family:Plex,sans-serif}
.ter{fill:var(--contour)}.ctr{stroke:var(--contour);fill:none}.road{stroke:var(--ink3);stroke-opacity:.55;fill:none}
.tr0{stroke:var(--ink3);stroke-opacity:.25}.tr1{stroke:var(--secondary);stroke-opacity:.45}.tr2{stroke:var(--secondary);stroke-opacity:.8}.tr3{stroke:var(--warn)}.tr4{stroke:var(--error)}
.jn{fill:var(--ink3)}.jname{fill:var(--ink2);font-size:10px;font-family:Plex,sans-serif}
.ex{fill:var(--secondary)}.offl{fill:var(--panel);stroke:var(--error);stroke-width:1.6}.x{stroke:var(--error);stroke-width:1.8}
.cand{fill:var(--panel);stroke:var(--ink3);stroke-width:1.5}.newst{fill:var(--primary)}.shortr{fill:none;stroke:var(--warn);stroke-width:2.2}
.selr{fill:none;stroke:var(--ink);stroke-width:2}.lockr{fill:none;stroke:var(--ink);stroke-width:1.5}.plbl{fill:var(--ink);font-size:10px;font-weight:600;font-family:Plex,sans-serif}
.rt{fill:none;stroke:var(--primary);stroke-width:3.6;stroke-linejoin:round;stroke-linecap:round}.rt.bad{stroke:var(--error);stroke-dasharray:7 5}
.rs{fill:var(--panel);stroke:var(--primary);stroke-width:2.5}.re{fill:var(--ink)}
.bdg{fill:var(--ink)}.bdgt{fill:var(--panel);font-size:11px;font-weight:700;font-family:Plex,sans-serif}
.car{fill:var(--secondary)}.car.low{fill:var(--warn)}.occ{fill:none;stroke:var(--primary);stroke-width:3}.occ.full{stroke:var(--warn)}
.wt{fill:var(--warn)}.wtt{fill:var(--panel);font-size:10px;font-weight:700;font-family:Plex,sans-serif}.stn{fill:var(--panel);stroke:var(--ink2);stroke-width:1.4}
.bar{fill:var(--secondary)}.bar.over{fill:var(--warn)}.sup{stroke:var(--ink);stroke-dasharray:6 4;stroke-width:1.3}.now{stroke:var(--warn);stroke-width:2}
.area{fill:var(--secondary);fill-opacity:.18}.kwl{fill:none;stroke:var(--secondary);stroke-width:2}
.elev{fill:var(--contour);fill-opacity:.5}.bat{fill:none;stroke:var(--primary);stroke-width:2.5;stroke-linejoin:round}.res{stroke:var(--error);stroke-width:1.4}.tgt{stroke:var(--ink3);stroke-dasharray:5 4}
.scale{stroke:var(--ink2);stroke-width:1.5}
.ln{fill:none;stroke-width:2}
.keys{display:flex;flex-wrap:wrap;gap:4px 16px;font-size:.86rem;color:var(--ink2);margin-top:6px}.keys i{display:inline-block;width:12px;height:3px;margin-right:6px;vertical-align:4px}
@media (max-width:1180px){.cols{grid-template-columns:minmax(0,1fr)}.netgrid{grid-template-columns:minmax(0,1fr)}.trip{grid-template-columns:minmax(0,1fr)}}
@media (max-width:860px){.app{grid-template-columns:minmax(0,1fr)}.side{position:static;height:auto;border-right:0;border-bottom:1px solid var(--line);
padding:14px max(16px,env(safe-area-inset-left)) 12px}nav.views{grid-template-columns:repeat(2,minmax(0,1fr))}nav.views a{padding:7px 10px}
.work{padding:18px max(16px,env(safe-area-inset-right)) 24px max(16px,env(safe-area-inset-left))}.status{padding:6px 16px;position:static}.mapctl{position:static;max-width:none;justify-content:flex-start;margin-bottom:6px}}
)CSS";

/* ======================================================================== shared page pieces */
static std::string header(const std::string &title, const std::string &lede) {
    return "<header><h1>" + esc(title) + "</h1>" + (lede.empty() ? std::string() : "<p class=\"lede\">" + esc(lede) + "</p>") + "</header>";
}

static std::string dataTag(const Scene &sc, bool with_source = true) {
    std::string o;
    if (sc.cfg.source == "synthetic") o = "<span class=\"tag warn\">SYNTHETIC DATASET</span>";
    else if (sc.cfg.source == "real") o = "<span class=\"tag info\">REAL-WORLD DATA (as declared in scenario.cfg)</span>";
    else o = "<span class=\"tag warn\">MIXED REAL AND SYNTHETIC DATA</span>";
    if (with_source && !sc.cfg.source_note.empty()) o += " <span class=\"small muted\">Source: " + esc(sc.cfg.source_note) + "</span>";
    return o;
}

static void metric(std::string &o, const std::string &label, const std::string &value, const std::string &note, const char *accent = "") {
    o += "<div class=\"metric" + std::string(*accent ? " " : "") + accent + "\"><div class=\"ml\">" + esc(label) + "</div><div class=\"mv\" title=\"" + esc(value) + "\">" +
         esc(value) + "</div><div class=\"mn\" title=\"" + esc(note) + "\">" + esc(note) + "</div></div>";
}

static std::string section(const std::string &title, const std::string &hint = "") {
    return "<h2>" + esc(title) + "</h2>" + (hint.empty() ? std::string() : "<p class=\"hint\">" + esc(hint) + "</p>");
}

static std::string errorBox(const std::string &title, const std::string &text) {
    return "<div class=\"err\"><div class=\"t\">" + esc(title) + "</div><div class=\"mono\">" + esc(text) + "</div></div>";
}

/* ======================================================================== charts (SVG) */
// Bars with an optional part above a limit drawn in amber, and a dashed limit line.
static void barChart(std::string &o, const std::vector<std::string> &labels, const std::vector<double> &vals, double limit, const std::string &limit_label,
                     const std::string &xlab, const std::string &ylab, int W = 620, int H = 270) {
    int n = (int)vals.size();
    if (n == 0) return;
    int l = 52, r = 12, t = 20, b = 40;
    double ymax = limit > 0 ? limit * 1.12 : 1;
    for (double v : vals) ymax = std::max(ymax, v * 1.12);
    auto X = [&](double i) { return l + i / n * (W - l - r); };
    auto Y = [&](double v) { return t + (1 - v / ymax) * (H - t - b); };
    appf(o, "<svg class=\"chart\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"%s\">", W, H, esc(ylab + " by " + xlab).c_str());
    for (int g = 0; g <= 4; g++)
        appf(o, "<line class=\"gl\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tk\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(ymax * g / 4),
             Y(ymax * g / 4), l - 5, Y(ymax * g / 4) + 4, fint(ymax * g / 4).c_str());
    for (int i = 0; i < n; i++) {
        double v = vals[(size_t)i], under = limit > 0 ? std::min(v, limit) : v, bw = (W - l - r) / (double)n;
        double x = X(i) + bw * 0.16, w = bw * 0.68;
        appf(o, "<g><title>%s: %s</title><rect class=\"bar\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/>", esc(labels[(size_t)i]).c_str(), fint(v).c_str(), x, Y(under), w,
             Y(0) - Y(under));
        if (limit > 0 && v > limit) appf(o, "<rect class=\"bar over\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/>", x, Y(v), w, Y(limit) - Y(v));
        o += "</g>";
        if (n <= 12 || i % 3 == 0) appf(o, "<text class=\"tk\" x=\"%.1f\" y=\"%d\" text-anchor=\"middle\">%s</text>", X(i + 0.5), H - b + 15, esc(labels[(size_t)i]).c_str());
    }
    if (limit > 0)
        appf(o, "<line class=\"sup\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tkl\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(limit), Y(limit), W - r,
             Y(limit) - 5, esc(limit_label).c_str());
    appf(o, "<text class=\"tk\" x=\"%d\" y=\"%d\" text-anchor=\"middle\">%s</text><text class=\"tk\" x=\"%d\" y=\"12\">%s</text></svg>", (l + W - r) / 2, H - 6, esc(xlab).c_str(), 4,
         esc(ylab).c_str());
}

// Battery along a route: kWh after each move against distance, with reserve, charge target and elevation.
static void batteryChart(std::string &o, const std::vector<std::array<double, 3>> &profile, double battery, double reserve, double target, int W = 440, int H = 240) {
    int l = 44, r = 12, t = 16, b = 34;
    double total = profile.empty() ? 1 : std::max(1e-6, profile.back()[0]), B = std::max(1e-6, battery);
    double emin = 1e9, emax = -1e9;
    for (auto &p : profile) { emin = std::min(emin, p[2]); emax = std::max(emax, p[2]); }
    if (emax <= emin) emax = emin + 1;
    auto X = [&](double v) { return l + v / total * (W - l - r); };
    auto Y = [&](double v) { return t + (1 - v / B) * (H - t - b); };
    auto YE = [&](double v) { return t + (1 - (v - emin) / ((emax - emin) * 2.5)) * (H - t - b); };
    appf(o, "<svg class=\"chart\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"Battery and elevation along the route\">", W, H);
    for (int g = 0; g <= 4; g++)
        appf(o, "<line class=\"gl\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tk\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(B * g / 4), Y(B * g / 4),
             l - 5, Y(B * g / 4) + 4, fint(B * g / 4).c_str());
    if (!profile.empty()) {
        appf(o, "<path class=\"elev\" d=\"M%d %d", l, H - b);
        for (auto &p : profile) appf(o, "L%.1f %.1f", X(p[0]), YE(p[2]));
        appf(o, "L%.1f %dZ\"/>", X(profile.back()[0]), H - b);
    }
    appf(o, "<line class=\"tgt\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><line class=\"res\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/>", l, W - r, Y(target), Y(target), l, W - r, Y(reserve),
         Y(reserve));
    o += "<polyline class=\"bat\" points=\"";
    for (auto &p : profile) appf(o, "%.1f,%.1f ", X(p[0]), Y(std::max(0.0, p[1])));
    o += "\"/>";
    for (int g = 0; g <= 4; g++)
        appf(o, "<text class=\"tk\" x=\"%.1f\" y=\"%d\" text-anchor=\"%s\">%s%s</text>", X(total * g / 4), H - b + 16, g == 0 ? "start" : g == 4 ? "end" : "middle", fint(total * g / 4).c_str(),
             g == 4 ? " km" : "");
    appf(o, "<text class=\"tk\" x=\"4\" y=\"11\">kWh</text></svg>");
}

/* ======================================================================== the road network (SVG) */
struct MapT {
    double minx = 0, maxy = 0, s = 1, s0 = 1, ox = 0, oy = 0, z = 1, cx = 0, cy = 0;
    int W = 900, H = 600;
    double X(double x) const { return ox + (x - minx) * s; }
    double Y(double y) const { return oy + (maxy - y) * s; }
};

// Fits the city into W pixels; z > 1 zooms in around (cx, cy) in km.
static MapT mapFor(const City &C, int W, double z = 1, double cx = NAN, double cy = NAN, int maxH = 720) {
    MapT m;
    double dw = std::max(1e-6, C.maxx - C.minx), dh = std::max(1e-6, C.maxy - C.miny);
    m.W = W;
    m.H = (int)std::max(260.0, std::min((double)maxH, W * dh / dw));
    double pad = 22;
    m.s0 = std::min((W - 2 * pad) / dw, (m.H - 2 * pad) / dh);
    m.z = std::max(1.0, std::min(32.0, z));
    m.s = m.s0 * m.z;
    m.minx = C.minx;
    m.maxy = C.maxy;
    m.cx = std::isfinite(cx) ? std::max(C.minx, std::min(C.maxx, cx)) : (C.minx + C.maxx) / 2;
    m.cy = std::isfinite(cy) ? std::max(C.miny, std::min(C.maxy, cy)) : (C.miny + C.maxy) / 2;
    if (m.z <= 1) { m.cx = (C.minx + C.maxx) / 2; m.cy = (C.miny + C.maxy) / 2; }
    m.ox = W / 2.0 - (m.cx - m.minx) * m.s;
    m.oy = m.H / 2.0 - (m.maxy - m.cy) * m.s;
    return m;
}

struct Layers {
    bool terrain = true, junctions = true, names = false, existing = true, candidates = true, built = true, route = true, traffic = false;
};

static void drawBase(std::string &o, const City &C, const MapT &m, const Run *R, const Layers &L) {
    const Scene &sc = C.sc;
    const Terrain &T = C.terrain;
    double dw = C.maxx - C.minx, dh = C.maxy - C.miny;
    if (L.terrain && T.ok) {
        int g = 3, gw = T.gw / g, gh = T.gh / g;
        o += "<g class=\"ter\">";
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                double v = 0;
                for (int yy = 0; yy < g; yy++) for (int xx = 0; xx < g; xx++) v += T.V[(size_t)((y * g + yy) * T.gw + x * g + xx)];
                v /= g * g;
                double tt = (v - T.zmin) / std::max(1.0f, T.zmax - T.zmin);
                double x0 = m.X(C.minx + (double)x / gw * dw), y0 = m.Y(C.maxy - (double)y / gh * dh);
                double cw = dw / gw * m.s, ch = dh / gh * m.s;
                if (x0 > m.W || y0 > m.H || x0 + cw < 0 || y0 + ch < 0) continue;
                appf(o, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill-opacity=\"%.2f\"/>", x0, y0, cw + 0.6, ch + 0.6, 0.05 + 0.45 * tt);
            }
        o += "</g>";
        for (const auto &lev : T.levels) {
            appf(o, "<path class=\"ctr\" stroke-width=\"%s\" d=\"", lev.major ? "1.1" : "0.6");
            for (size_t k = 0; k + 3 < lev.segs.size(); k += 4)
                appf(o, "M%.0f %.0fL%.0f %.0f", m.X(lev.segs[k]), m.Y(lev.segs[k + 1]), m.X(lev.segs[k + 2]), m.Y(lev.segs[k + 3]));
            o += "\"/>";
        }
    }
    if (L.traffic && R) {
        std::string buckets[5];
        double wid[5] = { 1.0, 2.0, 3.4, 4.8, 6.5 };
        for (int e = 0; e < sc.ne; e++) {
            double r = (double)R->edge_load[(size_t)e] / std::max(1, R->edge_load_max);
            int b = r <= 0 ? 0 : r < 0.25 ? 1 : r < 0.5 ? 2 : r < 0.75 ? 3 : 4;
            const NodeV &a = sc.nodes[(size_t)sc.edges[(size_t)e].u], &n = sc.nodes[(size_t)sc.edges[(size_t)e].v];
            appf(buckets[b], "M%.1f %.1fL%.1f %.1f", m.X(a.x), m.Y(a.y), m.X(n.x), m.Y(n.y));
        }
        for (int b = 0; b < 5; b++)
            if (!buckets[b].empty()) appf(o, "<path class=\"tr%d\" fill=\"none\" stroke-linecap=\"round\" stroke-width=\"%.1f\" d=\"%s\"/>", b, wid[b], buckets[b].c_str());
    } else {
        o += "<path class=\"road\" stroke-width=\"1.2\" d=\"";
        for (int e = 0; e < sc.ne; e++) {
            const NodeV &a = sc.nodes[(size_t)sc.edges[(size_t)e].u], &n = sc.nodes[(size_t)sc.edges[(size_t)e].v];
            appf(o, "M%.1f %.1fL%.1f %.1f", m.X(a.x), m.Y(a.y), m.X(n.x), m.Y(n.y));
        }
        o += "\"/>";
    }
    // junction dots: on large networks only when zoomed in
    bool dots = L.junctions && (sc.nn <= 600 || m.z >= 2);
    if (dots) {
        o += "<g class=\"jn\">";
        for (int i = 0; i < sc.nn; i++) {
            double x = m.X(sc.nodes[(size_t)i].x), y = m.Y(sc.nodes[(size_t)i].y);
            if (x < -5 || y < -5 || x > m.W + 5 || y > m.H + 5) continue;
            appf(o, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"1.7\"/>", x, y);
        }
        o += "</g>";
    }
    if (L.names && (sc.nn <= 80 || m.z >= 3)) {
        for (int i = 0; i < sc.nn; i++) {
            double x = m.X(sc.nodes[(size_t)i].x), y = m.Y(sc.nodes[(size_t)i].y);
            if (x < 0 || y < 0 || x > m.W || y > m.H) continue;
            appf(o, "<text class=\"jname\" x=\"%.1f\" y=\"%.1f\">%s</text>", x + 4, y - 4, esc(sc.nodes[(size_t)i].name).c_str());
        }
    }
}

static void drawRoute(std::string &o, const Scene &sc, const MapT &m, const std::vector<int> &path, bool ok) {
    if (path.size() < 2) return;
    o += std::string("<polyline class=\"rt") + (ok ? "" : " bad") + "\" points=\"";
    for (int v : path) appf(o, "%.1f,%.1f ", m.X(sc.nodes[(size_t)v].x), m.Y(sc.nodes[(size_t)v].y));
    o += "\"/>";
    const NodeV &a = sc.nodes[(size_t)path.front()], &b = sc.nodes[(size_t)path.back()];
    appf(o, "<circle class=\"rs\" cx=\"%.1f\" cy=\"%.1f\" r=\"6\"/><circle class=\"re\" cx=\"%.1f\" cy=\"%.1f\" r=\"6\"/>", m.X(a.x), m.Y(a.y), m.X(b.x), m.Y(b.y));
}

static void drawStopBadges(std::string &o, const Scene &sc, const MapT &m, const std::vector<int> &stops) {
    for (size_t q = 0; q < stops.size(); q++) {
        const NodeV &n = sc.nodes[(size_t)sc.stations[(size_t)stops[q]].node];
        double x = m.X(n.x) - 13, y = m.Y(n.y) - 13;
        appf(o, "<circle class=\"bdg\" cx=\"%.1f\" cy=\"%.1f\" r=\"8.5\"/><text class=\"bdgt\" x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\">%d</text>", x, y, x, y + 4, (int)q + 1);
    }
}

// Station markers, as in the desktop app: existing = blue square, offline = red outline with a cross,
// candidate = hollow ring, new station = green diamond, amber ring = short of chargers.
static void drawStations(std::string &o, Ctx &c, const MapT &m, const Run *R, const Layers &L, int selected, bool links) {
    Engine &E = c.E;
    const Scene &sc = E.city().sc;
    for (int i = 0; i < sc.ns; i++) {
        const SiteV &si = sc.stations[(size_t)i];
        bool built = R && R->built[(size_t)i];
        if (si.existing && !L.existing) continue;
        if (!si.existing && built && !L.built) continue;
        if (!si.existing && !built && !L.candidates) continue;
        const NodeV &n = sc.nodes[(size_t)si.node];
        double x = m.X(n.x), y = m.Y(n.y);
        if (x < -12 || y < -12 || x > m.W + 12 || y > m.H + 12) continue;
        int ports = E.effectivePorts(i);
        std::string title = (si.existing ? "Existing station " : built ? "New station " : "Candidate site ") + std::to_string(si.id) + ", " + sc.nodes[(size_t)si.node].name + ": " +
                            std::to_string(ports) + " ports" + (si.existing ? (si.available ? "" : ", " + si.status) : ", " + rupees(si.cost));
        if (links) o += "<a href=\"" + c.url("/network", { { "station", std::to_string(si.id) }, { "mode", "" } }) + "\">";
        o += "<g><title>" + esc(title) + "</title>";
        double r;
        if (si.existing) {
            r = 4.2 + std::sqrt((double)ports) * 0.9;
            if (si.available) appf(o, "<rect class=\"ex\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/>", x - r, y - r, 2 * r, 2 * r);
            else appf(o, "<rect class=\"offl\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/><path class=\"x\" d=\"M%.1f %.1fL%.1f %.1fM%.1f %.1fL%.1f %.1f\"/>", x - r, y - r, 2 * r, 2 * r,
                      x - r + 1.5, y - r + 1.5, x + r - 1.5, y + r - 1.5, x - r + 1.5, y + r - 1.5, x + r - 1.5, y - r + 1.5);
        } else if (built) {
            r = 5.5 + std::sqrt((double)ports) * 1.0;
            appf(o, "<path class=\"newst\" d=\"M%.1f %.1fL%.1f %.1fL%.1f %.1fL%.1f %.1fZ\"/>", x, y - r, x + r, y, x, y + r, x - r, y);
        } else {
            r = 4.2;
            appf(o, "<circle class=\"cand\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", x, y, r);
        }
        if (R && R->short_of[(size_t)i] > 0 && (si.existing ? si.available : built)) appf(o, "<circle class=\"shortr\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", x, y, r + 3.5);
        appf(o, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"10\" fill=\"transparent\"/></g>", x, y);
        if (links) o += "</a>";
        if (E.plan.edits.lock.count(i)) appf(o, "<rect class=\"lockr\" x=\"%.1f\" y=\"%.1f\" width=\"20\" height=\"20\" rx=\"2\"/>", x - 10, y - 10);
        if (E.plan.edits.ban.count(i)) appf(o, "<path class=\"x\" d=\"M%.1f %.1fL%.1f %.1fM%.1f %.1fL%.1f %.1f\"/>", x - 7, y - 7, x + 7, y + 7, x - 7, y + 7, x + 7, y - 7);
        if (E.plan.edits.ports.count(i)) appf(o, "<text class=\"plbl\" x=\"%.1f\" y=\"%.1f\">%dp</text>", x + 9, y - 9, ports);
        if (i == selected) appf(o, "<circle class=\"selr\" cx=\"%.1f\" cy=\"%.1f\" r=\"14\"/>", x, y);
    }
}

static void drawScaleBar(std::string &o, const MapT &m) {
    double target = 110 / m.s, nice = 1;
    for (double c : { 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0 }) if (c <= target) nice = c;
    double px = nice * m.s, x0 = 16, y0 = m.H - 16;
    appf(o, "<path class=\"scale\" fill=\"none\" d=\"M%.1f %.1fv-5M%.1f %.1fh%.1fv-5\"/><text class=\"tkl\" x=\"%.1f\" y=\"%.1f\">%s km</text>", x0, y0, x0, y0, px, x0, y0 - 9,
         nice < 1 ? f1(nice).c_str() : fint(nice).c_str());
}

static std::string legendItem(const std::string &svg, const std::string &label) {
    return "<div><svg width=\"18\" height=\"18\" viewBox=\"0 0 18 18\" aria-hidden=\"true\">" + svg + "</svg>" + esc(label) + "</div>";
}

static std::string planLegend(const Layers &L, bool route) {
    std::string o = "<div class=\"legend\">";
    if (L.traffic) o += legendItem("<path d=\"M1 9h16\" stroke=\"var(--error)\" stroke-width=\"5\"/>", "Busy road (planned trips)");
    else o += legendItem("<path d=\"M1 9h16\" stroke=\"var(--ink3)\" stroke-width=\"1.5\"/>", "Road");
    o += legendItem("<rect class=\"ex\" x=\"4\" y=\"4\" width=\"10\" height=\"10\"/>", "Existing station");
    o += legendItem("<rect class=\"offl\" x=\"4\" y=\"4\" width=\"10\" height=\"10\"/><path class=\"x\" d=\"M5 5l8 8M5 13l8-8\"/>", "Offline or in maintenance");
    o += legendItem("<circle class=\"cand\" cx=\"9\" cy=\"9\" r=\"4.5\"/>", "Candidate site (not selected)");
    o += legendItem("<path class=\"newst\" d=\"M9 2l7 7-7 7-7-7z\"/>", "New station (selected)");
    o += legendItem("<circle class=\"shortr\" cx=\"9\" cy=\"9\" r=\"6.5\"/>", "Short of chargers");
    if (route) {
        o += legendItem("<path class=\"rt\" d=\"M2 9h14\"/>", "Route");
        o += legendItem("<circle class=\"bdg\" cx=\"9\" cy=\"9\" r=\"7.5\"/><text class=\"bdgt\" x=\"9\" y=\"13\" text-anchor=\"middle\">1</text>", "Charging stop");
    }
    if (L.terrain) o += legendItem("<path class=\"ctr\" stroke-width=\"1.1\" d=\"M1 12c4-6 10-6 16-2\"/>", "Contour line, every 50 m");
    return o + "</div>";
}

/* ======================================================================== page frame */
static std::string frame(Ctx &c, const std::string &body, const std::string &head_extra = "") {
    Engine &E = c.E;
    const City &C = E.city();
    const Scene &sc = C.sc;
    std::string theme = c.get("theme");
    std::string o = "<!doctype html><html lang=\"en\"";
    if (theme == "dark" || theme == "light") o += " data-theme=\"" + theme + "\"";
    o += "><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    appf(o, "<title>%s \xC2\xB7 EV33 Charge Atlas</title>", VIEWS[c.view].name);
    o += "<meta name=\"description\" content=\"EV33 Charge Atlas: EV charging station placement and trip planning, computed by a C++17 engine.\">";
    o += "<link rel=\"icon\" href=\"data:,\">" + head_extra + "<style>" + CSS + "</style></head><body><div class=\"app\"><aside class=\"side\">";
    // brand: the same charge-point glyph as the desktop app
    o += "<a class=\"brand\" href=\"" + c.url("/") + "\"><svg width=\"30\" height=\"30\" viewBox=\"0 0 26 26\" aria-hidden=\"true\"><path d=\"M4 22L13 5\" stroke=\"var(--primary)\" "
         "stroke-width=\"2.4\" stroke-linecap=\"round\"/><path d=\"M13 5L22 22\" stroke=\"var(--ink2)\" stroke-width=\"2.4\" stroke-linecap=\"round\"/><circle cx=\"13\" cy=\"15\" "
         "r=\"3.2\" fill=\"var(--secondary)\"/></svg><span><b>EV33</b><span>Charge Atlas</span></span></a>";
    // dataset
    o += "<form method=\"get\" action=\"" + std::string(VIEWS[c.view].path) + "\"><label class=\"lbl\" for=\"city\">Dataset</label><div class=\"dsrow\">" +
         c.hidden({ "city", "budget", "edit", "station", "trip", "from", "to", "calc", "z", "cx", "cy", "check" }, false) + "<select id=\"city\" name=\"city\">";
    for (int i = 0; i < (int)E.cities.size(); i++) {
        const City &ci = E.cities[(size_t)i];
        o += "<option value=\"" + esc(ci.name) + "\"" + (i == E.plan.city ? " selected" : "") + ">" + esc(ci.label) + "</option>";
    }
    o += "</select><button>Open</button></div></form><div>" + dataTag(sc, false) + "</div>";
    // views
    o += "<nav class=\"views\" aria-label=\"Views\">";
    for (int v = 0; v < V_COUNT; v++)
        o += "<a href=\"" + c.url(VIEWS[v].path) + "\"" + (v == c.view ? " class=\"on\" aria-current=\"page\"" : "") + ">" + VIEWS[v].name + "<span>" + std::to_string(v + 1) + "</span></a>";
    o += "</nav><div class=\"foot\"><div><span class=\"lbl\">Current plan</span><div class=\"plan\">" + esc(E.planLabel(E.plan)) + "</div></div><div class=\"seg full\">";
    static const char *TH[][2] = { { "", "Auto" }, { "light", "Light" }, { "dark", "Charcoal" } };
    for (auto &t : TH) o += "<a href=\"" + c.url(VIEWS[c.view].path, { { "theme", t[0] } }) + "\"" + (theme == t[0] ? " class=\"on\"" : "") + ">" + t[1] + "</a>";
    o += "</div></div></aside><main><div class=\"work\">" + body + "</div><div class=\"status\"><span>";
    std::string st = E.status;
    if (st.empty() || st[0] == '!') st = (st.empty() ? std::string() : st.substr(1) + " ") + C.label + ": " + E.planLabel(E.plan);
    o += esc(st) + "</span><span>C++17 engine \xC2\xB7 same results as the desktop app</span></div></main></div></body></html>";
    return o;
}

/* ======================================================================== 1. Dashboard */
static std::string pageDashboard(Ctx &c) {
    Engine &E = c.E;
    City &C = E.city();
    const Scene &sc = C.sc;
    Run *R = E.run();
    std::string o = header("Dashboard", "Network, stations and trips of \"" + C.label + "\", with the result of the current plan: " + E.planLabel(E.plan) +
                                            ". Change the plan in Station Placement.");
    o += "<div>" + dataTag(sc) + "</div>";
    if (!R) return o + errorBox("The plan could not be computed.", E.status);
    int offline = 0;
    for (int i = 0; i < sc.nexist; i++) offline += !sc.stations[(size_t)i].available;
    o += "<section><h3>Dataset</h3><div class=\"metrics\" style=\"margin-top:8px\">";
    metric(o, "Road nodes", fint(sc.nn), "junctions in the road network");
    metric(o, "Road edges", fint(sc.ne), "two-way road segments");
    metric(o, "Existing charging stations", fint(sc.nexist), offline ? fint(offline) + " offline or in maintenance" : std::string("all available"), "s");
    metric(o, "Candidate locations", fint(sc.candidates()), "sites where a new station may be built");
    metric(o, "Planned trips", fint(sc.nt), fint(sc.nveh) + " vehicle models");
    o += "</div></section>";
    const SelV &sel = R->run.sel;
    double budget = E.plan.budget, remaining = budget - sel.cost;
    o += "<section><h3>Placement result <span class=\"small muted\" style=\"font-weight:400\">" + esc(SELECT_LONG[E.plan.sel]) + "</span></h3><div class=\"metrics\" style=\"margin-top:8px\">";
    metric(o, "Selected charging stations", fint(sel.n), sel.optimal ? "provably optimal for the benefit scores" : "heuristic choice (not guaranteed optimal)", "p");
    metric(o, "Installation budget", rupees(budget), C.default_budget == E.plan.budget ? "the dataset's budget" : "changed in Station Placement");
    metric(o, "Total installation cost", rupees(sel.cost), "benefit score " + fint(sel.value));
    metric(o, "Remaining budget", rupees(remaining), sel.over_budget ? "locked sites alone exceed the budget" : "never negative: the budget is a hard limit", sel.over_budget ? "e" : "");
    o += "</div></section>";

    const KpiV &k = R->run.kpi;
    o += "<div class=\"cols\"><section class=\"panel\">" + section("Trips with this plan") + "<p><span class=\"big\">" + f1(k.feasible_pct) + "%</span><span class=\"ink2\">of trips can be completed (" +
         fint(k.feasible) + " of " + fint(k.trips) + ")</span></p><table class=\"kv\"><tbody>";
    auto kv = [&](const std::string &a, const std::string &b) { o += "<tr><th>" + esc(a) + "</th><td>" + esc(b) + "</td></tr>"; };
    kv("Shortest route beyond the starting charge", fint(k.need_charge) + " trips, " + fint(k.charged_ok) + " still completed");
    kv("Not possible", fint(k.infeasible) + " trips (reasons in Trip Planner)");
    kv("Average completed trip", f1(k.avg_km) + " km, " + f2(k.avg_kwh) + " kWh");
    kv("Average charging", f2(k.avg_stops) + " stops, " + f1(k.avg_charge_min) + " min");
    kv("Average door-to-door time", fdur(k.avg_travel_min));
    kv("Computed in", fms(R->total_ms) + " (" + fint((double)R->run.sssp) + " shortest-path trees)");
    o += "</tbody></table></section>";
    const CapV &cap = R->cap;
    o += "<section class=\"panel\">" + section("Charging capacity") + "<p><span class=\"big\">" + fint((double)cap.flow_dfs) + "</span><span class=\"ink2\">of " + fint(cap.trips_needing) +
         " charge-needing trips fit the port slots</span></p><p class=\"ink2\">" + md(flowNote(sc, cap)) + "</p><p>" +
         (cap.consistent ? "<span class=\"tag ok\">Ford\xE2\x80\x93" "Fulkerson = Edmonds\xE2\x80\x93Karp = min cut = bipartite matching</span>" : "<span class=\"tag bad\">The flow methods disagree</span>") +
         "</p><p class=\"ink2\">" + md(surgeNote(R->ses)) + "</p><p class=\"ink2\">" + fint((double)R->ses.served) + " of " + fint((double)R->ses.sessions) +
         " planned charging sessions can be served with the installed ports.</p></section></div>";

    o += "<div class=\"cols\"><section class=\"panel\">" + section("Charging energy by hour", "Bars: kWh drawn by planned charging sessions. Dashed line: grid supply per hour. Amber: energy above the supply.");
    std::vector<std::string> labels;
    for (size_t h = 0; h < R->ses.hourly_kwh.size(); h++) { char b[8]; std::snprintf(b, sizeof b, "%02d", (int)h); labels.push_back(b); }
    barChart(o, labels, R->ses.hourly_kwh, sc.cfg.grid_kw, "grid supply " + fint(sc.cfg.grid_kw) + " kWh", "hour of the day", "kWh");
    o += "</section><section class=\"panel\">" + section("Busiest charging stations", "Sessions planned at each station against the chargers it has (interval partitioning gives the chargers needed).");
    std::vector<LoadV> loads = R->ses.st;
    ev::merge_sort(loads, [](const LoadV &a, const LoadV &b) { return a.sessions > b.sessions; });
    o += "<div class=\"scroll\"><table><thead><tr><th>Station</th><th>Kind</th><th>Sessions</th><th>Chargers needed</th><th>Has</th></tr></thead><tbody>";
    int shown = 0;
    for (size_t i = 0; i < loads.size() && i < 8; i++) {
        const LoadV &L = loads[i];
        if (L.sessions == 0) break;
        const SiteV &s = sc.stations[(size_t)L.station];
        int has = R->ports[(size_t)L.station];
        o += "<tr><td>" + std::to_string(s.id) + " &nbsp;" + esc(s.name) + "</td><td class=\"" + (s.existing ? "ink2" : "good") + "\">" + (s.existing ? "existing" : "new") + "</td><td>" +
             std::to_string(L.sessions) + "</td><td class=\"" + (L.chargers_needed > has ? "bad" : "") + "\">" + std::to_string(L.chargers_needed) + "</td><td>" + std::to_string(has) + "</td></tr>";
        shown++;
    }
    if (!shown) o += "<tr><td colspan=\"5\" class=\"muted\">No charging sessions in this plan.</td></tr>";
    o += "</tbody></table></div></section></div>";
    return o;
}

/* ======================================================================== 2. Trip Planner */
static std::string pageTrip(Ctx &c) {
    Engine &E = c.E;
    const Scene &sc = E.city().sc;
    Run *R = E.run();
    std::string o = header("Trip Planner", "Plan one trip on the current city. The route keeps the battery above the vehicle's reserve and adds charging stops where needed; if that is "
                                           "impossible, the reason is shown.");
    o += "<div>" + dataTag(sc) + "</div>";
    if (!R || sc.nn == 0) return o + errorBox("The plan could not be computed.", E.status);

    // ---- the form's values: typed in, or filled from a dataset trip or a vehicle model
    int fill_trip = -1;
    if (c.has("trip"))
        for (int i = 0; i < sc.nt; i++) if (sc.trips[(size_t)i].id == std::atoi(c.get("trip").c_str())) fill_trip = i;
    if (fill_trip < 0 && !c.has("from") && !R->samples.empty()) fill_trip = R->samples[0].trip;
    std::string f_from, f_to, f_ev, f_bat, f_chg, f_cons, f_res, f_dep;
    int method = std::max(0, std::min(PLANNER_COUNT - 1, std::atoi(c.get("method", c.get("dm", "0").c_str()).c_str())));
    if (method == PLANNER_LEG_FLOYD && sc.nn > FLOYD_MAX_NODES) method = PLANNER_EXACT;
    bool typed = c.has("from") || c.has("to");
    if (!typed && fill_trip >= 0) {
        const TripV &tr = sc.trips[(size_t)fill_trip];
        const VehicleV &v = sc.vehicles[(size_t)tr.vehicle];
        f_from = std::to_string(sc.nodes[(size_t)tr.src].id);
        f_to = std::to_string(sc.nodes[(size_t)tr.dst].id);
        f_ev = v.id;
        f_bat = num(v.battery_kwh, 2);
        f_chg = num(tr.start_kwh, 2);
        f_cons = num(v.consumption, 3);
        f_res = num(v.reserve_kwh, 2);
        char b[8];
        std::snprintf(b, sizeof b, "%02d:%02d", tr.depart_min / 60 % 24, tr.depart_min % 60);
        f_dep = b;
    } else {
        f_from = c.get("from"); f_to = c.get("to"); f_ev = c.get("ev", "custom"); f_bat = c.get("bat"); f_chg = c.get("chg"); f_cons = c.get("cons"); f_res = c.get("res"); f_dep = c.get("dep", "08:00");
    }
    int model = -1;
    for (int i = 0; i < sc.nveh; i++) if (sc.vehicles[(size_t)i].id == f_ev) model = i;
    if (c.has("usemodel") && model >= 0) {   // "Use the model's values"
        const VehicleV &v = sc.vehicles[(size_t)model];
        f_bat = num(v.battery_kwh, 2);
        f_cons = num(v.consumption, 3);
        f_res = num(v.reserve_kwh, 2);
        double ch;
        if (!toNumber(f_chg, ch) || ch > v.battery_kwh) f_chg = num(v.battery_kwh * 0.6, 2);
    }

    // ---- calculate when asked
    TripAnswer a;
    std::vector<std::string> problems;
    bool calc = c.get("calc") == "1" && !c.has("usemodel");
    if (calc) {
        TripQuery q;
        double from_id = 0, to_id = 0, bat = 0, chg = 0, cons = 0, res = 0;
        if (!toNumber(f_from, from_id) || (q.src = nodeById(sc, (long)from_id)) < 0) problems.push_back("Source junction: enter a node id that exists in this network.");
        if (!toNumber(f_to, to_id) || (q.dst = nodeById(sc, (long)to_id)) < 0) problems.push_back("Destination junction: enter a node id that exists in this network.");
        if (!toNumber(f_bat, bat)) problems.push_back("Battery capacity: enter a number of kWh.");
        if (!toNumber(f_chg, chg)) problems.push_back("Initial battery charge: enter a number of kWh.");
        if (!toNumber(f_cons, cons)) problems.push_back("Energy consumption: enter kWh per km.");
        if (!toNumber(f_res, res)) problems.push_back("Minimum battery reserve: enter a number of kWh.");
        int hh = -1, mm = -1;
        if (std::sscanf(f_dep.c_str(), "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) problems.push_back("Departure time: use hh:mm, for example 08:30.");
        if (problems.empty()) {
            if (model >= 0) {
                const VehicleV &m = sc.vehicles[(size_t)model];
                q.vehicle.id = m.id;
                q.vehicle.model = m.model;
                q.vehicle.connector = m.connector;
                q.vehicle.max_charge_kw = m.max_charge_kw;
            } else {
                q.vehicle.id = "custom";
                q.vehicle.model = "Custom vehicle";
                q.vehicle.connector = "CCS2";
            }
            q.vehicle.battery_kwh = bat;
            q.vehicle.charge_kwh = std::min(chg, bat);
            q.vehicle.consumption_kwh_per_km = cons;
            q.vehicle.reserve_kwh = res;
            q.start_kwh = chg;
            q.depart_min = hh * 60 + mm;
            q.method = method;
            a = E.planTrip(q);
        }
    }

    // ---- form
    o += "<div class=\"trip\"><section class=\"panel\">" + section("Trip");
    o += "<form method=\"get\" action=\"/trip\" class=\"field\">" + c.hidden({ "trip", "from", "to", "ev", "bat", "chg", "cons", "res", "dep", "calc" }) +
         "<label for=\"trip\">Start from a trip in the dataset (optional)</label><select id=\"trip\" name=\"trip\" class=\"wide\">";
    for (const Sample &s : R->samples) {
        const TripV &tr = sc.trips[(size_t)s.trip];
        std::string l = "Trip " + std::to_string(tr.id) + ": " + sc.nodes[(size_t)tr.src].name + " to " + sc.nodes[(size_t)tr.dst].name + ", " + f1(s.km) + " km, " +
                        (s.kind == 2 ? std::to_string(s.stops) + " stops" : s.kind == 1 ? "1 stop" : s.kind == 0 ? "direct" : "not possible");
        o += "<option value=\"" + std::to_string(tr.id) + "\"" + (s.trip == fill_trip && !typed ? " selected" : "") + ">" + esc(l) + "</option>";
    }
    o += "</select><div><button>Use this trip</button></div></form>";
    o += "<form method=\"get\" action=\"/trip\">" + c.hidden({ "trip", "from", "to", "ev", "bat", "chg", "cons", "res", "dep", "method", "calc" });
    auto nodeNote = [&](const std::string &v) {
        double id;
        int k = toNumber(v, id) ? nodeById(sc, (long)id) : -1;
        return k >= 0 ? "<span class=\"note\">" + esc(sc.nodes[(size_t)k].name) + ", " + fint(sc.nodes[(size_t)k].elev) + " m</span>"
                      : std::string("<span class=\"note bad\">no junction with this id</span>");
    };
    o += "<div class=\"field\"><label for=\"from\">Source junction (node id)</label><div class=\"inl\"><input id=\"from\" name=\"from\" inputmode=\"numeric\" value=\"" + esc(f_from) + "\">" +
         nodeNote(f_from) + "</div></div>";
    o += "<div class=\"field\"><label for=\"to\">Destination junction (node id)</label><div class=\"inl\"><input id=\"to\" name=\"to\" inputmode=\"numeric\" value=\"" + esc(f_to) + "\">" +
         nodeNote(f_to) + "</div><span class=\"note\">Node ids run from " + std::to_string(sc.nodes.front().id) + " to " + std::to_string(sc.nodes.back().id) + " in this city.</span></div>";
    o += "<h2 style=\"margin-top:14px\">Vehicle</h2><div class=\"field\"><label for=\"ev\">EV model</label><select id=\"ev\" name=\"ev\" class=\"wide\">";
    for (int i = 0; i <= sc.nveh; i++) {
        std::string id = i < sc.nveh ? sc.vehicles[(size_t)i].id : "custom";
        std::string l = i < sc.nveh ? sc.vehicles[(size_t)i].id + "  " + sc.vehicles[(size_t)i].model : "Custom vehicle";
        o += "<option value=\"" + esc(id) + "\"" + ((i < sc.nveh ? i == model : model < 0) ? " selected" : "") + ">" + esc(l) + "</option>";
    }
    o += "</select><div class=\"row\"><button name=\"usemodel\" value=\"1\" title=\"Fill battery, consumption and reserve from the chosen model\">Use model values</button>";
    if (model >= 0) {
        const VehicleV &v = sc.vehicles[(size_t)model];
        o += "<span class=\"note\">Connector " + esc(v.connector.empty() ? std::string("not recorded") : v.connector) +
             (v.max_charge_kw > 0 ? ", charges at up to " + fint(v.max_charge_kw) + " kW" : std::string()) + "</span>";
    }
    o += "</div></div>";
    auto numField = [&](const char *id, const char *lab, const std::string &v, const std::string &unit) {
        o += "<div class=\"field\"><label for=\"" + std::string(id) + "\">" + lab + "</label><div class=\"inl\"><input id=\"" + id + "\" name=\"" + id +
             "\" inputmode=\"decimal\" value=\"" + esc(v) + "\"><span class=\"note\">" + esc(unit) + "</span></div></div>";
    };
    double bv = 0, cv = 0, kv = 0, rv = 0;
    bool nb = toNumber(f_bat, bv), nc = toNumber(f_chg, cv);
    numField("bat", "Battery capacity", f_bat, "kWh");
    numField("chg", "Initial battery charge", f_chg, nb && nc && bv > 0 ? "kWh (" + fint(cv / bv * 100) + "%)" : "kWh");
    numField("cons", "Energy consumption on flat road", f_cons, "kWh per km");
    numField("res", "Minimum battery reserve", f_res, "kWh, never planned below");
    if (toNumber(f_cons, kv) && toNumber(f_res, rv) && nc && kv > 0 && cv > rv)
        o += "<p class=\"note\">About " + fint((cv - rv) / kv) + " km on flat road before reaching the reserve.</p>";
    o += "<div class=\"field\"><label for=\"dep\">Departure time</label><div class=\"inl\"><input id=\"dep\" name=\"dep\" type=\"time\" value=\"" + esc(f_dep) + "\"></div></div>";
    o += "<h2 style=\"margin-top:14px\">Routing algorithm</h2><div class=\"field\"><select name=\"method\" aria-label=\"Routing algorithm\">";
    for (int m = 0; m < PLANNER_COUNT; m++) {
        bool off = m == PLANNER_LEG_FLOYD && sc.nn > FLOYD_MAX_NODES;
        o += "<option value=\"" + std::to_string(m) + "\"" + (m == method ? " selected" : "") + (off ? " disabled" : "") + ">" + esc(PLANNER_LABEL[m]) + (off ? " (not offered above 1,600 junctions)" : "") + "</option>";
    }
    int avail = 0;
    for (int i = 0; i < sc.nexist; i++) avail += sc.stations[(size_t)i].available;
    o += "</select><span class=\"note\">" + esc(PLANNER_HINT[method]) + "</span><span class=\"note\">Charging is possible at the " + std::to_string(avail) +
         " available existing stations and the " + std::to_string(R->run.sel.n) + " new stations of the current placement, where the connector matches.</span></div>";
    o += "<input type=\"hidden\" name=\"calc\" value=\"1\"><button class=\"primary wide\" style=\"padding:9px\">Calculate route</button></form></section><div style=\"display:grid;gap:14px;min-width:0\">";

    // ---- the answer
    if (!problems.empty()) {
        o += "<div class=\"err\"><div class=\"t\">Nothing was planned. Correct these fields:</div><ul style=\"margin:6px 0 0;padding-left:20px\">";
        for (auto &p : problems) o += "<li>" + esc(p) + "</li>";
        o += "</ul></div></div></div>";
        return o;
    }
    if (!a.done) {
        o += "<section class=\"panel\">" + section("No route calculated yet") +
             "<p class=\"ink2\">Choose two junctions and a vehicle, then press Calculate route. The result shows the route, the charging stops, the battery level along the way and, "
             "when the trip is not possible, the reason.</p></section></div></div>";
        return o;
    }
    if (!a.error.empty()) {
        o += errorBox("Nothing was planned.", a.error) + "</div></div>";
        return o;
    }
    const ev::TripPlan &p = a.plan;
    const ev::ElectricVehicle &veh = a.query.vehicle;
    bool ok = p.feasible();
    o += "<div class=\"row\">" + std::string(ok ? (p.stops.empty() ? "<span class=\"tag ok\">FEASIBLE, NO CHARGING NEEDED</span>" : "<span class=\"tag ok\">FEASIBLE WITH CHARGING</span>")
                                                  : "<span class=\"tag bad\">NOT FEASIBLE</span>") +
         " <span class=\"mono ink2\">status = " + esc(ev::to_string(p.status)) + "</span><span class=\"small muted\">" + esc(PLANNER_LABEL[a.query.method]) + ", " + esc(fms(a.ms)) +
         (a.states ? ", " + fint((double)a.states) + " states settled" : std::string()) + "</span></div><p style=\"margin:0\">" + esc(p.explanation) + "</p><div class=\"metrics six\">";
    if (ok) {
        metric(o, "Total distance", f1(p.distance_km) + " km", "shortest road route " + f1(p.shortest_km) + " km");
        metric(o, "Energy used", f2(p.energy_kwh) + " kWh", "for this vehicle on this route");
        metric(o, "Driving time", fdur(p.drive_min), "from road travel times");
        metric(o, "Charging", p.stops.empty() ? std::string("none") : fdur(p.charge_min), fint((double)p.stops.size()) + (p.stops.size() == 1 ? " stop" : " stops"));
        metric(o, "Arrival", fclock(p.arrive_min), "departs " + fclock(p.depart_min));
        metric(o, "Battery on arrival", f2(p.arrive_kwh) + " kWh", "lowest " + f2(p.min_kwh) + " kWh, reserve " + f2(veh.reserve_kwh));
    } else {
        metric(o, "Shortest road route", p.shortest_km >= 0 ? f1(p.shortest_km) + " km" : std::string("none"), p.shortest_km >= 0 ? "ignoring the battery" : "no road connects them");
        metric(o, "Energy it needs", p.shortest_km >= 0 ? f2(p.shortest_kwh) + " kWh" : std::string("\xE2\x80\x93"), "for this vehicle");
        metric(o, "Usable at departure", f2(a.query.start_kwh - veh.reserve_kwh) + " kWh", "start charge minus reserve");
    }
    o += "</div><div class=\"cols\"><section class=\"panel\">" + section("Battery along the route", "Green: battery. Red: reserve. Dashed: charge target at a stop. Shaded: elevation profile.");
    batteryChart(o, a.profile, veh.battery_kwh, veh.reserve_kwh, sc.cfg.target_frac * veh.battery_kwh);
    o += "</section><section class=\"panel\">" + section(ok ? "Route on the map" : "Shortest road route (not drivable)");
    {
        const City &C = E.city();
        MapT m = mapFor(C, 520, 1, NAN, NAN, 380);
        Layers L;
        L.junctions = false;
        L.candidates = false;
        appf(o, "<svg class=\"map\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"Route on the road network\">", m.W, m.H);
        drawBase(o, C, m, R, L);
        drawRoute(o, sc, m, a.path, ok);
        drawStations(o, c, m, R, L, -1, false);
        if (ok) drawStopBadges(o, sc, m, a.stop_station);
        drawScaleBar(o, m);
        o += "</svg>";
    }
    o += "</section></div>";
    if (ok && !p.stops.empty()) {
        o += "<section class=\"panel\">" + section("Charging stops") + "<div class=\"scroll\"><table><thead><tr><th>#</th><th>Station</th><th>Junction</th><th>Arrive</th><th>Arrive with</th>"
             "<th>Charge</th><th>Leave with</th></tr></thead><tbody>";
        for (size_t k = 0; k < p.stops.size(); k++) {
            const ev::ChargingStop &st = p.stops[k];
            const SiteV &s = sc.stations[(size_t)a.stop_station[k]];
            o += "<tr><td>" + std::to_string(k + 1) + "</td><td>" + std::to_string(s.id) + " " + esc(s.name) + " (" + (s.existing ? "existing" : "new") + ")</td><td>" +
                 esc(sc.nodes[(size_t)st.node].name) + "</td><td>" + fclock(st.arrive_min) + "</td><td>" + f2(st.arrive_kwh) + " kWh</td><td>" + esc(fdur(st.charge_min)) + " at " +
                 fint(s.power_kw) + " kW</td><td>" + f2(st.depart_kwh) + " kWh</td></tr>";
        }
        o += "</tbody></table></div></section>";
    }
    std::string seq;
    size_t next = 0;
    for (size_t i = 0; i < a.path.size(); i++) {
        if (i) seq += "  \xE2\x86\x92  ";
        seq += sc.nodes[(size_t)a.path[i]].name;
        if (ok && next < p.stops.size() && p.stops[next].route_pos == i) { seq += " **[charge " + std::to_string(next + 1) + "]**"; next++; }
        if (seq.size() > 6000) { seq += "  \xE2\x80\xA6 (" + fint((double)(a.path.size() - i - 1)) + " more)"; break; }
    }
    if (a.path.size() <= 1) seq = ok ? "Origin and destination are the same junction." : "No road path exists between these junctions.";
    o += "<section class=\"panel\">" + section("Route sequence", ok ? "Junctions in driving order; charging stops are marked." : "Junctions of the plain shortest road route.") +
         "<p class=\"seq\">" + md(seq) + "</p></section></div></div>";
    return o;
}

/* ======================================================================== 3. Station Placement */
static std::mutex g_cmp_mu;
static std::map<int, Compare> g_cmp;

// All fifteen runs per city take up to ~20 s on the large city, so a worker thread computes them at
// start-up with its own Engine; the request thread only copies its finished rows under the mutex.
static void compareWorker(std::string data, std::string results, std::vector<int> order) {
    Engine W;
    if (!W.init(data, results)) return;
    for (int i : order) {
        if (!W.load(i)) {
            std::lock_guard<std::mutex> g(g_cmp_mu);
            g_cmp[i].done = true;
            continue;
        }
        Compare &C = W.compare(i);
        while (!C.done) {
            W.stepCompare(C);
            std::lock_guard<std::mutex> g(g_cmp_mu);
            g_cmp[i] = C;
        }
    }
}

static std::string portsCell(Ctx &c, int i, const char *page) {
    Engine &E = c.E;
    const Scene &sc = E.city().sc;
    int ports = E.effectivePorts(i);
    auto link = [&](int n, const char *label, bool off) {
        Edits w = E.plan.edits;
        n = std::max(1, std::min(64, n));
        if (n == E.basePorts(i)) w.ports.erase(i); else w.ports[i] = n;
        return "<a class=\"btn sm" + std::string(off ? " dis" : "") + "\" aria-label=\"" + (label[0] == '+' ? "one more port" : "one port fewer") + "\" href=\"" +
               c.url(page, { { "edit", editKey(sc, w) } }) + "\">" + label + "</a>";
    };
    return "<span class=\"row\" style=\"gap:6px;flex-wrap:nowrap\">" + link(ports - 1, "\xE2\x88\x92", ports <= 1) + "<span class=\"mono\">" + std::to_string(ports) + "</span>" +
           link(ports + 1, "+", ports >= 64) + "</span>";
}

static std::string decisionCell(Ctx &c, int i, const char *page) {
    Engine &E = c.E;
    const Scene &sc = E.city().sc;
    int mode = E.plan.edits.lock.count(i) ? 1 : E.plan.edits.ban.count(i) ? 2 : 0;
    static const char *M[] = { "Auto", "Always", "Never" };
    std::string o = "<span class=\"seg sm\">";
    for (int m = 0; m < 3; m++) {
        Edits w = E.plan.edits;
        w.lock.erase(i);
        w.ban.erase(i);
        if (m == 1) w.lock.insert(i);
        if (m == 2) w.ban.insert(i);
        o += "<a" + std::string(m == mode ? (m == 2 ? " class=\"on red\"" : " class=\"on\"") : "") + " href=\"" + c.url(page, { { "edit", editKey(sc, w) } }) + "\">" + M[m] + "</a>";
    }
    return o + "</span>";
}

static std::string pagePlacement(Ctx &c, std::string &head) {
    Engine &E = c.E;
    City &C = E.city();
    const Scene &sc = C.sc;
    std::string o = header("Station Placement", "Choose which candidate sites to build within the installation budget. The 0/1 knapsack treats each site as an item: weight = installation "
                                                "cost, value = benefit score.");
    o += "<div>" + dataTag(sc) + "</div>";
    // ---- controls
    double total = 0;
    for (int i = sc.nexist; i < sc.ns; i++) total += sc.stations[(size_t)i].cost;
    o += "<section class=\"panel\"><form method=\"get\" action=\"/placement\" class=\"row\" style=\"align-items:flex-end;gap:16px\">" + c.hidden({ "sel", "budget" }) +
         "<div class=\"field\" style=\"margin:0\"><label for=\"sel\">Placement algorithm</label><select id=\"sel\" name=\"sel\">";
    for (int k = 0; k < SEL_COUNT; k++) {
        int m = SEL_ORDER[k];
        o += "<option value=\"" + std::string(selName(m)) + "\"" + (m == E.plan.sel ? " selected" : "") + ">" + esc(SELECT_LONG[m]) + "</option>";
    }
    o += "</select></div><div class=\"field\" style=\"margin:0\"><label for=\"budget\">Installation budget (lakh rupees)</label><input id=\"budget\" name=\"budget\" type=\"number\" min=\"0\" "
         "max=\"10000000\" step=\"1\" value=\"" + std::to_string(E.plan.budget) + "\" style=\"width:150px\"></div><button class=\"primary\">Apply</button></form>";
    o += "<p class=\"note\" style=\"margin-top:8px\">Dataset budget " + esc(rupees(C.default_budget)) + "; all " + std::to_string(sc.candidates()) + " candidates together cost " +
         esc(rupees(total)) + ".</p><div class=\"row\"><a class=\"btn" + std::string(E.plan.budget == C.default_budget ? " dis" : "") + "\" href=\"" + c.url("/placement", { { "budget", "" } }) +
         "\">Reset to the dataset budget</a><a class=\"btn" + (E.plan.edits.empty() ? " dis" : "") + "\" href=\"" + c.url("/placement", { { "edit", "" } }) + "\">Clear what-if edits (" +
         std::to_string(E.plan.edits.count()) + ")</a><a class=\"btn\" href=\"" + c.url("/sites.csv") + "\">Download the selected sites (CSV)</a></div></section>";
    Run *R = E.run();
    if (!R) return o + errorBox("The plan could not be computed.", E.status);
    // ---- summary
    const SelV &sel = R->run.sel;
    double budget = E.plan.budget, left = budget - sel.cost;
    o += "<div class=\"metrics six\">";
    metric(o, "Installation budget", rupees(budget), "hard limit, never exceeded");
    metric(o, "Selected stations", fint(sel.n) + " of " + fint(sc.candidates()), "candidate sites to build", "p");
    metric(o, "Total installation cost", rupees(sel.cost), fint(sel.cost / std::max(1e-9, budget) * 100) + "% of the budget");
    metric(o, "Remaining budget", rupees(left), sel.over_budget ? "locked sites exceed the budget" : "not spent", sel.over_budget ? "e" : "");
    metric(o, "Total benefit", fint(sel.value), "sum of the selected sites' scores");
    metric(o, "Search", fms(sel.ms), std::string(sel.optimal ? "proven optimal" : "heuristic") + (sel.search_nodes ? ", " + fint((double)sel.search_nodes) + " B&B nodes" : std::string()));
    o += "</div>";
    // ---- explanation
    std::string t = "**" + std::string(SELECT_LONG[E.plan.sel]) + "** chose **" + fint(sel.n) + " of " + fint(sc.candidates()) + "** candidate sites. Together they cost **" +
                    rupees(sel.cost) + "** of the **" + rupees(E.plan.budget) + "** budget and add up to a benefit score of **" + fint(sel.value) + "**. ";
    if (sel.optimal)
        t += "This is the 0/1 knapsack optimum: no other set of candidates that fits the budget has a higher total score (the exact methods prove it; "
             "the engine also checks that the cost never exceeds the budget). ";
    else {
        ev::PlacementResult best = E.placementWith(SEL_DP);
        t += "Greedy takes sites in order of benefit per rupee and stops adding a site when it no longer fits, so it can miss the best combination. Here it reaches **" +
             fint(sel.value) + "** where the exact optimum is **" + fint((double)best.benefit) + "** (" + f1(sel.value / std::max(1.0, (double)best.benefit) * 100) + "%). ";
    }
    if (!sel.note.empty()) t += sel.note + ". ";
    if (!E.plan.edits.empty())
        t += "Your edits are respected: " + fint((double)E.plan.edits.lock.size()) + " site(s) forced in (paid first), " + fint((double)E.plan.edits.ban.size()) + " ruled out, " +
             fint((double)E.plan.edits.ports.size()) + " port change(s). ";
    bool estimated = false;
    for (int i = sc.nexist; i < sc.ns; i++) estimated = estimated || sc.stations[(size_t)i].estimated;
    t += estimated ? "Benefit scores were estimated from the trips: each trip adds 1 to every junction on its shortest route (4 if the vehicle cannot drive that route on its starting charge)."
                   : "Benefit scores come from the dataset's candidates file.";
    std::string limit = "**Limits of this model.** The knapsack adds up benefit scores, so two neighbouring sites that serve the same drivers are both credited; it does not "
                        "solve the full geographic facility-location (coverage) problem. The trip planner re-checks every placement: with this one, **" +
                        f1(R->run.kpi.feasible_pct) + "%** of trips can be completed.";
    o += "<section class=\"panel\">" + section("Why these sites") + "<p>" + md(t) + "</p><p class=\"ink2\" style=\"margin:0\">" + md(limit) + "</p></section>";

    // ---- tabs
    std::string tab = c.get("tab", "candidates");
    o += "<section><nav class=\"tabs\">";
    static const char *TABS[][2] = { { "candidates", "Candidate sites" }, { "existing", "Existing stations" }, { "compare", "Compare methods" } };
    for (auto &tb : TABS) o += "<a href=\"" + c.url("/placement", { { "tab", tb[0] } }) + "\"" + (tab == tb[0] ? " class=\"on\"" : "") + ">" + tb[1] + "</a>";
    o += "</nav><div class=\"panel\" style=\"border-top:0;border-top-left-radius:0;border-top-right-radius:0\">";
    if (tab == "existing") {
        if (sc.nexist == 0) o += "<p class=\"ink2\">This dataset has no existing charging stations.</p>";
        else {
            std::vector<int> sessions((size_t)sc.ns, 0), needed((size_t)sc.ns, 0);
            for (const LoadV &L : R->ses.st) { sessions[(size_t)L.station] = L.sessions; needed[(size_t)L.station] = L.chargers_needed; }
            o += "<p class=\"hint\">Existing stations are always part of the network and are not part of the budget. Offline or maintenance stations cannot be used for charging.</p>"
                 "<div class=\"scroll\"><table><thead><tr><th>Id</th><th>Name</th><th>Junction</th><th>Status</th><th>Connector</th><th>Power</th><th>Ports</th><th>Planned sessions</th>"
                 "<th>Chargers needed</th></tr></thead><tbody>";
            for (int i = 0; i < sc.nexist; i++) {
                const SiteV &s = sc.stations[(size_t)i];
                o += "<tr><td>" + std::to_string(s.id) + "</td><td>" + esc(s.name) + "</td><td>" + esc(sc.nodes[(size_t)s.node].name) + "</td><td class=\"" + (s.available ? "" : "bad") + "\">" +
                     esc(s.status) + "</td><td>" + esc(s.connector.empty() ? std::string("not recorded") : s.connector) + "</td><td>" + fint(s.power_kw) + " kW</td><td>" +
                     portsCell(c, i, "/placement") + "</td><td>" + std::to_string(sessions[(size_t)i]) + "</td><td class=\"" + (needed[(size_t)i] > E.effectivePorts(i) ? "bad" : "") + "\">" +
                     std::to_string(needed[(size_t)i]) + "</td></tr>";
            }
            o += "</tbody></table></div>";
        }
    } else if (tab == "compare") {
        Compare Cm;
        {
            std::lock_guard<std::mutex> g(g_cmp_mu);
            auto it = g_cmp.find(E.plan.city);
            if (it != g_cmp.end()) Cm = it->second;
        }
        if (!Cm.done) head += "<meta http-equiv=\"refresh\" content=\"3\">";
        o += "<p class=\"hint\">Every placement method with every routing method on the same data at the dataset's budget (" +
             (Cm.done ? std::string("complete") : "computing, " + std::to_string(Cm.rows.size()) + " runs so far; this page refreshes every 3 seconds") + ").</p>";
        o += "<div class=\"scroll\"><table><thead><tr><th>Placement</th><th>Routing</th><th>Stations</th><th>Cost</th><th>Benefit</th><th>Optimal</th><th>Placement time</th>"
             "<th>Trips completed</th><th>Routing time</th></tr></thead><tbody>";
        for (const CompareRow &r : Cm.rows)
            o += "<tr><td>" + esc(SELECT_LABEL[r.sel]) + "</td><td>" + esc(ROUTE_LABEL[r.route]) + "</td><td>" + std::to_string(r.stations) + "</td><td>" + esc(rupees(r.cost)) + "</td><td>" +
                 fint(r.value) + "</td><td class=\"" + (r.optimal ? "good" : "warnc") + "\">" + (r.optimal ? "yes" : "no") + "</td><td>" + esc(fms(r.select_ms)) + "</td><td>" + fint(r.feasible) +
                 " (" + f1(r.feasible_pct) + "%)</td><td>" + esc(fms(r.route_ms)) + "</td></tr>";
        if (Cm.rows.empty()) o += "<tr><td colspan=\"9\" class=\"muted\">The first runs are being computed.</td></tr>";
        o += "</tbody></table></div>";
        if (Cm.done) o += "<p class=\"ink2\" style=\"margin-top:10px\">" + md(strategiesFinding(Cm)) + "</p>";
    } else {
        std::string filter = c.get("filter", "all"), sort = c.get("sort", "sel"), dir = c.get("dir", sort == "sel" || sort == "benefit" || sort == "ratio" ? "desc" : "asc");
        o += "<div class=\"row\" style=\"margin-bottom:10px\"><span class=\"seg\">";
        static const char *F[][2] = { { "all", "All" }, { "selected", "Selected" }, { "not", "Not selected" } };
        for (auto &f : F) o += "<a href=\"" + c.url("/placement", { { "filter", f[0] } }) + "\"" + (filter == f[0] ? " class=\"on\"" : "") + ">" + f[1] + "</a>";
        o += "</span><span class=\"note\">Click a column header to sort (merge sort). Decision: Auto = the algorithm decides.</span></div>";
        std::vector<int> rows;
        for (int i = sc.nexist; i < sc.ns; i++) {
            bool built = R->built[(size_t)i] != 0;
            if ((filter == "selected" && !built) || (filter == "not" && built)) continue;
            rows.push_back(i);
        }
        bool desc = dir == "desc";
        auto key = [&](int i) -> double {
            const SiteV &s = sc.stations[(size_t)i];
            if (sort == "sel") return R->built[(size_t)i];
            if (sort == "id") return s.id;
            if (sort == "cost") return s.cost;
            if (sort == "benefit") return (double)s.value;
            if (sort == "ratio") return s.value / std::max(1e-9, s.cost);
            return 0;
        };
        if (sort == "name")
            ev::merge_sort(rows, [&](int a, int b) { return desc ? sc.stations[(size_t)b].name < sc.stations[(size_t)a].name : sc.stations[(size_t)a].name < sc.stations[(size_t)b].name; });
        else ev::merge_sort(rows, [&](int a, int b) { return desc ? key(b) < key(a) : key(a) < key(b); });
        auto th = [&](const char *k, const char *label) {
            bool on = sort == k;
            std::string nd = on ? (desc ? "asc" : "desc") : (std::strcmp(k, "id") == 0 || std::strcmp(k, "name") == 0 || std::strcmp(k, "cost") == 0 ? "asc" : "desc");
            return "<th><a href=\"" + c.url("/placement", { { "sort", k }, { "dir", nd } }) + "\">" + label + (on ? (desc ? " \xE2\x96\xBE" : " \xE2\x96\xB4") : "") + "</a></th>";
        };
        o += "<div class=\"scroll\"><table><thead><tr>" + th("sel", "Selected") + th("id", "Id") + th("name", "Name") + "<th>Junction</th>" + th("cost", "Cost") + th("benefit", "Benefit") +
             th("ratio", "Benefit per lakh") + "<th>Ports</th><th>Decision</th></tr></thead><tbody>";
        for (int i : rows) {
            const SiteV &s = sc.stations[(size_t)i];
            bool built = R->built[(size_t)i] != 0;
            o += "<tr><td>" + std::string(built ? "<span class=\"good\"><span class=\"diamond\"></span>built</span>" : "<span class=\"muted\">\xE2\x80\x93</span>") + "</td><td>" + std::to_string(s.id) +
                 "</td><td>" + esc(s.name) + "</td><td>" + esc(sc.nodes[(size_t)s.node].name) + "</td><td>" + esc(rupees(s.cost)) + "</td><td>" + fint((double)s.value) +
                 (s.estimated ? " (est.)" : "") + "</td><td>" + f2(s.value / std::max(1e-9, s.cost)) + "</td><td>" + portsCell(c, i, "/placement") + "</td><td>" + decisionCell(c, i, "/placement") +
                 "</td></tr>";
        }
        if (rows.empty()) o += "<tr><td colspan=\"9\" class=\"muted\">No candidate sites match this filter.</td></tr>";
        o += "</tbody></table></div>";
    }
    o += "</div></section>";
    return o;
}

/* ======================================================================== 4. Road Network */
static Layers layersFrom(const Ctx &c) {
    Layers L;
    if (c.get("lay") == "1") {   // the layer form was submitted: unchecked boxes are absent
        L.terrain = c.has("ter"); L.junctions = c.has("jun"); L.names = c.has("nam"); L.existing = c.has("exi");
        L.candidates = c.has("can"); L.built = c.has("new"); L.route = c.has("rte"); L.traffic = c.has("trf");
    }
    return L;
}

static std::string mapControls(Ctx &c, const MapT &m) {
    auto tstr = [](double v) { return num(v, 2); };
    double step = (m.W / 3.0) / m.s;
    auto a = [&](const char *label, const char *aria, const Params &p) { return "<a class=\"btn sm\" aria-label=\"" + std::string(aria) + "\" href=\"" + c.url("/network", p) + "\">" + label + "</a>"; };
    std::string o = "<div class=\"mapctl\">";
    o += a("+", "Zoom in", { { "z", num(std::min(32.0, m.z * 2), 0) }, { "cx", tstr(m.cx) }, { "cy", tstr(m.cy) } });
    o += a("\xE2\x88\x92", "Zoom out", { { "z", m.z / 2 <= 1 ? "" : num(m.z / 2, 0) }, { "cx", m.z / 2 <= 1 ? "" : tstr(m.cx) }, { "cy", m.z / 2 <= 1 ? "" : tstr(m.cy) } });
    if (m.z > 1) {
        o += a("\xE2\x86\x90", "Pan west", { { "cx", tstr(m.cx - step) } });
        o += a("\xE2\x86\x91", "Pan north", { { "cy", tstr(m.cy + step) } });
        o += a("\xE2\x86\x93", "Pan south", { { "cy", tstr(m.cy - step) } });
        o += a("\xE2\x86\x92", "Pan east", { { "cx", tstr(m.cx + step) } });
    }
    o += a("Fit", "Show the whole network", { { "z", "" }, { "cx", "" }, { "cy", "" } });
    return o + "</div>";
}

static std::string stationPanel(Ctx &c, Run &R, int i) {
    Engine &E = c.E;
    const Scene &sc = E.city().sc;
    const SiteV &s = sc.stations[(size_t)i];
    bool built = R.built[(size_t)i] != 0;
    std::string o = "<div style=\"border:1px solid var(--line);border-radius:4px;padding:12px;background:var(--bg)\"><div class=\"row\" style=\"justify-content:space-between\"><b>" + esc(s.name) +
                    "</b><a class=\"btn sm\" href=\"" + c.url("/network", { { "station", "" } }) + "\">Close</a></div><p style=\"margin:6px 0\">";
    if (s.existing) o += s.available ? "<span class=\"tag info\">EXISTING</span>" : "<span class=\"tag bad\">EXISTING, " + esc(s.status) + "</span>";
    else o += built ? "<span class=\"tag ok\">NEW STATION (SELECTED)</span>" : "<span class=\"tag\">CANDIDATE (NOT SELECTED)</span>";
    o += "</p><table class=\"kv\"><tbody>";
    auto row = [&](const char *k, const std::string &v) { o += "<tr><th>" + std::string(k) + "</th><td>" + esc(v) + "</td></tr>"; };
    row("Station id", std::to_string(s.id));
    row("Junction", sc.nodes[(size_t)s.node].name + " (node " + std::to_string(sc.nodes[(size_t)s.node].id) + ")");
    row("Ports", std::to_string(E.effectivePorts(i)) + " at " + fint(s.power_kw) + " kW");
    row("Connector", s.connector.empty() ? "not recorded" : s.connector);
    if (!s.existing) {
        row("Installation cost", rupees(s.cost));
        row("Benefit score", fint((double)s.value) + (s.estimated ? " (estimated)" : ""));
    }
    for (const LoadV &L : R.ses.st)
        if (L.station == i) row("Planned sessions", fint(L.sessions) + ", needs " + fint(L.chargers_needed) + " chargers");
    o += "</tbody></table><div class=\"row\" style=\"margin-top:8px\"><span class=\"ink2\">Ports</span>" + portsCell(c, i, "/network") + "</div>";
    if (!s.existing) o += "<div class=\"row\" style=\"margin-top:8px\"><span class=\"ink2\">Build</span>" + decisionCell(c, i, "/network") + "</div>";
    else o += "<p class=\"note\" style=\"margin-top:8px\">Existing stations are always part of the network; only their ports can change.</p>";
    return o + "</div>";
}

static std::string pageNetwork(Ctx &c, std::string &head) {
    Engine &E = c.E;
    City &C = E.city();
    const Scene &sc = C.sc;
    Run *R = E.run();
    bool day = c.get("mode") == "day";
    std::string o = header("Road Network", "");
    o += "<div class=\"row\">" + dataTag(sc, false) + "<span class=\"small muted\">" +
         esc(fint(sc.nn) + " junctions, " + fint(sc.ne) + " roads, " + fint(C.maxx - C.minx) + " x " + fint(C.maxy - C.miny) +
             " km. Positions: latitude/longitude projected onto a flat plane (equirectangular, around the mean latitude)" +
             (sc.cfg.source == "synthetic" ? "; this is a generated graph, not a real place." : ".")) +
         "</span></div>";
    if (!R) return o + errorBox("The plan could not be computed.", E.status);
    double z = std::atof(c.get("z", "1").c_str());
    double cx = c.has("cx") ? std::atof(c.get("cx").c_str()) : NAN, cy = c.has("cy") ? std::atof(c.get("cy").c_str()) : NAN;
    MapT m = mapFor(C, 900, z, cx, cy);
    Layers L = layersFrom(c);
    int policy = c.get("policy") == "divert" ? 1 : 0;

    // day simulation state
    SimView *sv = nullptr;
    LiveState live;
    double T = 510, speed = 10;
    bool play = false;
    if (day) {
        sv = E.sim(policy);
        if (!sv) return o + errorBox("The day simulation could not run.", E.status);
        std::string ts = c.get("time");
        if (!ts.empty()) {
            size_t colon = ts.find(':');
            T = colon == std::string::npos ? std::atof(ts.c_str()) : std::atoi(ts.c_str()) * 60.0 + std::atof(ts.c_str() + colon + 1);
        }
        if (!(T >= 0)) T = 0;
        if (T > sv->end_min) T = std::fmod(T, sv->end_min);
        speed = c.has("speed") ? std::atof(c.get("speed").c_str()) : 10;
        if (!(speed > 0) || speed > 240) speed = 10;
        play = c.get("play") == "1";
        if (play) {
            double nt = T + speed;
            if (nt > sv->end_min) nt = 0;
            head += "<meta http-equiv=\"refresh\" content=\"1;url=" + c.url("/network", { { "time", num(nt, 2) } }) + "\">";
        }
        sv->at(T, false, live);
    }

    // selected dataset trip (plan mode)
    int trip = -1;
    if (c.has("trip")) for (int i = 0; i < sc.nt; i++) if (sc.trips[(size_t)i].id == std::atoi(c.get("trip").c_str())) trip = i;
    const TripDetail *D = !day && trip >= 0 && L.route ? &E.trip(trip) : nullptr;
    int station = c.has("station") ? stationIndex(sc, std::atol(c.get("station").c_str())) : -1;

    // ---- the map
    std::string svg;
    appf(svg, "<svg class=\"map\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"%s\">", m.W, m.H, day ? "Vehicles and station occupancy" : "Road network with stations and candidate sites");
    if (day) {
        Layers B;
        B.junctions = false;
        drawBase(svg, C, m, R, B);
        for (int k = 0; k < sv->sim.nst; k++) {
            int s = sv->sim.st[(size_t)k].station;
            const NodeV &n = sc.nodes[(size_t)sc.stations[(size_t)s].node];
            double x = m.X(n.x), y = m.Y(n.y), r = 7.5;
            int ports = R->ports[(size_t)s], busy = live.charging[(size_t)k], wait = live.waiting[(size_t)k];
            appf(svg, "<g><title>Station %d: %d of %d ports busy, %d waiting</title><circle class=\"stn\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", sc.stations[(size_t)s].id, busy, ports, wait, x, y, r);
            if (busy > 0) {
                double f = std::min(1.0, (double)busy / std::max(1, ports)), rr = r + 3.2;
                if (f >= 0.999) appf(svg, "<circle class=\"occ full\" cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\"/>", x, y, rr);
                else {
                    double ang = -3.14159265358979323846 / 2 + 2 * 3.14159265358979323846 * f;
                    appf(svg, "<path class=\"occ\" d=\"M%.1f %.1fA%.1f %.1f 0 %d 1 %.1f %.1f\"/>", x, y - rr, rr, rr, f > 0.5 ? 1 : 0, x + rr * std::cos(ang), y + rr * std::sin(ang));
                }
            }
            if (wait > 0)
                appf(svg, "<rect class=\"wt\" x=\"%.1f\" y=\"%.1f\" width=\"%d\" height=\"16\" rx=\"8\"/><text class=\"wtt\" x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\">%d</text>", x + r + 2,
                     y - r - 12, wait >= 10 ? 22 : 16, x + r + 2 + (wait >= 10 ? 11 : 8), y - r - 0.5, wait);
            svg += "</g>";
        }
        for (int v = 0; v < live.ncars; v++)
            appf(svg, "<circle class=\"car%s\" cx=\"%.1f\" cy=\"%.1f\" r=\"2.7\"/>", live.cars[(size_t)v * 3 + 2] < 0.3f ? " low" : "", m.X(live.cars[(size_t)v * 3]), m.Y(live.cars[(size_t)v * 3 + 1]));
    } else {
        drawBase(svg, C, m, R, L);
        if (D && D->valid) drawRoute(svg, sc, m, D->path, D->plan.feasible);
        drawStations(svg, c, m, R, L, station, true);
        if (D && D->valid) drawStopBadges(svg, sc, m, D->plan.stop);
    }
    drawScaleBar(svg, m);
    svg += "</svg>";

    o += "<div class=\"netgrid\"><div style=\"display:grid;gap:12px;min-width:0\"><div class=\"mapwrap\">" + mapControls(c, m) + svg + "</div>";
    if (!day) {
        o += "<p class=\"note\" style=\"margin:0\">Use + and \xE2\x88\x92 to zoom, the arrows to pan and Fit to see everything. Click a station for its details." +
             std::string(sc.nn > 600 ? " Large network: junction dots appear when you zoom in." : "") + "</p>";
    } else {
        // grid draw over the day; click an hour to jump there
        int W = 900, H = 170, l = 46, r = 10, t = 12, b = 26;
        double grid = sc.cfg.grid_kw, ymax = grid * 1.15;
        for (double v : sv->series_kw) ymax = std::max(ymax, v * 1.1);
        double hours = sv->end_min / 60;
        auto X = [&](double h) { return l + h / hours * (W - l - r); };
        auto Y = [&](double v) { return t + (1 - v / ymax) * (H - t - b); };
        o += "<section class=\"panel\">" + section("Charging power drawn from the grid", "Click an hour to jump there.");
        appf(o, "<svg class=\"chart\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"Charging power drawn from the grid over the day\">", W, H);
        for (int g = 0; g <= 3; g++)
            appf(o, "<line class=\"gl\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tk\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(ymax * g / 3), Y(ymax * g / 3),
                 l - 4, Y(ymax * g / 3) + 4, fint(ymax * g / 3).c_str());
        if (!sv->series_t.empty()) {
            appf(o, "<path class=\"area\" d=\"M%d %d", l, H - b);
            for (size_t i = 0; i < sv->series_t.size(); i++) appf(o, "L%.1f %.1f", X(sv->series_t[i]), Y(sv->series_kw[i]));
            appf(o, "L%.1f %dZ\"/><polyline class=\"kwl\" points=\"", X(sv->series_t.back()), H - b);
            for (size_t i = 0; i < sv->series_t.size(); i++) appf(o, "%.1f,%.1f ", X(sv->series_t[i]), Y(sv->series_kw[i]));
            o += "\"/>";
        }
        appf(o, "<line class=\"sup\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tkl\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">grid supply %s kW</text>", l, W - r, Y(grid), Y(grid),
             W - r, Y(grid) - 5, fint(grid).c_str());
        appf(o, "<line class=\"now\" x1=\"%.1f\" x2=\"%.1f\" y1=\"%d\" y2=\"%d\"/>", X(T / 60), X(T / 60), t, H - b);
        for (int h = 0; h < (int)hours; h++) {
            if (h % 3 == 0) appf(o, "<text class=\"tk\" x=\"%.1f\" y=\"%d\" text-anchor=\"middle\">%02d:00</text>", X(h), H - 8, h % 24);
            o += "<a href=\"" + c.url("/network", { { "time", std::to_string(h * 60) }, { "play", "" } }) + "\"><rect x=\"" + num(X(h), 1) + "\" y=\"" + std::to_string(t) + "\" width=\"" +
                 num(X(1) - X(0), 1) + "\" height=\"" + std::to_string(H - t - b) + "\" fill=\"transparent\"><title>" + fclock(h * 60.0) + "</title></rect></a>";
        }
        o += "</svg></section>";
    }
    o += "</div><aside class=\"panel\" style=\"display:grid;gap:12px\"><div class=\"seg full\"><a href=\"" + c.url("/network", { { "mode", "" }, { "play", "" } }) + "\"" + (day ? "" : " class=\"on\"") +
         ">Plan</a><a href=\"" + c.url("/network", { { "mode", "day" }, { "station", "" } }) + "\"" + (day ? " class=\"on\"" : "") + ">Day simulation</a></div>";
    if (!day) {
        // layers
        auto box = [&](const char *k, const char *label, bool on) {
            return "<label style=\"display:flex;gap:8px;align-items:center\"><input type=\"checkbox\" name=\"" + std::string(k) + "\" value=\"1\"" + (on ? " checked" : "") + ">" + label + "</label>";
        };
        o += "<form method=\"get\" action=\"/network\">" + c.hidden({ "lay", "ter", "jun", "nam", "exi", "can", "new", "rte", "trf" }) + "<h2>Layers</h2><div style=\"display:grid;gap:4px\">" +
             box("ter", "Terrain and contours", L.terrain) + box("jun", "Junctions", L.junctions) + box("nam", "Junction names (zoomed in)", L.names) +
             box("exi", "Existing stations", L.existing) + box("can", "Candidate sites", L.candidates) + box("new", "New stations", L.built) +
             box("rte", "Route of the chosen trip", L.route) + box("trf", "Traffic (planned trips per road)", L.traffic) +
             "</div><input type=\"hidden\" name=\"lay\" value=\"1\"><button style=\"margin-top:8px\">Apply layers</button></form>";
        if (station >= 0) o += stationPanel(c, *R, station);
        o += "<div><h2>Legend</h2>" + planLegend(L, L.route) + "</div>";
        // trip
        o += "<form method=\"get\" action=\"/network\">" + c.hidden({ "trip" }) + "<h2>Route</h2><label class=\"lbl\" for=\"ntrip\">Show a trip from the dataset</label><div class=\"dsrow\">"
             "<select id=\"ntrip\" name=\"trip\"><option value=\"\">None</option>";
        for (const Sample &s : R->samples) {
            const TripV &tr = sc.trips[(size_t)s.trip];
            o += "<option value=\"" + std::to_string(tr.id) + "\"" + (s.trip == trip ? " selected" : "") + ">Trip " + std::to_string(tr.id) + ", " + f1(s.km) + " km, " +
                 (s.kind == 2 ? std::to_string(s.stops) + " stops" : s.kind == 1 ? "1 stop" : s.kind == 0 ? "direct" : "not possible") + "</option>";
        }
        o += "</select><button>Show</button></div></form>";
        if (D && D->valid) {
            const TripV &tr = sc.trips[(size_t)D->trip];
            o += "<div><p class=\"small\"><b>Trip " + std::to_string(tr.id) + ": " + esc(sc.nodes[(size_t)tr.src].name) + " to " + esc(sc.nodes[(size_t)tr.dst].name) + ".</b> " +
                 md(tripFacts(sc, *D)) + "</p><table class=\"small\"><tbody>";
            for (auto &st : tripItinerary(sc, *D)) o += "<tr><td class=\"mono muted\">" + esc(st.first) + "</td><td>" + md(st.second) + "</td></tr>";
            o += "</tbody></table></div>";
        }
    } else {
        int dayn = (int)(T / 1440) + 1;
        o += "<div><h2>Simulated day</h2><p class=\"hint\">Every planned trip is replayed with the real port counts; a driver who finds every port busy queues.</p><div class=\"row\">"
             "<span class=\"clock\">" + fclock(std::fmod(T, 1440)) + "</span>" + (dayn > 1 ? "<span class=\"muted\">day " + std::to_string(dayn) + "</span>" : std::string()) + "</div></div>";
        o += "<div class=\"row\"><a class=\"btn primary\" href=\"" + c.url("/network", { { "play", play ? "" : "1" }, { "time", num(T >= sv->end_min ? 0 : T, 2) } }) + "\">" +
             (play ? "Pause" : "Play") + "</a><a class=\"btn\" href=\"" + c.url("/network", { { "time", num(std::max(0.0, T - 60), 2) }, { "play", "" } }) + "\">\xE2\x88\x92" "1 h</a><a class=\"btn\" href=\"" +
             c.url("/network", { { "time", num(std::min(sv->end_min, T + 60), 2) }, { "play", "" } }) + "\">+1 h</a></div>";
        o += "<form method=\"get\" action=\"/network\" class=\"dsrow\">" + c.hidden({ "time", "play" }) + "<input name=\"time\" aria-label=\"Go to a time\" placeholder=\"time, e.g. 18:30\"><button>Go</button></form>";
        static const double SP[] = { 1, 10, 60 };
        static const char *SPL[] = { "1 min/s", "10 min/s", "1 h/s" };
        o += "<div><span class=\"lbl\">Speed</span><div class=\"seg full\">";
        for (int k = 0; k < 3; k++) o += "<a" + std::string(std::fabs(speed - SP[k]) < 1e-6 ? " class=\"on\"" : "") + " href=\"" + c.url("/network", { { "speed", num(SP[k], 0) }, { "time", num(T, 2) } }) + "\">" + SPL[k] + "</a>";
        o += "</div></div><div><span class=\"lbl\">When every port at a station is busy</span><div class=\"seg full\"><a" + std::string(policy == 0 ? " class=\"on\"" : "") + " href=\"" +
             c.url("/network", { { "policy", "" }, { "time", num(T, 2) } }) + "\">Drivers wait</a><a" + (policy == 1 ? " class=\"on\"" : "") + " href=\"" +
             c.url("/network", { { "policy", "divert" }, { "time", num(T, 2) } }) + "\">Switch after 20 min</a></div></div><table class=\"kv\"><tbody>";
        auto row = [&](const char *k, const std::string &v, bool hot) { o += "<tr><th>" + std::string(k) + "</th><td" + (hot ? " class=\"warnc\"" : "") + ">" + esc(v) + "</td></tr>"; };
        row("On the road", fint(live.road), false);
        row("Charging", fint(live.charging_total), false);
        row("Waiting for a port", fint(live.waiting_total), live.waiting_total > 0);
        row("Arrived", fint(live.arrived) + " of " + fint(sv->sim.nv), false);
        row("Grid draw now", fint(live.kw) + " kW", live.kw > sc.cfg.grid_kw);
        row("Energy delivered", f1(live.kwh / 1000) + " MWh", false);
        o += "</tbody></table><div class=\"legend\">" + legendItem("<circle class=\"car\" cx=\"9\" cy=\"9\" r=\"4\"/>", "Vehicle") +
             legendItem("<circle class=\"car low\" cx=\"9\" cy=\"9\" r=\"4\"/>", "Vehicle, battery under 30%") +
             legendItem("<circle class=\"stn\" cx=\"9\" cy=\"9\" r=\"5\"/><path class=\"occ\" d=\"M9 1.5A7.5 7.5 0 0 1 16.5 9\"/>", "Station, arc = ports in use") +
             legendItem("<rect class=\"wt\" x=\"1\" y=\"3\" width=\"16\" height=\"12\" rx=\"6\"/>", "Drivers waiting") + "</div><p class=\"small ink2\">" +
             md(liveSummary(E, *sv, E.sim(1 - policy), policy)) + "</p>";
    }
    o += "</aside></div>";
    return o;
}

/* ======================================================================== 5. Algorithms */
static std::string catalogTable(bool core) {
    std::string o = "<div class=\"scroll\"><table><thead><tr><th>Algorithm</th><th>Purpose</th><th>Input \xE2\x86\x92 output</th><th>Time</th><th>Space</th><th>Role in EV33</th></tr></thead><tbody>";
    for (const AlgoInfo &a : algorithmCatalog()) {
        if (a.core != core) continue;
        o += "<tr><td><b>" + esc(a.name) + "</b><br><span class=\"small muted\">" + esc(a.family) + "</span></td><td>" + esc(a.purpose) + "</td><td>" + esc(a.input) +
             "<br><span class=\"muted\">\xE2\x86\x92</span> " + esc(a.output) + "</td><td class=\"mono\">" + esc(a.time) + "</td><td class=\"mono\">" + esc(a.space) + "</td><td>" + esc(a.role) +
             "<br><span class=\"small muted\">" + esc(a.where) + "<br>Checked by: " + esc(a.check) + "</span></td></tr>";
    }
    return o + "</tbody></table></div>";
}

static std::string pageAlgorithms(Ctx &c) {
    Engine &E = c.E;
    std::string o = header("Algorithms", "Every algorithm below is written by hand in the engine (include/ev, src/) and tested against an independent method. The standard library's "
                                         "sort, priority queue and searches are not used for this work. n = items, V = junctions, E = roads.");
    o += "<details open class=\"panel\"><summary>Core EV workflow: used every time a plan is computed</summary><p class=\"hint\">Load data \xE2\x86\x92 benefit of each site \xE2\x86\x92 "
         "placement \xE2\x86\x92 route every trip \xE2\x86\x92 capacity \xE2\x86\x92 charging sessions \xE2\x86\x92 day simulation</p>" + catalogTable(true) + "</details>";
    o += "<details open class=\"panel\"><summary>Analysis report and teaching: run on the city's data, not needed to plan</summary><p class=\"hint\">Uses marked illustrative show the "
         "algorithm on scenario data; the placement and routing workflow does not depend on them.</p>" + catalogTable(false) + "</details>";
    Analysis &A = E.analysis(E.plan.city);
    if (!A.done) E.computeAnalysis(E.plan.city);
    o += "<details open class=\"panel\"><summary>Cross-checks on this city</summary><p class=\"hint\">Every algorithm was run on \"" + esc(E.city().label) +
         "\" and each answer compared with a second, independent method.</p><div class=\"scroll\"><table><thead><tr><th>Decision</th><th>Algorithm</th><th>Result on this city</th>"
         "<th>Check</th></tr></thead><tbody>";
    for (const AlgoRow &r : algorithmRows(E.city(), E.run(), A)) {
        if (r.family) { o += "<tr class=\"fam\"><th colspan=\"4\">" + esc(r.decision) + "</th></tr>"; continue; }
        const char *cls = r.check == CHECK_OK ? "ok" : r.check == CHECK_BAD ? "bad" : "";
        o += "<tr><td>" + esc(r.decision) + "</td><td class=\"ink2\">" + esc(r.algorithm) + "</td><td>" + md(r.result) + "</td><td><span class=\"tag " + cls + "\">" + esc(checkLabel(r.check)) +
             "</span></td></tr>";
    }
    o += "</tbody></table></div><details style=\"margin-top:10px\"><summary class=\"small\">Show the engine's own report</summary><pre class=\"mono small\" style=\"white-space:pre-wrap;"
         "background:var(--panel2);padding:12px;border-radius:4px\">" + esc(A.text) + "</pre></details></details>";
    return o;
}

/* ======================================================================== 6. Benchmarks */
static std::string pageBench(Ctx &c) {
    Engine &E = c.E;
    const auto &rows = E.bench();
    std::string o = header("Benchmarks", "Run times measured by the separate benchmark program (ev33_bench) on generated datasets with seed 12345. Each row is the median of repeated runs "
                                         "and was checked for correctness outside the timed part.");
    int failed = 0;
    for (const BenchRow &r : rows) failed += !r.pass;
    o += "<section class=\"panel\"><p class=\"small ink2\" style=\"margin:0\">File: benchmarks/results.csv";
    if (!rows.empty()) o += " &nbsp;|&nbsp; " + fint((double)rows.size()) + " measurements, " + std::to_string(failed) + " failed checks &nbsp;|&nbsp; " + esc(rows[0].compiler) + ", " + esc(rows[0].build);
    o += "</p><p class=\"note\" style=\"margin:6px 0 0\">The website shows the committed measurements. To measure on your own computer, run ev33_bench from a terminal, or use "
         "Run quick benchmark in the desktop app.</p></section>";
    if (rows.empty()) {
        o += "<p class=\"warnc\">No benchmark results available. Run the benchmark executable to generate measurements.</p>";
        if (!E.bench_error.empty()) o += "<p class=\"small muted\">" + esc(E.bench_error) + "</p>";
        return o;
    }
    std::vector<Family> fams = FAMILIES;
    fams.push_back({ "Trip planning", { "trip_planning" } });
    int fam = std::max(0, std::min((int)fams.size() - 1, std::atoi(c.get("family", "0").c_str())));
    o += "<div class=\"row\">";
    for (int f = 0; f < (int)fams.size(); f++)
        o += "<a class=\"btn" + std::string(f == fam ? " primary" : "") + "\" href=\"" + c.url("/benchmarks", { { "family", std::to_string(f) } }) + "\">" + fams[(size_t)f].label + "</a>";
    o += "</div>";
    const Family &F = fams[(size_t)fam];
    bool trip = F.fam.size() == 1 && F.fam[0] == "trip_planning";
    std::vector<const BenchRow *> mine;
    for (auto &r : rows) if (std::find(F.fam.begin(), F.fam.end(), r.family) != F.fam.end()) mine.push_back(&r);
    static const char *PAL[] = { "var(--primary)", "var(--secondary)", "var(--warn)", "var(--error)", "#8A6FB0", "var(--ink2)", "#3E9A9A" };
    o += "<div class=\"cols\"><section class=\"panel\">";
    if (trip) {
        o += section("Trip planning on each city", "Median time to plan every trip of the city with each planner.");
        std::vector<std::string> labels;
        std::vector<double> vals;
        for (const BenchRow *r : mine) { labels.push_back(algName(r->alg) + " / " + r->dataset); vals.push_back(r->ms); }
        o += "<div class=\"scroll\"><table><thead><tr><th>Planner / dataset</th><th>Median</th><th></th></tr></thead><tbody>";
        double mx = 1e-9;
        for (double v : vals) mx = std::max(mx, v);
        for (size_t i = 0; i < vals.size(); i++)
            o += "<tr><td>" + esc(labels[i]) + "</td><td class=\"mono\">" + esc(fms(vals[i])) + "</td><td style=\"width:40%\"><div style=\"height:10px;background:var(--secondary);width:" +
                 num(std::max(1.0, vals[i] / mx * 100), 1) + "%\"></div></td></tr>";
        o += "</tbody></table></div>";
    } else {
        o += section("Run time against input size (log-log)", "Each line is one algorithm on the family's typical input; the table lists every measurement.");
        std::map<std::string, std::vector<std::pair<double, double>>> series;
        double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
        for (const BenchRow *r : mine) {
            if (benchSkip(*r) || r->ms <= 0 || r->n <= 0) continue;
            series[r->alg].push_back({ r->n, r->ms });
            xmin = std::min(xmin, r->n); xmax = std::max(xmax, r->n); ymin = std::min(ymin, r->ms); ymax = std::max(ymax, r->ms);
        }
        if (!series.empty()) {
            double lx0 = std::floor(std::log10(xmin)), lx1 = std::ceil(std::log10(xmax)), ly0 = std::floor(std::log10(ymin)), ly1 = std::ceil(std::log10(ymax));
            if (lx1 <= lx0) lx1 = lx0 + 1;
            if (ly1 <= ly0) ly1 = ly0 + 1;
            int W = 640, H = 400, l = 64, r = 26, t = 26, b = 42;
            auto X = [&](double v) { return l + (std::log10(v) - lx0) / (lx1 - lx0) * (W - l - r); };
            auto Y = [&](double v) { return t + (1 - (std::log10(v) - ly0) / (ly1 - ly0)) * (H - t - b); };
            appf(o, "<svg class=\"chart\" viewBox=\"0 0 %d %d\" role=\"img\" aria-label=\"Run time against input size, log-log\">", W, H);
            for (double e = lx0; e <= lx1; e++)
                appf(o, "<line class=\"gl\" x1=\"%.1f\" x2=\"%.1f\" y1=\"%d\" y2=\"%d\"/><text class=\"tk\" x=\"%.1f\" y=\"%d\" text-anchor=\"middle\">%s</text>", X(std::pow(10, e)), X(std::pow(10, e)), t,
                     H - b, X(std::pow(10, e)), H - b + 16, fint(std::pow(10, e)).c_str());
            for (double e = ly0; e <= ly1; e++) {
                char lab[32];
                std::snprintf(lab, sizeof lab, "%g", std::pow(10, e));
                appf(o, "<line class=\"gl\" x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\"/><text class=\"tk\" x=\"%d\" y=\"%.1f\" text-anchor=\"end\">%s</text>", l, W - r, Y(std::pow(10, e)),
                     Y(std::pow(10, e)), l - 6, Y(std::pow(10, e)) + 4, lab);
            }
            appf(o, "<text class=\"tk\" x=\"%d\" y=\"%d\" text-anchor=\"middle\">input size n</text><text class=\"tk\" x=\"4\" y=\"14\">median time (ms)</text>", (l + W - r) / 2, H - 4);
            int k = 0;
            std::string keys;
            for (auto &kv : series) {
                auto pts = kv.second;
                ev::merge_sort(pts, [](const std::pair<double, double> &a, const std::pair<double, double> &b) { return a.first < b.first; });
                const char *colr = PAL[k % 7];
                appf(o, "<polyline class=\"ln\" style=\"stroke:%s\" points=\"", colr);
                for (auto &p : pts) appf(o, "%.1f,%.1f ", X(p.first), Y(p.second));
                o += "\"/>";
                for (auto &p : pts)
                    appf(o, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"3.2\" style=\"fill:%s\"><title>%s: n = %s, %s</title></circle>", X(p.first), Y(p.second), colr, esc(algName(kv.first)).c_str(),
                         fint(p.first).c_str(), fms(p.second).c_str());
                keys += "<span><i style=\"background:" + std::string(colr) + "\"></i>" + esc(algName(kv.first)) + "</span>";
                k++;
            }
            o += "</svg><div class=\"keys\">" + keys + "</div>";
        } else o += "<p class=\"muted\">No measurements in this family.</p>";
    }
    o += "</section><section class=\"panel\">" + section("Worst cases and trade-offs");
    for (auto &nt : adversarialNotes(rows)) o += "<h3>" + esc(nt.first) + "</h3><p class=\"small ink2\">" + esc(nt.second) + "</p>";
    o += "</section></div><section class=\"panel\">" + section("Measurements") +
         "<div class=\"scroll\"><table><thead><tr><th>Algorithm</th><th>Dataset</th><th>Variant</th><th>Input size</th><th>Vertices</th><th>Edges</th><th>Reps</th><th>Median</th>"
         "<th>Min</th><th>Max</th><th>Peak memory</th><th>Correct</th></tr></thead><tbody>";
    for (const BenchRow *r : mine)
        o += "<tr><td>" + esc(algName(r->alg)) + "</td><td>" + esc(r->dataset) + "</td><td>" + esc(r->variant) + "</td><td>" + fint(r->n) + "</td><td>" +
             (r->vertices > 0 ? fint(r->vertices) : std::string("\xE2\x80\x93")) + "</td><td>" + (r->edges > 0 ? fint(r->edges) : std::string("\xE2\x80\x93")) + "</td><td>" +
             std::to_string(r->reps) + "</td><td class=\"mono\">" + esc(fms(r->ms)) + "</td><td class=\"mono\">" + esc(fms(r->min_ms)) + "</td><td class=\"mono\">" + esc(fms(r->max_ms)) +
             "</td><td>" + esc(fbytes((double)r->peak)) + "</td><td class=\"" + (r->pass ? "good" : "bad") + "\">" + (r->pass ? "pass" : "FAIL") + "</td></tr>";
    o += "</tbody></table></div></section>";
    return o;
}

/* ======================================================================== 7. Data Management */
struct DatasetFolder {
    std::string key, label, dir, note;
    bool example = false;
};
static std::vector<DatasetFolder> g_folders;   // bundled datasets and examples of rejected data (fixed at start-up)

static void spit(const fs::path &p, const std::string &text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

// The bundled folders, plus copies of the hand-made sample with one deliberate mistake each, so a
// visitor can see exactly how the loader rejects bad data (nothing on the server can be uploaded).
static void prepareFolders(const std::string &data_dir) {
    std::error_code ec;
    fs::path root = fs::path(data_dir).parent_path();
    fs::path sample = root / "sample";
    if (fs::exists(sample / "scenario.cfg", ec)) g_folders.push_back({ "sample", "data/sample", sample.string(), "the hand-made 10-node sample (answers worked out by hand)", false });
    for (const auto &e : fs::directory_iterator(data_dir, ec)) {
        if (!e.is_directory(ec) || !fs::exists(e.path() / "scenario.cfg", ec)) continue;
        std::string n = e.path().filename().string();
        g_folders.push_back({ n, "data/scenarios/" + n, e.path().string(), "generated city", false });
    }
    ev::merge_sort(g_folders, [](const DatasetFolder &a, const DatasetFolder &b) { return a.label < b.label; });
    if (!fs::exists(sample / "scenario.cfg", ec)) return;
    struct Case { const char *key, *label, *file, *from, *to; };
    const Case cases[] = {
        { "negative-distance", "A road with a negative distance", "roads.csv", "1,1,2,20,,", "1,1,2,-20,," },
        { "text-in-number", "Text where a number belongs", "roads.csv", "1,1,2,20,,", "1,1,2,twenty,," },
        { "duplicate-node", "Two nodes with the same id", "nodes.csv", "2,0.000000,0.179856", "1,0.000000,0.179856" },
        { "unknown-node", "A road to a node that does not exist", "roads.csv", "2,2,3,20,,", "2,2,77,20,," },
        { "negative-cost", "A negative installation cost", "candidates.csv", "501,Junction A site,2,4000000", "501,Junction A site,2,-4000000" },
        { "zero-battery", "A vehicle with a zero battery", "vehicles.csv", "SV1,Sample hatchback,20,12", "SV1,Sample hatchback,0,12" },
        { "missing-column", "A required column is missing", "roads.csv", "id,from,to,distance_km", "id,from,to,length" },
        { "missing-file", "roads.csv is missing", "", "", "" },
    };
    fs::path base = fs::temp_directory_path(ec) / "ev33_examples";
    for (const Case &k : cases) {
        fs::path dir = base / k.key;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        for (const auto &e : fs::directory_iterator(sample, ec))
            if (e.is_regular_file(ec) && e.path().extension() != ".md") fs::copy_file(e.path(), dir / e.path().filename(), fs::copy_options::overwrite_existing, ec);
        if (*k.file) {
            std::ifstream in(dir / k.file, std::ios::binary);
            std::stringstream s;
            s << in.rdbuf();
            std::string text = s.str();
            size_t at = text.find(k.from);
            if (at == std::string::npos) continue;
            text.replace(at, std::strlen(k.from), k.to);
            spit(dir / k.file, text);
        } else fs::remove(dir / "roads.csv", ec);
        g_folders.push_back({ std::string("example-") + k.key, k.label, dir.string(), "a copy of data/sample with this one mistake", true });
    }
}

static std::string pageData(Ctx &c) {
    Engine &E = c.E;
    City &C = E.city();
    const Scene &sc = C.sc;
    std::string o = header("Data Management", "Datasets are folders of CSV files (nodes, roads, stations, candidates, vehicles, trips). Loading validates every record; a bad file is "
                                              "reported with its line and is never repaired silently.");
    o += "<div class=\"cols\"><section class=\"panel\">" + section("Loaded dataset") + "<p style=\"margin:0 0 4px\">" + esc(C.label) + "</p><p>" + dataTag(sc, false) + "</p><p class=\"small muted\">Folder: data/scenarios/" +
         esc(C.name) + (sc.cfg.source_note.empty() ? std::string() : "<br>Source: " + esc(sc.cfg.source_note)) + "</p>";
    o += "<table><thead><tr><th>File</th><th>Holds</th><th>Records</th></tr></thead><tbody>";
    auto row = [&](const char *f, const char *h, const std::string &n) { o += "<tr><td class=\"mono\">" + std::string(f) + "</td><td class=\"ink2\">" + h + "</td><td>" + n + "</td></tr>"; };
    row("nodes.csv", "road nodes (junctions)", fint(sc.nn));
    row("roads.csv", "road edges", fint(sc.ne));
    row("stations.csv", "existing charging stations", fint(sc.nexist));
    row("candidates.csv", "candidate locations", fint(sc.candidates()));
    row("vehicles.csv", "electric vehicles", fint(sc.nveh));
    row("trips.csv", "trips", fint(sc.nt));
    row("sessions.csv", "booked charging sessions", fint((double)sc.sessions));
    row("scenario.cfg", "budget and model settings", "1");
    o += "</tbody></table></section>";

    // ---- check a folder
    std::string key = c.get("check");
    const DatasetFolder *chosen = nullptr;
    for (const DatasetFolder &f : g_folders) if (f.key == key) chosen = &f;
    o += "<section class=\"panel\">" + section("Check a dataset", "Choose a folder: the loader validates every file and either accepts it or names the first problem as file:line: reason. "
                                                                   "Nothing is uploaded to this server; to check your own files, use the desktop app or ev33 validate --data DIR.");
    auto list = [&](bool example) {
        std::string l = "<div style=\"display:grid;gap:2px\">";
        for (const DatasetFolder &f : g_folders) {
            if (f.example != example) continue;
            l += "<a class=\"btn" + std::string(chosen == &f ? " primary" : "") + "\" style=\"text-align:left\" href=\"" + c.url("/data", { { "check", f.key } }) + "\">" + esc(f.label) +
                 " <span class=\"small" + (chosen == &f ? "" : " muted") + "\">" + esc(f.note) + "</span></a>";
        }
        return l + "</div>";
    };
    o += "<span class=\"lbl\">Dataset folders</span>" + list(false) + "<span class=\"lbl\" style=\"margin-top:10px\">Examples of rejected data</span>" + list(true);
    if (chosen) {
        DatasetCheck d = checkDataset(chosen->dir);
        o += "<div style=\"margin-top:12px\">";
        if (!d.ok) {
            std::string err = d.error;
            for (std::string pre : { chosen->dir + "/", chosen->dir }) {
                size_t at;
                while ((at = err.find(pre)) != std::string::npos) err.erase(at, pre.size());
            }
            o += "<div class=\"err\"><div class=\"t\">The dataset was rejected. Nothing was loaded; the current dataset is unchanged.</div><p class=\"mono\" style=\"margin:6px 0\">" + esc(err) +
                 "</p><p class=\"small\" style=\"margin:0\">The message names the file and line (file:line: reason). Fix that record and check again.</p></div>";
        } else {
            o += "<div class=\"panel\" style=\"background:var(--bg)\"><p class=\"good\" style=\"margin:0 0 6px\">Valid: every file and every cross-reference passed.</p><p>";
            if (d.source == "synthetic") o += "<span class=\"tag warn\">SYNTHETIC DATASET</span>";
            else if (d.source == "real") o += "<span class=\"tag info\">REAL-WORLD DATA (as declared)</span>";
            else o += "<span class=\"tag warn\">MIXED DATA</span>";
            o += "</p>" + (d.source_note.empty() ? std::string() : "<p class=\"small ink2\">Source: " + esc(d.source_note) + "</p>") + "<table class=\"kv\"><tbody>";
            auto kv = [&](const char *k, const std::string &v) { o += "<tr><th>" + std::string(k) + "</th><td>" + esc(v) + "</td></tr>"; };
            kv("Name", d.name);
            kv("Road nodes / roads", fint(d.nodes) + " / " + fint(d.roads) + (d.components > 1 ? "  (" + fint(d.components) + " separate parts)" : ""));
            kv("Existing stations", fint(d.stations) + (d.offline ? " (" + fint(d.offline) + " offline or in maintenance)" : ""));
            kv("Candidate sites", fint(d.candidates));
            kv("Vehicles / trips", fint(d.vehicles) + " / " + fint(d.trips));
            kv("Booked sessions", fint(d.sessions));
            kv("Budget", rupees(d.budget_lakh));
            o += "</tbody></table></div>";
        }
        o += "</div>";
    }
    o += "</section></div>";

    // ---- rules
    static const char *const RULES[][2] = {
        { "Missing files", "scenario.cfg, nodes.csv and roads.csv are required; an empty file is an error" },
        { "Missing columns", "every required column must be in the header; unknown or duplicate columns are errors" },
        { "Malformed records", "wrong number of fields, empty required values, text where a number belongs" },
        { "Duplicate identifiers", "node, road, station and candidate ids must be unique (stations and candidates share one id space)" },
        { "Negative or impossible distances", "distance must be positive, finite and at most 100,000 km; no road may join a node to itself" },
        { "Invalid installation costs", "amounts in rupees with at most 2 decimals; cost > 0; budget >= 0" },
        { "Invalid battery specifications", "capacity > 0, consumption > 0, 0 <= reserve < capacity, charge <= capacity" },
        { "Missing road-node references", "roads, stations, candidates and trips must name existing nodes; trips must name existing vehicles" },
        { "Coordinates", "latitude in [-90, 90], longitude in [-180, 180], both or neither" },
    };
    o += "<section class=\"panel\">" + section("What is checked when a dataset loads") + "<div class=\"scroll\"><table><thead><tr><th>Check</th><th>Rule</th></tr></thead><tbody>";
    for (auto &r : RULES) o += "<tr><td>" + std::string(r[0]) + "</td><td>" + esc(r[1]) + "</td></tr>";
    o += "</tbody></table></div><p class=\"note\" style=\"margin-top:8px\">File formats, the synthetic-data labels and how to prepare real data (for example from OpenStreetMap) are described in "
         "docs/data.md. A dataset declares its origin with data_source = synthetic | real | mixed in scenario.cfg; the app shows that label wherever its numbers appear.</p></section>";

    // ---- how the records are stored
    Run *R = E.run();
    if (R) {
        Analysis &A = E.analysis(E.plan.city);
        if (!A.done) E.computeAnalysis(E.plan.city);
        int t0 = R->samples.empty() ? -1 : R->samples[0].trip;
        const TripDetail *D = t0 >= 0 ? &E.trip(t0) : nullptr;
        o += "<details class=\"panel\"><summary>How the records are stored: typed C++ records and the structures built from them</summary><div class=\"scroll\"><table><thead><tr><th>Represents</th>"
             "<th>C++ type</th><th>Holds</th><th>In this city</th><th>Example</th><th>Defined in</th></tr></thead><tbody>";
        for (auto &x : modelRecords(E, *R, D))
            o += "<tr><td>" + esc(x.what) + "</td><td class=\"mono\">" + esc(x.type) + "</td><td>" + esc(x.holds) + "</td><td>" + esc(x.count) + "</td><td class=\"mono small\">" + esc(x.example) +
                 "</td><td class=\"mono\">" + esc(x.file) + "</td></tr>";
        o += "</tbody></table></div><h3 style=\"margin:14px 0 6px\">Structures built from them</h3><div class=\"scroll\"><table><thead><tr><th>Structure</th><th>Built from</th><th>Size here</th>"
             "<th>What it does</th><th>Code</th></tr></thead><tbody>";
        for (auto &x : modelStructures(E, *R, A))
            o += "<tr><td>" + esc(x.name) + "</td><td>" + esc(x.from) + "</td><td>" + esc(x.size) + "</td><td>" + esc(x.what) + "</td><td class=\"mono\">" + esc(x.code) + "</td></tr>";
        o += "</tbody></table></div></details>";
    }
    return o;
}

/* ======================================================================== 8. Settings */
static std::string pageSettings(Ctx &c) {
    Engine &E = c.E;
    std::string theme = c.get("theme"), policy = c.get("policy");
    int dm = std::max(0, std::min(PLANNER_COUNT - 1, std::atoi(c.get("dm", "0").c_str())));
    std::string o = header("Settings", "Settings are kept in the page address, so they follow you from view to view and can be bookmarked.");
    o += "<div class=\"cols\"><section class=\"panel\">" + section("Appearance") + "<span class=\"lbl\">Theme</span><div class=\"seg\">";
    static const char *TH[][2] = { { "", "Auto (follow the system)" }, { "light", "Light" }, { "dark", "Charcoal" } };
    for (auto &t : TH) o += "<a href=\"" + c.url("/settings", { { "theme", t[0] } }) + "\"" + (theme == t[0] ? " class=\"on\"" : "") + ">" + t[1] + "</a>";
    o += "</div><p class=\"note\" style=\"margin-top:8px\">Text size follows your browser's zoom (Ctrl + and Ctrl \xE2\x88\x92).</p></section>";
    o += "<section class=\"panel\">" + section("Defaults") + "<span class=\"lbl\">Trip Planner: routing algorithm</span><div class=\"seg\" style=\"margin-bottom:10px\">";
    static const char *SHORT[PLANNER_COUNT] = { "Exact", "Legs: Dijkstra", "Legs: Floyd\u2013Warshall", "Legs: Bellman\u2013Ford" };
    for (int m = 0; m < PLANNER_COUNT; m++) o += "<a href=\"" + c.url("/settings", { { "dm", m ? std::to_string(m) : "" } }) + "\"" + (m == dm ? " class=\"on\"" : "") + ">" + SHORT[m] + "</a>";
    o += "</div><span class=\"lbl\">Day simulation: when every port is busy</span><div class=\"seg\"><a href=\"" + c.url("/settings", { { "policy", "" } }) + "\"" + (policy != "divert" ? " class=\"on\"" : "") +
         ">Drivers wait</a><a href=\"" + c.url("/settings", { { "policy", "divert" } }) + "\"" + (policy == "divert" ? " class=\"on\"" : "") + ">Switch after 20 min</a></div></section></div>";
    o += "<section class=\"panel\">" + section("Website and desktop app") +
         "<p>This website and the desktop app <b>EV33 Charge Atlas</b> (ev33_gui) show the same eight views and call the same C++17 engine, so they give the same numbers for the same plan. "
         "They are separate programs and separate deployment targets: the website runs as a server on Render; the desktop app is a native Windows or Linux program built with CMake "
         "and run on your own computer. The desktop app does not replace this website.</p><p class=\"ink2\">What only the desktop app can do: zoom and pan the map with the mouse, "
         "check any folder on your computer, load it as a new city, and run the benchmark program. Source code, build instructions and the release ZIP: "
         "<a href=\"https://github.com/Astikpatel093/EV-project\">github.com/Astikpatel093/EV-project</a>.</p></section>";
    std::string compiler =
#ifdef __VERSION__
        std::string(__VERSION__);
#else
        std::string("unknown compiler");
#endif
    o += "<section class=\"panel\">" + section("About") + "<table class=\"kv\"><tbody><tr><th>Website server</th><td>ev33_web, C++17 (" + esc(compiler) +
         ")</td></tr><tr><th>Libraries used by the website</th><td>the C++ standard library only (HTTP, HTML, SVG and CSS are generated in C++; no JavaScript)</td></tr>"
         "<tr><th>Libraries used by the desktop app</th><td>Dear ImGui 1.91.5, GLFW 3.4, OpenGL 3</td></tr><tr><th>Fonts</th><td>IBM Plex Sans and Mono (SIL Open Font License)</td></tr>"
         "<tr><th>Datasets</th><td>" + std::to_string(E.cities.size()) + " generated cities in data/scenarios (all synthetic)</td></tr><tr><th>Benchmark results</th><td>benchmarks/results.csv</td></tr>"
         "</tbody></table></section>";
    return o;
}

/* ======================================================================== HTTP */
struct Response {
    int status = 200;
    std::string type = "text/html; charset=utf-8", body, location;
    bool cache = false;
};

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
    // addresses of the earlier version of the site
    static const char *OLD[][3] = { { "/live", "/network", "mode=day" }, { "/strategies", "/placement", "tab=compare" }, { "/capacity", "/", "" }, { "/model", "/data", "" } };
    for (auto &m : OLD)
        if (path == m[0]) {
            res.status = 301;
            res.location = std::string(m[1]) + (qs.empty() && !*m[2] ? "" : "?") + qs + (*m[2] ? (qs.empty() ? "" : "&") + std::string(m[2]) : "");
            res.type = "text/plain";
            res.body = "moved";
            return res;
        }
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
    int view = -1;
    for (int i = 0; i < V_COUNT; i++) if (path == VIEWS[i].path) view = i;
    if (view < 0) {
        res.status = 404;
        res.body = frame(c, header("Page not found", "") + "<p><a href=\"" + c.url("/") + "\">Open the dashboard</a></p>");
        return res;
    }
    c.view = view;
    std::string head, body;
    switch (view) {
    case V_DASH: body = pageDashboard(c); break;
    case V_TRIP: body = pageTrip(c); break;
    case V_PLACE: body = pagePlacement(c, head); break;
    case V_NET: body = pageNetwork(c, head); break;
    case V_ALGO: body = pageAlgorithms(c); break;
    case V_BENCH: body = pageBench(c); break;
    case V_DATA: body = pageData(c); break;
    default: body = pageSettings(c); break;
    }
    res.body = frame(c, body, head);
    return res;
}

static const char *statusText(int s) {
    return s == 200 ? "OK" : s == 301 ? "Moved Permanently" : s == 404 ? "Not Found" : s == 400 ? "Bad Request" : s == 405 ? "Method Not Allowed" : "Error";
}

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
            double t0 = nowMs();
            try {
                r = handle(E, target);
            } catch (const std::exception &e) {
                r = Response();
                r.status = 500;
                r.type = "text/plain";
                r.body = std::string("internal error: ") + e.what();
            }
            std::fprintf(stderr, "%s %s -> %d (%.1f ms, %zu bytes)\n", method.c_str(), target.substr(0, 160).c_str(), r.status, nowMs() - t0, r.body.size());
        }
    }
    std::string hdr;
    appf(hdr, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n%s", r.status, statusText(r.status), r.type.c_str(),
         r.body.size(), r.cache ? "Cache-Control: public, max-age=86400\r\n" : "Cache-Control: no-store\r\n");
    if (!r.location.empty()) hdr += "Location: " + r.location + "\r\n";
    hdr += "\r\n";
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
    if (data.empty()) data = find("data/scenarios", "medium/scenario.cfg");
    if (results.empty()) results = find("benchmarks", "results.csv");
    g_assets = assets.empty() ? find("assets/fonts", "IBMPlexSans-Regular.ttf") : assets + "/fonts";

    Engine E;
    if (!E.init(data, results)) { std::fprintf(stderr, "cannot load cities from %s\n", data.c_str()); return 1; }
    for (int i = 0; i < (int)E.cities.size(); i++) E.load(i);
    prepareFolders(data);
    {
        // smallest cities first so their tables are ready at once
        std::vector<int> order;
        for (const char *nm : { "small", "medium", "shock", "adversarial", "large" })
            for (int i = 0; i < (int)E.cities.size(); i++) if (E.cities[(size_t)i].name == nm) order.push_back(i);
        std::thread(compareWorker, data, results, order).detach();
    }

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
