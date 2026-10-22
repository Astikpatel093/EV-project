// ui.h - shared state, theme, widgets and view entry points of the EV33 Charge Atlas desktop app.
//
// The app is a thin layer over the engine: every number it shows comes from shared/engine.* (which
// calls the ev:: library). Views only draw and collect input; they never re-implement algorithms.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "catalog.h"
#include "engine.h"
#include "fmt.h"
#include "imgui.h"

namespace ev33 {

/* ---------------------------------------------------------------- colours */
// Warm light theme or deep charcoal theme; forest green is the primary accent, muted blue the
// secondary one. Every colour the views use comes from this table.
struct Palette {
    ImVec4 bg, panel, panel2, line, ink, ink2, ink3, contour;
    ImVec4 primary, primarySoft, secondary, secondarySoft, warn, warnSoft, error, errorSoft;
};
Palette makePalette(bool dark);
void applyTheme(const Palette &p, bool dark, float scale);
ImU32 col(const ImVec4 &c, float alpha = 1.0f);
ImVec4 mix(const ImVec4 &a, const ImVec4 &b, float t);

/* ---------------------------------------------------------------- fonts */
struct Fonts {
    ImFont *body = nullptr, *semi = nullptr, *bold = nullptr, *small = nullptr, *smallSemi = nullptr;
    ImFont *h1 = nullptr, *h2 = nullptr, *mono = nullptr, *monoBig = nullptr;
    bool fromFiles = false;
};
bool loadFonts(Fonts &f, const std::string &dir, float scale);

/* ---------------------------------------------------------------- widgets (ui.cpp) */
struct App;
// Wrapped paragraph; **bold** spans switch to the semibold font.
void RichText(const std::string &s, const Fonts &f, ImU32 color = 0);
void ViewHeader(App &app, const char *title, const char *purpose);
void SectionTitle(App &app, const char *title, const char *note = nullptr);
void Tag(const char *text, const ImVec4 &bg, const ImVec4 &fg);
void SyntheticTag(App &app, const Scene &sc);
// Joined row of buttons, one selected; returns true when the selection changed.
bool Segmented(const char *id, const char *const *labels, int n, int *sel, const Palette &p, float width = 0);
// A small metric panel: label, value (monospaced), note. Width 0 = fill.
void Metric(App &app, const char *id, const char *label, const std::string &value, const std::string &note, float width, float height,
            const ImVec4 *accent = nullptr);
// Begins a bordered panel (child window); returns its content width.
float BeginPanel(App &app, const char *id, ImVec2 size, bool scroll = false);
void EndPanel();
void HelpMarker(const char *text);

/* ---------------------------------------------------------------- charts (charts.cpp) */
struct Series {
    std::string name;
    ImVec4 color;
    std::vector<double> x, y;
};
struct ChartOptions {
    bool logX = false, logY = false;
    const char *xLabel = "", *yLabel = "";
    double yMinZero = true;                  // start the y axis at 0 (linear axes only)
    double hLine = -1;                       // horizontal reference line (e.g. grid supply), -1 none
    const char *hLineLabel = nullptr;
    double vLine = -1;                       // vertical marker (e.g. current time), -1 none
};
// Line chart drawn with ImDrawList (no plotting library). Returns the plot rectangle.
void LineChart(App &app, const char *id, ImVec2 size, const std::vector<Series> &series, const ChartOptions &o);
// Bar chart: one bar per category; `over` (optional, same length) is drawn stacked on top in `overColor`.
void BarChart(App &app, const char *id, ImVec2 size, const std::vector<std::string> &labels, const std::vector<double> &values,
              const std::vector<double> *over, const ImVec4 &color, const ImVec4 &overColor, const ChartOptions &o);
// Battery profile of a trip: kWh against km with reserve and target lines and the elevation shaded.
void BatteryChart(App &app, const char *id, ImVec2 size, const std::vector<std::array<double, 3>> &profile, double capacity, double reserve,
                  double target);

/* ---------------------------------------------------------------- PNG export (stored deflate, no dependencies) */
bool writePng(const std::string &path, int w, int h, const std::vector<unsigned char> &rgba_bottom_up);

/* ---------------------------------------------------------------- road network canvas (network.cpp) */
struct NetView {
    float z = 1, tx = 0, ty = 0;
    int hover = -1;
    bool dragging = false;
    void reset() { z = 1; tx = ty = 0; }
};
struct NetLayers {
    bool terrain = true, junctions = true, traffic = false, labels = false;
    bool existing = true, candidates = true, built = true, route = true;
};
enum NetMode { NET_PLAN, NET_DAY };
// Draws the network of the current city. `route` (node positions) and `stops` (station indices)
// are optional overlays. Returns the station index clicked this frame, or -1.
int DrawNetwork(App &app, NetView &v, const char *id, ImVec2 size, NetMode mode, const NetLayers &layers, const std::vector<int> *route,
                const std::vector<int> *stops, const LiveState *live, const SimView *sim);
void NetworkLegend(App &app, NetMode mode, const NetLayers &layers);

/* algorithm catalogue: shared/catalog.h */

/* ---------------------------------------------------------------- app state */
enum View { VIEW_DASHBOARD = 0, VIEW_TRIP, VIEW_PLACEMENT, VIEW_NETWORK, VIEW_ALGORITHMS, VIEW_BENCHMARKS, VIEW_DATA, VIEW_SETTINGS, VIEW_COUNT };
extern const char *const VIEW_NAMES[VIEW_COUNT];
extern const char *const VIEW_KEYS[VIEW_COUNT];   // command-line names

struct TripForm {
    int src_id = 0, dst_id = 0;              // junction ids as typed
    int vehicle = 0;                         // index into Scene::vehicles, or nveh = custom
    double battery = 40, charge = 20, consumption = 0.18, reserve = 4;
    int depart_h = 8, depart_m = 0;
    int method = PLANNER_EXACT;
    int city = -1;                           // city the form was filled for
    int from_trip = -1;                      // scenario trip used to fill the form
};

struct BenchJob {
    std::thread worker;
    std::atomic<bool> running{ false };
    std::atomic<int> exit_code{ 0 };
    std::string output_file, command;
    bool finished = false;
};

struct App {
    Engine eng;
    Palette pal;
    Fonts fonts;
    bool dark = false;
    float scale = 1, scale_user = 0;         // scale_user: 0 = follow the monitor
    bool rebuild_fonts = false;
    std::string assets_dir, exe_dir;
    std::string gl_version, glfw_version;    // reported in Settings
    int view = VIEW_DASHBOARD;

    // plan changes wait for the next frame so pointers into the current run stay valid while drawing
    Plan next;
    bool has_next = false;
    int pending_city = -1;

    // trip planner
    TripForm form;
    TripAnswer answer;
    NetView tripNet;

    // placement
    int placement_tab = 0;
    int cand_filter = 0;                     // 0 all, 1 selected, 2 not selected
    int station_panel = -1;

    // road network
    NetView net;
    NetLayers layers;
    int net_mode = NET_PLAN;
    double T = 510;                          // simulated minutes since midnight
    double speed = 10;                       // simulated minutes per real second
    bool playing = false;
    int policy = 0;                          // 0 wait in line, 1 divert

    // benchmarks
    int bench_family = 0;
    BenchJob job;

    // data management
    char data_path[512] = "";
    DatasetCheck check;
    bool has_check = false;

    // settings
    int default_method = PLANNER_EXACT;

    // messages and screenshots
    std::string toast;
    bool toast_error = false;
    double toast_until = 0;
    std::string shot_path;
    void notify(const std::string &msg, double now, bool error = false) { toast = msg; toast_error = error; toast_until = now + 5; }
    Plan &edit() {                           // the plan to change this frame
        if (!has_next) { next = eng.plan; has_next = true; }
        return next;
    }
};

// views (one file each)
void DrawDashboard(App &app);
void DrawTripPlanner(App &app);
void DrawPlacement(App &app);
void DrawRoadNetwork(App &app);
void DrawAlgorithms(App &app);
void DrawBenchmarks(App &app);
void DrawDataManagement(App &app);
void DrawSettings(App &app);
void FillTripForm(App &app, int trip_index);   // trip planner: copy a scenario trip into the form

} // namespace ev33
