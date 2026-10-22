// view_settings.cpp - appearance, defaults, folders and the libraries the app is built on.
#include <algorithm>

#include "ui.h"

namespace ev33 {

void DrawSettings(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    ViewHeader(app, "Settings", "Settings apply to this session (they are not saved to disk).");
    float gap = 12 * S, avail = ImGui::GetContentRegionAvail().x, half = (avail - gap) / 2;

    BeginPanel(app, "appearance", ImVec2(half, 0));
    SectionTitle(app, "Appearance");
    static const char *const THEMES[] = { "Warm light", "Charcoal" };
    int th = app.dark ? 1 : 0;
    if (Segmented("theme", THEMES, 2, &th, P, 280 * S)) app.dark = th == 1;
    ImGui::Dummy(ImVec2(0, 4 * S));
    bool autoScale = app.scale_user <= 0;
    if (ImGui::Checkbox("Interface size follows the monitor", &autoScale)) {
        app.scale_user = autoScale ? 0 : app.scale;
        app.rebuild_fonts = true;
    }
    if (!autoScale) {
        float s = app.scale_user;
        ImGui::SetNextItemWidth(280 * S);
        if (ImGui::SliderFloat("Interface size", &s, 0.8f, 2.0f, "%.2f x")) app.scale_user = s;
        if (ImGui::IsItemDeactivatedAfterEdit()) app.rebuild_fonts = true;
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Current size %.2f x. Fonts: %s.", app.scale,
                       app.fonts.fromFiles ? "IBM Plex Sans and Plex Mono (assets/fonts, SIL Open Font License)" : "built-in Dear ImGui font (assets/fonts not found)");
    ImGui::PopFont();
    EndPanel();
    ImGui::SameLine(0, gap);
    BeginPanel(app, "defaults", ImVec2(half, 0));
    SectionTitle(app, "Defaults");
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "Trip Planner algorithm for a new city");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##defmethod", PLANNER_LABEL[app.default_method])) {
        for (int m = 0; m < PLANNER_COUNT; m++)
            if (ImGui::Selectable(PLANNER_LABEL[m], m == app.default_method)) app.default_method = m;
        ImGui::EndCombo();
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "Day simulation: when every port is busy");
    ImGui::PopFont();
    static const char *const POL[] = { "Drivers wait", "Switch after 20 min" };
    Segmented("pol", POL, 2, &app.policy, P);
    ImGui::Dummy(ImVec2(0, 2 * S));
    ImGui::Checkbox("Road Network: terrain and contours", &app.layers.terrain);
    ImGui::Checkbox("Road Network: junction dots", &app.layers.junctions);
    ImGui::Checkbox("Road Network: junction names when zoomed in", &app.layers.labels);
    EndPanel();

    ImGui::Dummy(ImVec2(0, 6 * S));
    BeginPanel(app, "paths", ImVec2(half, 0));
    SectionTitle(app, "Folders");
    if (ImGui::BeginTable("paths", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [&](const char *k, const std::string &v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", k);
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextWrapped("%s", v.c_str()); ImGui::PopFont();
        };
        row("Cities", E.data_dir);
        row("Benchmark results", E.bench_path.empty() ? E.results_dir + "/results.csv" : E.bench_path);
        row("Fonts", app.assets_dir);
        row("Program", app.exe_dir);
        ImGui::EndTable();
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Start the program with --data, --results or --assets to use other folders (--help lists every option).");
    ImGui::PopFont();
    EndPanel();
    ImGui::SameLine(0, gap);
    BeginPanel(app, "about", ImVec2(half, 0));
    SectionTitle(app, "About");
    ImGui::TextWrapped("EV33 Charge Atlas 2.0: EV charging station placement and trip planning. Engine, algorithms and interface are written in C++17.");
    if (ImGui::BeginTable("libs", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [&](const char *k, const std::string &v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", k);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", v.c_str());
        };
        row("Engine", "C++17 standard library only (include/ev, src/)");
        row("Interface", std::string("Dear ImGui ") + IMGUI_VERSION + " (MIT licence, third_party/imgui)");
        row("Window and input", "GLFW " + app.glfw_version.substr(0, app.glfw_version.find(' ')) + " (zlib licence, third_party/glfw)");
        row("Rendering", "OpenGL " + app.gl_version);
#ifdef __VERSION__
        row("Compiler", __VERSION__);
#endif
        ImGui::EndTable();
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::PushStyleColor(ImGuiCol_Text, P.ink3);
    ImGui::TextWrapped("The interface libraries are external dependencies, vendored in third_party/ and built by CMake; the engine does not use them.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    EndPanel();
}

} // namespace ev33
