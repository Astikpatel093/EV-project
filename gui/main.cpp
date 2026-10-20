// main.cpp - EV33 Charge Atlas, the desktop app (C++17, Dear ImGui + ImPlot on GLFW/OpenGL).
//
//   ev33_gui [--data DIR] [--results DIR] [--assets DIR] [--page NAME] [--city NAME]
//            [--select S] [--route R] [--budget N] [--edit l<id>,b<id>,p<id>=n]
//            [--station ID] [--trip ID] [--traffic] [--time HH:MM] [--play] [--policy wait|divert]
//            [--dark|--light] [--size WxH] [--scale F] [--shot FILE.png [--frames N]]
//
// --shot renders N frames (default 30), saves the window as a PNG and exits: useful for reports.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "ui.h"

#ifndef EV33_SOURCE_DIR
#define EV33_SOURCE_DIR "."
#endif

#if defined(_WIN32) && !defined(_WIN64)
#define EVGL_API __stdcall
#else
#define EVGL_API
#endif
typedef void(EVGL_API *PFN_ReadPixels)(int, int, int, int, unsigned, unsigned, void *);
typedef void(EVGL_API *PFN_PixelStorei)(unsigned, int);
typedef void(EVGL_API *PFN_Viewport)(int, int, int, int);
typedef void(EVGL_API *PFN_ClearColor)(float, float, float, float);
typedef void(EVGL_API *PFN_Clear)(unsigned);

using namespace ev33;

static bool exists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static std::string dirOf(const char *argv0) {
    std::string s = argv0 ? argv0 : "";
    size_t k = s.find_last_of("/\\");
    return k == std::string::npos ? "." : s.substr(0, k);
}

// first folder in the list that contains `probe`
static std::string findDir(const std::vector<std::string> &cands, const std::string &probe) {
    for (auto &c : cands) if (exists(c + "/" + probe)) return c;
    return cands.empty() ? "." : cands.front();
}

static void usage() {
    std::printf(
        "EV33 Charge Atlas (desktop app)\n"
        "  --data DIR --results DIR --assets DIR   where the cities, benchmark results.csv and fonts are\n"
        "  --page planner|live|strategies|capacity|algorithms|model|benchmarks\n"
        "  --city medium|shock|large|adversarial|small   --select dp|bnb-lc|bnb-fifo|bnb-lifo|greedy\n"
        "  --route dijkstra|floyd|bellman   --budget N   --edit l<id>,b<id>,p<id>=n\n"
        "  --station ID   --trip ID   --traffic   --time HH:MM   --play   --policy wait|divert\n"
        "  --dark | --light   --size WxH   --scale F   --shot FILE.png [--frames N]\n");
}

static void glfwError(int code, const char *msg) { std::fprintf(stderr, "GLFW error %d: %s\n", code, msg); }

int main(int argc, char **argv) {
    std::string data, results, assets, page, city, select, route, edits, shot, policy, timeStr;
    int budget = -1, station = -1, trip = -1, frames = 30, W = 1440, H = 900;
    bool dark = false, traffic = false, play = false;
    float scaleOverride = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](std::string &dst) { if (i + 1 < argc) dst = argv[++i]; };
        std::string tmp;
        if (a == "--data") next(data);
        else if (a == "--results") next(results);
        else if (a == "--assets") next(assets);
        else if (a == "--page") next(page);
        else if (a == "--city") next(city);
        else if (a == "--select") next(select);
        else if (a == "--route") next(route);
        else if (a == "--edit") next(edits);
        else if (a == "--shot") next(shot);
        else if (a == "--policy") next(policy);
        else if (a == "--time") next(timeStr);
        else if (a == "--budget") { next(tmp); budget = std::atoi(tmp.c_str()); }
        else if (a == "--station") { next(tmp); station = std::atoi(tmp.c_str()); }
        else if (a == "--trip") { next(tmp); trip = std::atoi(tmp.c_str()); }
        else if (a == "--frames") { next(tmp); frames = std::max(2, std::atoi(tmp.c_str())); }
        else if (a == "--size") { next(tmp); std::sscanf(tmp.c_str(), "%dx%d", &W, &H); }
        else if (a == "--scale") { next(tmp); scaleOverride = (float)std::atof(tmp.c_str()); }
        else if (a == "--dark") dark = true;
        else if (a == "--light") dark = false;
        else if (a == "--traffic") traffic = true;
        else if (a == "--play") play = true;
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "Unknown option %s\n", a.c_str()); usage(); return 2; }
    }

    // locate data, results and fonts: the working directory, next to the program, or the source tree
    std::string exe = dirOf(argv[0]);
    std::vector<std::string> roots = { ".", "..", exe, exe + "/..", exe + "/../..", EV33_SOURCE_DIR };
    std::vector<std::string> dc, rc, ac;
    for (auto &r : roots) { dc.push_back(r + "/data/scenarios"); rc.push_back(r + "/benchmarks"); ac.push_back(r + "/assets/fonts"); }
    if (data.empty()) data = findDir(dc, "medium/scenario.cfg");
    if (results.empty()) results = findDir(rc, "results.csv");
    if (assets.empty()) assets = findDir(ac, "IBMPlexSans-Regular.ttf");
    else assets += "/fonts";

    App app;
    if (!app.eng.init(data, results)) {
        std::fprintf(stderr, "Could not load the cities from %s: %s\nRun from the project folder, or pass --data DIR.\n",
                     data.c_str(), app.eng.cities.empty() ? "" : app.eng.cities[0].error.c_str());
        return 1;
    }
    // command-line plan
    for (int c = 0; c < (int)app.eng.cities.size(); c++)
        if (app.eng.cities[(size_t)c].name == city) app.eng.selectCity(c);
    if (!select.empty()) { int m = selParse(select.c_str()); if (m >= 0) app.eng.plan.sel = m; }
    if (!route.empty()) { int m = routeParse(route.c_str()); if (m >= 0) app.eng.plan.route = m; }
    if (budget >= 0) app.eng.plan.budget = budget;
    const Scene &sc = app.eng.city().sc;
    auto stationIndex = [&](int id) { for (int k = 0; k < sc.ns; k++) if (sc.stations[k].id == id) return k; return -1; };
    if (!edits.empty()) {
        size_t p = 0;
        while (p < edits.size()) {
            size_t q = edits.find(',', p);
            std::string tok = edits.substr(p, q == std::string::npos ? std::string::npos : q - p);
            p = q == std::string::npos ? edits.size() : q + 1;
            if (tok.size() < 2) continue;
            int id = std::atoi(tok.c_str() + 1), k = stationIndex(id);
            if (k < 0) continue;
            if (tok[0] == 'l') app.eng.setBuild(k, 1);
            else if (tok[0] == 'b') app.eng.setBuild(k, 2);
            else if (tok[0] == 'p') { size_t e = tok.find('='); if (e != std::string::npos) app.eng.setPorts(k, std::atoi(tok.c_str() + e + 1)); }
        }
    }
    if (station >= 0) app.panel = stationIndex(station);
    if (trip >= 0) for (int k = 0; k < sc.nt; k++) if (sc.trips[k].id == trip) app.trip = k;
    app.traffic = traffic;
    app.policy = policy == "divert" ? 1 : 0;
    if (!timeStr.empty()) { int hh = 0, mm = 0; std::sscanf(timeStr.c_str(), "%d:%d", &hh, &mm); app.T = hh * 60 + mm; app.autoplayed = true; }
    if (play) app.playing = true, app.autoplayed = true;
    for (int k = 0; k < PAGE_COUNT; k++) {
        std::string n = PAGE_NAMES[k];
        for (auto &ch : n) ch = (char)std::tolower((unsigned char)ch);
        if (n == page || (page == "live" && k == PAGE_LIVE) || (page == "model" && k == PAGE_MODEL) || (page == "benchmarks" && k == PAGE_BENCH))
            app.force_page = k;
    }

    // window
    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) { std::fprintf(stderr, "Could not start GLFW (no display?).\n"); return 1; }
#ifdef __APPLE__
    const char *glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char *glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow *win = glfwCreateWindow(W, H, "EV33 Charge Atlas", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "Could not open a window with OpenGL 3.\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    auto glReadPixels_ = (PFN_ReadPixels)glfwGetProcAddress("glReadPixels");
    auto glPixelStorei_ = (PFN_PixelStorei)glfwGetProcAddress("glPixelStorei");
    auto glViewport_ = (PFN_Viewport)glfwGetProcAddress("glViewport");
    auto glClearColor_ = (PFN_ClearColor)glfwGetProcAddress("glClearColor");
    auto glClear_ = (PFN_Clear)glfwGetProcAddress("glClear");

    float xs = 1, ys = 1;
    glfwGetWindowContentScale(win, &xs, &ys);
    app.scale = scaleOverride > 0 ? scaleOverride : std::max(1.0f, xs);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);
    if (!loadFonts(app.fonts, assets, app.scale))
        std::fprintf(stderr, "Fonts not found in %s; using the built-in font.\n", assets.c_str());
    app.dark = dark;
    app.pal = makePalette(dark);
    applyTheme(app.pal, dark, app.scale);

    int frame = 0;
    bool wasDark = dark;
    while (!glfwWindowShouldClose(win)) {
        bool animating = (app.page == PAGE_LIVE && app.playing) || ImGui::GetTime() - app.trip_anim < 2.6 || !shot.empty() ||
                         ImGui::GetTime() < app.toast_until;
        if (animating) glfwPollEvents(); else glfwWaitEventsTimeout(0.5);

        // apply plan changes requested during the last frame
        if (app.pending_city >= 0) {
            app.eng.selectCity(app.pending_city);
            app.pending_city = -1;
            app.has_next = false;
            app.panel = app.trip = -1;
            app.plannerMap.reset();
            app.liveMap.reset();
        }
        if (app.has_next) { app.eng.plan = app.next; app.has_next = false; }
        if (app.dark != wasDark) { app.pal = makePalette(app.dark); applyTheme(app.pal, app.dark, app.scale); wasDark = app.dark; }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        float S = app.scale;
        const Palette &P = app.pal;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("EV33", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        // brand
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            float u = 30 * S / 32;
            ImVec2 pts[4] = { ImVec2(p.x + 4 * u, p.y + 22 * u), ImVec2(p.x + 10 * u, p.y + 12 * u), ImVec2(p.x + 16 * u, p.y + 10 * u), ImVec2(p.x + 28 * u, p.y + 4 * u) };
            dl->AddBezierCubic(pts[0], pts[1], pts[2], pts[3], col(P.ink), 2.2f * u);
            dl->AddCircleFilled(ImVec2(p.x + 16 * u, p.y + 10 * u), 4.2f * u, col(P.cobalt));
            ImGui::Dummy(ImVec2(32 * S, 30 * S));
            ImGui::SameLine();
            ImGui::PushFont(app.fonts.h2);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("EV33");
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(P.ink2, "Charge Atlas");
            ImGui::SameLine();
            ImGui::TextColored(P.ink3, "   C++17 engine, computed live");
            float right = ImGui::GetWindowContentRegionMax().x;
            float bw1 = ImGui::CalcTextSize("Save screenshot").x + 20 * S, bw2 = ImGui::CalcTextSize("Light theme").x + 20 * S;
            ImGui::SameLine(right - bw1 - bw2 - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button(app.dark ? "Light theme" : "Dark theme", ImVec2(bw2, 0))) app.dark = !app.dark;
            ImGui::SameLine();
            if (ImGui::Button("Save screenshot", ImVec2(bw1, 0))) {
                char buf[64];
                std::time_t t = std::time(nullptr);
                std::strftime(buf, sizeof buf, "ev33_screenshot_%Y%m%d_%H%M%S.png", std::localtime(&t));
                app.shot_path = buf;
            }
        }
        // pages
        if (ImGui::BeginTabBar("pages", ImGuiTabBarFlags_NoTooltip)) {
            for (int k = 0; k < PAGE_COUNT; k++) {
                ImGuiTabItemFlags fl = app.force_page == k ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(PAGE_NAMES[k], nullptr, fl)) { app.page = k; ImGui::EndTabItem(); }
            }
            ImGui::EndTabBar();
        }
        app.force_page = -1;

        float statusH = ImGui::GetTextLineHeightWithSpacing() + 6 * S;
        float sideW = 300 * S;
        ImGui::BeginChild("sidebar", ImVec2(sideW, -statusH), ImGuiChildFlags_None);
        drawSidebar(app);
        ImGui::EndChild();
        ImGui::SameLine(0, 18 * S);
        bool fixed = app.page == PAGE_PLANNER || app.page == PAGE_LIVE;
        ImGui::BeginChild("content", ImVec2(0, -statusH), ImGuiChildFlags_None, fixed ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
        switch (app.page) {
        case PAGE_PLANNER: drawPlanner(app); break;
        case PAGE_LIVE: drawLive(app); break;
        case PAGE_STRATEGIES: drawStrategies(app); break;
        case PAGE_CAPACITY: drawCapacity(app); break;
        case PAGE_ALGORITHMS: drawAlgorithms(app); break;
        case PAGE_MODEL: drawModel(app); break;
        default: drawBench(app); break;
        }
        ImGui::EndChild();
        // status line
        ImGui::PushFont(app.fonts.small);
        if (ImGui::GetTime() < app.toast_until) ImGui::TextColored(P.green, "%s", app.toast.c_str());
        else {
            std::string plan = "Plan: " + app.eng.city().label + ", " + app.eng.planLabel(app.eng.plan) + ".  Data: " + app.eng.data_dir;
            ImGui::TextColored(P.ink3, "%s", plan.c_str());
        }
        ImGui::PopFont();
        ImGui::End();

        ImGui::Render();
        int fw, fh;
        glfwGetFramebufferSize(win, &fw, &fh);
        glViewport_(0, 0, fw, fh);
        glClearColor_(P.paper.x, P.paper.y, P.paper.z, 1);
        glClear_(0x4000);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        frame++;
        bool autoShot = !shot.empty() && frame >= frames;
        if (autoShot) app.shot_path = shot;
        if (!app.shot_path.empty()) {
            std::vector<unsigned char> px((size_t)fw * (size_t)fh * 4);
            glPixelStorei_(0x0D05, 1);
            glReadPixels_(0, 0, fw, fh, 0x1908, 0x1401, px.data());
            bool ok = writePng(app.shot_path, fw, fh, px);
            app.notify(ok ? "Saved the window to " + app.shot_path : "Could not write " + app.shot_path, ImGui::GetTime());
            if (autoShot) { std::printf("%s %s\n", ok ? "saved" : "failed", app.shot_path.c_str()); glfwSetWindowShouldClose(win, 1); }
            app.shot_path.clear();
        }
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
