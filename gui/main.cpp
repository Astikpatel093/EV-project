// main.cpp - EV33 Charge Atlas, the native desktop application (C++17, Dear ImGui on GLFW + OpenGL 3).
//
//   ev33_gui [--data DIR] [--results DIR] [--assets DIR] [--load DIR]
//            [--view dashboard|trip|placement|network|algorithms|benchmarks|data|settings]
//            [--city NAME] [--select M] [--route R] [--budget LAKH]
//            [--trip ID] [--method exact|dijkstra|floyd|bellman]
//            [--day] [--time HH:MM] [--play] [--policy wait|divert]
//            [--dark|--light] [--size WxH] [--scale F] [--shot FILE.png [--frames N]]
//
// --shot renders N frames (default 30), saves the window as a PNG and exits (for reports).
// The window is one Dear ImGui window: a sidebar with the dataset and the navigation, and the
// workspace on the right where the selected view draws itself (view_*.cpp).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "ui.h"

#ifndef EV33_SOURCE_DIR
#define EV33_SOURCE_DIR "."
#endif

// The few OpenGL 1.x functions the app calls itself (everything else is in the ImGui backend).
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
typedef const unsigned char *(EVGL_API *PFN_GetString)(unsigned);

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

static std::string findDir(const std::vector<std::string> &cands, const std::string &probe) {
    for (auto &c : cands) if (exists(c + "/" + probe)) return c;
    return cands.empty() ? "." : cands.front();
}

static void usage() {
    std::printf(
        "EV33 Charge Atlas - native desktop app\n"
        "  --data DIR --results DIR --assets DIR   folders of the cities, benchmark results.csv and fonts\n"
        "  --load DIR                              also load this dataset folder and open it\n"
        "  --view dashboard|trip|placement|network|algorithms|benchmarks|data|settings\n"
        "  --city medium|shock|large|adversarial|small   --select dp|bnb-lc|bnb-fifo|bnb-lifo|greedy\n"
        "  --route dijkstra|floyd|bellman   --budget LAKH\n"
        "  --trip ID [--method exact|dijkstra|floyd|bellman]   plan this dataset trip in Trip Planner\n"
        "  --day [--time HH:MM] [--play] [--policy wait|divert]  Road Network in day-simulation mode\n"
        "  --dark | --light   --size WxH   --scale F   --shot FILE.png [--frames N]\n");
}

static void glfwError(int code, const char *msg) { std::fprintf(stderr, "GLFW error %d: %s\n", code, msg); }

// ---------------------------------------------------------------- sidebar
static void drawSidebar(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // brand: a charge-point glyph (two strokes and a dot), the name
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float u = S;
        dl->AddLine(ImVec2(p.x + 4 * u, p.y + 22 * u), ImVec2(p.x + 13 * u, p.y + 5 * u), col(P.primary), 2.4f * u);
        dl->AddLine(ImVec2(p.x + 13 * u, p.y + 5 * u), ImVec2(p.x + 22 * u, p.y + 22 * u), col(P.ink2), 2.4f * u);
        dl->AddCircleFilled(ImVec2(p.x + 13 * u, p.y + 15 * u), 3.2f * u, col(P.secondary));
        ImGui::Dummy(ImVec2(26 * S, 26 * S));
        ImGui::SameLine(0, 8 * S);
        ImGui::BeginGroup();
        ImGui::PushFont(app.fonts.h2);
        ImGui::TextUnformatted("EV33");
        ImGui::PopFont();
        ImGui::PushFont(app.fonts.small);
        ImGui::TextColored(P.ink2, "Charge Atlas");
        ImGui::PopFont();
        ImGui::EndGroup();
    }
    ImGui::Dummy(ImVec2(0, 8 * S));
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "Dataset");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##city", E.city().label.c_str())) {
        for (int i = 0; i < (int)E.cities.size(); i++) {
            City &c = E.cities[(size_t)i];
            std::string l = c.label + (c.loaded ? "  (" + fint(c.sc.nn) + " junctions, " + fint(c.sc.nt) + " trips)" : "");
            if (ImGui::Selectable(l.c_str(), i == E.plan.city) && i != E.plan.city) app.pending_city = i;
            if (ImGui::IsItemHovered() && !c.blurb.empty()) ImGui::SetTooltip("%s", c.blurb.c_str());
        }
        ImGui::EndCombo();
    }
    SyntheticTag(app, E.city().sc);
    ImGui::Dummy(ImVec2(0, 10 * S));

    // navigation
    float w = ImGui::GetContentRegionAvail().x;
    for (int v = 0; v < VIEW_COUNT; v++) {
        bool on = app.view == v;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight() + 4 * S;
        ImGui::PushID(v);
        if (ImGui::InvisibleButton("nav", ImVec2(w, h))) app.view = v;
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (on || hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(on ? P.primarySoft : P.panel2), 3 * S);
        if (on) dl->AddRectFilled(p, ImVec2(p.x + 3 * S, p.y + h), col(P.primary));
        ImFont *f = on ? app.fonts.semi : app.fonts.body;
        dl->AddText(f, f->FontSize, ImVec2(p.x + 12 * S, p.y + (h - f->FontSize) / 2), col(on ? P.ink : P.ink2), VIEW_NAMES[v]);
        char key[8];
        std::snprintf(key, sizeof key, "%d", v + 1);
        ImFont *m = app.fonts.mono;
        dl->AddText(m, m->FontSize, ImVec2(p.x + w - 16 * S, p.y + (h - m->FontSize) / 2), col(P.ink3), key);
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Ctrl + 1..8 switches views.");
    ImGui::PopFont();

    // plan summary at the bottom
    float bottom = 92 * S;
    if (ImGui::GetContentRegionAvail().y > bottom) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - bottom);
    ImGui::Separator();
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "Current plan");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(P.ink3, "%s", E.planLabel(E.plan).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    static const char *const T[] = { "Light", "Charcoal" };
    int th = app.dark ? 1 : 0;
    if (Segmented("theme", T, 2, &th, P)) app.dark = th == 1;
}

int main(int argc, char **argv) {
    std::string data, results, assets, view, city, select, route, shot, policy, timeStr, load, method;
    int budget = -1, trip = -1, frames = 30, W = 1440, H = 900;
    bool dark = false, play = false, day = false;
    float scaleOverride = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](std::string &dst) { if (i + 1 < argc) dst = argv[++i]; };
        std::string tmp;
        if (a == "--data") next(data);
        else if (a == "--results") next(results);
        else if (a == "--assets") next(assets);
        else if (a == "--load") next(load);
        else if (a == "--view" || a == "--page") next(view);
        else if (a == "--city") next(city);
        else if (a == "--select") next(select);
        else if (a == "--route") next(route);
        else if (a == "--method") next(method);
        else if (a == "--shot") next(shot);
        else if (a == "--policy") next(policy);
        else if (a == "--time") next(timeStr);
        else if (a == "--budget") { next(tmp); budget = std::atoi(tmp.c_str()); }
        else if (a == "--trip") { next(tmp); trip = std::atoi(tmp.c_str()); }
        else if (a == "--frames") { next(tmp); frames = std::max(2, std::atoi(tmp.c_str())); }
        else if (a == "--size") { next(tmp); std::sscanf(tmp.c_str(), "%dx%d", &W, &H); }
        else if (a == "--scale") { next(tmp); scaleOverride = (float)std::atof(tmp.c_str()); }
        else if (a == "--dark") dark = true;
        else if (a == "--light") dark = false;
        else if (a == "--day") day = true;
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
    app.assets_dir = assets;
    app.exe_dir = exe;
    if (!app.eng.init(data, results)) {
        std::fprintf(stderr, "Could not load the cities from %s: %s\nRun from the project folder, or pass --data DIR.\n",
                     data.c_str(), app.eng.cities.empty() ? "" : app.eng.cities[0].error.c_str());
        return 1;
    }
    // command-line state
    for (int c = 0; c < (int)app.eng.cities.size(); c++)
        if (app.eng.cities[(size_t)c].name == city) app.eng.selectCity(c);
    if (!load.empty()) {
        std::string err;
        int k = app.eng.addCity(load, load + " (loaded)", err);
        if (k >= 0) app.eng.selectCity(k);
        else std::fprintf(stderr, "Could not load %s: %s\n", load.c_str(), err.c_str());
    }
    if (!select.empty()) { int m = selParse(select); if (m >= 0) app.eng.plan.sel = m; }
    if (!route.empty()) { int m = routeParse(route); if (m >= 0) app.eng.plan.route = m; }
    if (budget >= 0) app.eng.plan.budget = budget;
    for (int v = 0; v < VIEW_COUNT; v++) if (view == VIEW_KEYS[v]) app.view = v;
    if (!method.empty()) {
        static const char *const M[] = { "exact", "dijkstra", "floyd", "bellman" };
        for (int m = 0; m < PLANNER_COUNT; m++) if (method == M[m]) app.default_method = m;
    }
    if (trip >= 0) {
        const Scene &sc = app.eng.city().sc;
        for (int k = 0; k < sc.nt; k++)
            if (sc.trips[(size_t)k].id == trip) {
                FillTripForm(app, k);
                app.form.method = app.default_method;
                TripQuery q;
                q.src = sc.trips[(size_t)k].src;
                q.dst = sc.trips[(size_t)k].dst;
                const VehicleV &v = sc.vehicles[(size_t)app.form.vehicle];
                q.vehicle.id = v.id; q.vehicle.model = v.model; q.vehicle.connector = v.connector; q.vehicle.max_charge_kw = v.max_charge_kw;
                q.vehicle.battery_kwh = v.battery_kwh; q.vehicle.consumption_kwh_per_km = v.consumption; q.vehicle.reserve_kwh = v.reserve_kwh;
                q.vehicle.charge_kwh = sc.trips[(size_t)k].start_kwh;
                q.start_kwh = sc.trips[(size_t)k].start_kwh;
                q.depart_min = sc.trips[(size_t)k].depart_min;
                q.method = app.form.method;
                app.answer = app.eng.planTrip(q);
            }
    }
    if (day) app.net_mode = NET_DAY;
    app.policy = policy == "divert" ? 1 : 0;
    if (!timeStr.empty()) { int hh = 0, mm = 0; std::sscanf(timeStr.c_str(), "%d:%d", &hh, &mm); app.T = hh * 60 + mm; }
    app.playing = play;

    // window: GLFW + OpenGL 3
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
    auto glGetString_ = (PFN_GetString)glfwGetProcAddress("glGetString");
    if (glGetString_) { const unsigned char *v = glGetString_(0x1F02); if (v) app.gl_version = (const char *)v; }
    app.glfw_version = glfwGetVersionString();

    float xs = 1, ys = 1;
    glfwGetWindowContentScale(win, &xs, &ys);
    float monitorScale = std::max(1.0f, xs);
    app.scale_user = scaleOverride;
    app.scale = scaleOverride > 0 ? scaleOverride : monitorScale;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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
        bool animating = (app.view == VIEW_NETWORK && app.net_mode == NET_DAY && app.playing) || app.job.running || !shot.empty() ||
                         ImGui::GetTime() < app.toast_until;
        if (animating) glfwPollEvents(); else glfwWaitEventsTimeout(0.25);

        // changes requested during the last frame
        if (app.pending_city >= 0) {
            app.eng.selectCity(app.pending_city);
            app.pending_city = -1;
            app.has_next = false;
            app.station_panel = -1;
            app.net.reset();
            app.tripNet.reset();
        }
        if (app.has_next) { app.eng.plan = app.next; app.has_next = false; }
        if (app.dark != wasDark) { app.pal = makePalette(app.dark); applyTheme(app.pal, app.dark, app.scale); wasDark = app.dark; }
        if (app.rebuild_fonts) {
            app.rebuild_fonts = false;
            app.scale = app.scale_user > 0 ? app.scale_user : monitorScale;
            loadFonts(app.fonts, assets, app.scale);
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
            applyTheme(app.pal, app.dark, app.scale);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        float S = app.scale;
        const Palette &P = app.pal;
        if (io.KeyCtrl)
            for (int v = 0; v < VIEW_COUNT; v++)
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + v), false)) app.view = v;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("EV33", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        float statusH = ImGui::GetTextLineHeight() + 10 * S;
        float sideW = 232 * S;
        // sidebar
        ImGui::PushStyleColor(ImGuiCol_ChildBg, P.panel);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * S, 14 * S));
        ImGui::BeginChild("sidebar", ImVec2(sideW, -statusH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        drawSidebar(app);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, b.y), col(P.line));
        }
        ImGui::SameLine(0, 0);
        // workspace
        bool fixed = app.view == VIEW_NETWORK || app.view == VIEW_TRIP;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * S, 16 * S));
        ImGui::BeginChild("workspace", ImVec2(0, -statusH), ImGuiChildFlags_AlwaysUseWindowPadding,
                          fixed ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
        ImGui::PopStyleVar();
        switch (app.view) {
        case VIEW_DASHBOARD: DrawDashboard(app); break;
        case VIEW_TRIP: DrawTripPlanner(app); break;
        case VIEW_PLACEMENT: DrawPlacement(app); break;
        case VIEW_NETWORK: DrawRoadNetwork(app); break;
        case VIEW_ALGORITHMS: DrawAlgorithms(app); break;
        case VIEW_BENCHMARKS: DrawBenchmarks(app); break;
        case VIEW_DATA: DrawDataManagement(app); break;
        default: DrawSettings(app); break;
        }
        ImGui::EndChild();
        // status bar
        {
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, ImVec2(a.x + io.DisplaySize.x, a.y + statusH), col(P.panel2));
            dl->AddLine(a, ImVec2(a.x + io.DisplaySize.x, a.y), col(P.line));
            ImGui::SetCursorScreenPos(ImVec2(a.x + 14 * S, a.y + 5 * S));
            ImGui::PushFont(app.fonts.small);
            if (ImGui::GetTime() < app.toast_until) ImGui::TextColored(app.toast_error ? P.error : P.primary, "%s", app.toast.c_str());
            else {
                const std::string &st = app.eng.status;
                bool err = !st.empty() && st[0] == '!';
                std::string line = st.empty() ? app.eng.city().label + ": " + app.eng.planLabel(app.eng.plan) : err ? st.substr(1) : st;
                ImGui::TextColored(err ? P.error : P.ink2, "%s", line.c_str());
            }
            ImGui::SameLine(io.DisplaySize.x - 210 * S);
            if (ImGui::SmallButton("Save screenshot")) {
                char buf[64];
                std::time_t t = std::time(nullptr);
                std::strftime(buf, sizeof buf, "ev33_screenshot_%Y%m%d_%H%M%S.png", std::localtime(&t));
                app.shot_path = buf;
            }
            ImGui::PopFont();
        }
        ImGui::End();

        ImGui::Render();
        int fw, fh;
        glfwGetFramebufferSize(win, &fw, &fh);
        glViewport_(0, 0, fw, fh);
        glClearColor_(P.bg.x, P.bg.y, P.bg.z, 1);
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
            app.notify(ok ? "Saved the window to " + app.shot_path : "Could not write " + app.shot_path, ImGui::GetTime(), !ok);
            if (autoShot) { std::printf("%s %s\n", ok ? "saved" : "failed", app.shot_path.c_str()); glfwSetWindowShouldClose(win, 1); }
            app.shot_path.clear();
        }
        glfwSwapBuffers(win);
    }

    if (app.job.worker.joinable()) app.job.worker.join();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
