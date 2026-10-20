// ui.h - shared state, theme and helpers for the EV33 desktop app.
#pragma once

#include <string>
#include <vector>

#include "engine.h"
#include "fmt.h"
#include "imgui.h"

namespace ev33 {

/* ---------------------------------------------------------------- colours (same tokens as the old site) */
struct Palette {
    ImVec4 paper, paper2, paper3, ink, ink2, ink3, line, contour, cobalt, cobaltSoft, amber, amberSoft, red, green, greenSoft;
};
Palette makePalette(bool dark);
void applyTheme(const Palette &p, bool dark, float scale);
ImU32 col(const ImVec4 &c, float alpha = 1.0f);
ImVec4 mix(const ImVec4 &a, const ImVec4 &b, float t);

/* ---------------------------------------------------------------- fonts */
struct Fonts {
    ImFont *body = nullptr, *bold = nullptr, *semi = nullptr, *small = nullptr, *smallBold = nullptr;
    ImFont *h1 = nullptr, *h2 = nullptr, *big = nullptr, *mono = nullptr;
};
bool loadFonts(Fonts &f, const std::string &dir, float scale);

/* ---------------------------------------------------------------- text helpers */
// Wrapped paragraph; **bold** spans switch to the bold font.
void RichText(const std::string &s, const Fonts &f, ImU32 color = 0, float max_width = 0);
// Wrapping row of toggle buttons (one selected).
bool Chips(const char *id, const char *const *labels, int n, int *sel, const Palette &p);
// ImPlot tick formatter with thousands separators (1,000 / 10,000 / 0.1).
int TickFormat(double value, char *buf, int size, void *);
void Heading(const char *title, const Fonts &f, const char *sub = nullptr);
void Caption(const char *text, const Palette &p);
void Pill(const char *text, const ImVec4 &bg, const ImVec4 &fg);
// Row of joined buttons, one selected; returns true when the selection changed.
bool Segmented(const char *id, const char *const *labels, int n, int *sel, const Palette &p, float width = 0, const bool *disabled = nullptr);

/* ---------------------------------------------------------------- PNG export (stored deflate, no dependencies) */
bool writePng(const std::string &path, int w, int h, const std::vector<unsigned char> &rgba_bottom_up);

/* ---------------------------------------------------------------- app state */
struct MapView {
    float z = 1, tx = 0, ty = 0;
    int hover = -1;
    bool dragging = false;
    void reset() { z = 1; tx = ty = 0; }
};

enum Page { PAGE_PLANNER = 0, PAGE_LIVE, PAGE_STRATEGIES, PAGE_CAPACITY, PAGE_ALGORITHMS, PAGE_MODEL, PAGE_BENCH, PAGE_COUNT };
extern const char *const PAGE_NAMES[PAGE_COUNT];

struct App {
    Engine eng;
    Palette pal;
    Fonts fonts;
    bool dark = false;
    float scale = 1;
    int page = PAGE_PLANNER;
    int force_page = -1;            // set to switch tabs programmatically

    // plan changes wait for the next frame so pointers into the current run stay valid while drawing
    Plan next;
    bool has_next = false;
    int pending_city = -1;

    // planner
    MapView plannerMap;
    int panel = -1;                 // station whose panel is open
    bool traffic = false;
    int trip = -1;                  // scenario trip index shown in the inspector
    double trip_anim = 0;           // time the replay started

    // live day
    MapView liveMap;
    double T = 510;                 // simulated minutes since midnight
    double speed = 10;              // simulated minutes per real second
    bool playing = false;
    int policy = 0;                 // 0 wait in line, 1 divert
    bool autoplayed = false;

    // strategies, benchmarks
    int metric = 0;
    int bench_family = 0;

    // messages and screenshots
    std::string toast;
    double toast_until = 0;
    std::string shot_path;          // set to save a PNG after this frame
    void notify(const std::string &msg, double now) { toast = msg; toast_until = now + 4; }
};

// views.cpp
void drawSidebar(App &app);
void drawPlanner(App &app);
void drawLive(App &app);
void drawStrategies(App &app);
void drawCapacity(App &app);
void drawAlgorithms(App &app);
void drawModel(App &app);
void drawBench(App &app);

// map.cpp
enum MapMode { MAP_PLANNER, MAP_LIVE };
int drawCityMap(App &app, MapView &mv, const char *id, ImVec2 size, MapMode mode, const LiveState *live, const SimView *sim);

} // namespace ev33
