// view_network.cpp - the road network drawn by network.cpp, with layers, a legend, station details
// and a day simulation mode (ev::simulate_day replayed minute by minute).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "content.h"
#include "ui.h"

namespace ev33 {

static void stationDetails(App &app, Run &R) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    const Scene &sc = E.city().sc;
    int i = app.station_panel;
    if (i < 0 || i >= sc.ns) return;
    const SiteV &s = sc.stations[(size_t)i];
    ImGui::Separator();
    ImGui::PushFont(app.fonts.semi);
    ImGui::Text("%s", s.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::SmallButton("Close")) app.station_panel = -1;
    bool built = R.built[(size_t)i] != 0;
    ImGui::PushFont(app.fonts.smallSemi);
    if (s.existing) Tag(s.available ? "EXISTING" : ("EXISTING, " + s.status).c_str(), s.available ? P.secondarySoft : P.errorSoft, s.available ? P.secondary : P.error);
    else Tag(built ? "NEW STATION (SELECTED)" : "CANDIDATE (NOT SELECTED)", built ? P.primarySoft : P.panel2, built ? P.primary : P.ink2);
    ImGui::PopFont();
    if (ImGui::BeginTable("st", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [&](const char *k, const std::string &v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", k);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(v.c_str());
        };
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
        ImGui::EndTable();
    }
    int ports = E.effectivePorts(i);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Ports");
    ImGui::SameLine();
    if (ImGui::Button("\xE2\x88\x92") && ports > 1) { Plan &n = app.edit(); if (ports - 1 == E.basePorts(i)) n.edits.ports.erase(i); else n.edits.ports[i] = ports - 1; }
    ImGui::SameLine();
    ImGui::Text("%d", ports);
    ImGui::SameLine();
    if (ImGui::Button("+") && ports < 64) { Plan &n = app.edit(); if (ports + 1 == E.basePorts(i)) n.edits.ports.erase(i); else n.edits.ports[i] = ports + 1; }
    if (!s.existing) {
        int mode = E.plan.edits.lock.count(i) ? 1 : E.plan.edits.ban.count(i) ? 2 : 0;
        static const char *const M[] = { "Auto", "Always build", "Never build" };
        if (Segmented("build", M, 3, &mode, P)) {
            Plan &n = app.edit();
            n.edits.lock.erase(i);
            n.edits.ban.erase(i);
            if (mode == 1) n.edits.lock.insert(i);
            if (mode == 2) n.edits.ban.insert(i);
        }
    }
}

void DrawRoadNetwork(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    City &C = E.city();
    const Scene &sc = C.sc;
    Run *R = E.run();
    ViewHeader(app, "Road Network", nullptr);
    SyntheticTag(app, sc);
    ImGui::SameLine();
    ImGui::PushFont(app.fonts.small);
    ImGui::AlignTextToFramePadding();
    std::string proj = fint(sc.nn) + " junctions, " + fint(sc.ne) + " roads, " + fint(C.maxx - C.minx) + " x " + fint(C.maxy - C.miny) +
                       " km. Positions: latitude/longitude projected onto a flat plane (equirectangular, around the mean latitude)" +
                       (sc.cfg.source == "synthetic" ? "; this is a generated graph, not a real place." : ".");
    ImGui::TextColored(P.ink3, "%s", proj.c_str());
    ImGui::PopFont();
    if (!R) { ImGui::TextColored(P.error, "%s", E.status.c_str()); return; }

    float sideW = 300 * S, gap = 12 * S;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float canvasW = avail.x - sideW - gap;

    // day simulation state
    SimView *sim = nullptr;
    LiveState live;
    if (app.net_mode == NET_DAY) {
        sim = E.sim(app.policy);
        if (sim) {
            if (app.playing) {
                app.T += ImGui::GetIO().DeltaTime * app.speed;
                if (app.T >= sim->end_min) { app.T = sim->end_min; app.playing = false; }
            }
            sim->at(app.T, false, live);
        }
    }
    const std::vector<int> *route = nullptr, *stops = nullptr;
    if (app.answer.done && app.answer.error.empty() && app.answer.city == E.plan.city) {
        route = &app.answer.path;
        if (app.answer.plan.feasible()) stops = &app.answer.stop_station;
    }
    int picked = DrawNetwork(app, app.net, "##network", ImVec2(canvasW, avail.y), (NetMode)app.net_mode, app.layers, route, stops,
                             sim ? &live : nullptr, sim);
    if (picked >= 0) app.station_panel = picked;

    ImGui::SameLine(0, gap);
    BeginPanel(app, "netside", ImVec2(sideW, avail.y), true);
    static const char *const MODES[] = { "Plan", "Day simulation" };
    Segmented("mode", MODES, 2, &app.net_mode, P);
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (app.net_mode == NET_PLAN) {
        SectionTitle(app, "Layers");
        NetLayers &L = app.layers;
        ImGui::Checkbox("Terrain and contours", &L.terrain);
        ImGui::Checkbox("Junctions", &L.junctions);
        ImGui::Checkbox("Junction names (zoomed in)", &L.labels);
        ImGui::Checkbox("Existing stations", &L.existing);
        ImGui::Checkbox("Candidate sites", &L.candidates);
        ImGui::Checkbox("New stations", &L.built);
        ImGui::Checkbox("Route from Trip Planner", &L.route);
        ImGui::Checkbox("Traffic (planned trips per road)", &L.traffic);
        if (sc.nn > 600) {
            ImGui::PushFont(app.fonts.small);
            ImGui::TextColored(P.ink3, "Large network: junction dots appear when you zoom in.");
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0, 4 * S));
        SectionTitle(app, "Legend");
        NetworkLegend(app, NET_PLAN, L);
        ImGui::PushFont(app.fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, P.ink3);
        ImGui::TextWrapped("Wheel: zoom. Drag: pan. Double-click: zoom in. Click a station for details.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        stationDetails(app, *R);
    } else if (sim) {
        SectionTitle(app, "Simulated day", "Every planned trip is replayed with the real port counts; a driver who finds every port busy queues.");
        ImGui::PushFont(app.fonts.monoBig);
        int day = (int)(app.T / 1440) + 1;
        ImGui::Text("%s", fclock(std::fmod(app.T, 1440)).c_str());
        ImGui::PopFont();
        if (day > 1) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextColored(P.ink3, "day %d", day); }
        float bw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        if (ImGui::Button(app.playing ? "Pause" : "Play", ImVec2(bw, 0))) { app.playing = !app.playing; if (app.T >= sim->end_min) app.T = 0; }
        ImGui::SameLine();
        if (ImGui::Button("-1 h", ImVec2(bw, 0))) app.T = std::max(0.0, app.T - 60);
        ImGui::SameLine();
        if (ImGui::Button("+1 h", ImVec2(bw, 0))) app.T = std::min(sim->end_min, app.T + 60);
        float t = (float)app.T;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##t", &t, 0, (float)sim->end_min, "")) app.T = t;
        static const char *const SPEEDS[] = { "1 min/s", "10 min/s", "1 h/s" };
        static const double SV[] = { 1, 10, 60 };
        int si = app.speed <= 1 ? 0 : app.speed <= 10 ? 1 : 2;
        if (Segmented("speed", SPEEDS, 3, &si, P)) app.speed = SV[si];
        static const char *const POL[] = { "Drivers wait", "Switch after 20 min" };
        if (Segmented("policy", POL, 2, &app.policy, P)) sim = E.sim(app.policy);
        ImGui::Dummy(ImVec2(0, 4 * S));
        if (ImGui::BeginTable("now", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto row = [&](const char *k, const std::string &v, const ImVec4 &c) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", k);
                ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextColored(c, "%s", v.c_str()); ImGui::PopFont();
            };
            row("On the road", fint(live.road), P.ink);
            row("Charging", fint(live.charging_total), P.ink);
            row("Waiting for a port", fint(live.waiting_total), live.waiting_total ? P.warn : P.ink);
            row("Arrived", fint(live.arrived) + " of " + fint(sim->sim.nv), P.ink);
            row("Grid draw now", fint(live.kw) + " kW", live.kw > sc.cfg.grid_kw ? P.error : P.ink);
            row("Energy delivered", f1(live.kwh / 1000) + " MWh", P.ink);
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, 4 * S));
        NetworkLegend(app, NET_DAY, app.layers);
        ImGui::Dummy(ImVec2(0, 4 * S));
        RichText(liveSummary(E, *sim, nullptr, app.policy), app.fonts, col(P.ink2));
    } else {
        ImGui::TextColored(P.error, "%s", E.status.c_str());
    }
    EndPanel();
}

} // namespace ev33
