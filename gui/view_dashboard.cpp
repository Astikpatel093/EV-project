// view_dashboard.cpp - the loaded dataset and the current plan at a glance. Every figure is read
// from the engine's current run (shared/engine.h): nothing here is typed in by hand.
#include <algorithm>
#include <cmath>

#include <functional>

#include "content.h"
#include "ev/algorithms/sorting.hpp"
#include "ui.h"

namespace ev33 {

// Lays out `n` metric panels in rows of at most `perRow`.
static void metricRow(App &app, const char *id, int n, int perRow, const std::function<void(int, float, float)> &draw) {
    float S = app.scale, gap = 10 * S;
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, std::min(perRow, (int)((avail + gap) / (190 * S + gap))));
    float w = (avail - gap * (float)(cols - 1)) / (float)cols, h = 104 * S;
    ImGui::PushID(id);
    for (int i = 0; i < n; i++) {
        if (i % cols) ImGui::SameLine(0, gap);
        draw(i, w, h);
    }
    ImGui::PopID();
}

void DrawDashboard(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    City &C = E.city();
    const Scene &sc = C.sc;
    Run *R = E.run();
    ViewHeader(app, "Dashboard", ("Network, stations and trips of \"" + C.label + "\", with the result of the current plan: " +
                                  E.planLabel(E.plan) + ". Change the plan in Station Placement.").c_str());
    SyntheticTag(app, sc);
    if (!sc.cfg.source_note.empty()) {
        ImGui::SameLine();
        ImGui::PushFont(app.fonts.small);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.ink3, "Source: %s", sc.cfg.source_note.c_str());
        ImGui::PopFont();
    }
    if (!R) {
        ImGui::TextColored(P.error, "%s", E.status.c_str());
        return;
    }
    ImGui::Dummy(ImVec2(0, 4 * S));

    // ---- the dataset
    SectionTitle(app, "Dataset");
    int offline = 0;
    for (int i = 0; i < sc.nexist; i++) offline += !sc.stations[(size_t)i].available;
    metricRow(app, "data", 5, 5, [&](int i, float w, float h) {
        switch (i) {
        case 0: Metric(app, "m_nodes", "Road nodes", fint(sc.nn), "junctions in the road network", w, h); break;
        case 1: Metric(app, "m_edges", "Road edges", fint(sc.ne), "two-way road segments", w, h); break;
        case 2: Metric(app, "m_exist", "Existing charging stations", fint(sc.nexist),
                       offline ? fint(offline) + " offline or in maintenance" : std::string("all available"), w, h, &P.secondary); break;
        case 3: Metric(app, "m_cand", "Candidate locations", fint(sc.candidates()), "sites where a new station may be built", w, h); break;
        default: Metric(app, "m_trips", "Planned trips", fint(sc.nt), fint(sc.nveh) + " vehicle models", w, h); break;
        }
    });
    ImGui::Dummy(ImVec2(0, 6 * S));

    // ---- the plan
    SectionTitle(app, "Placement result", SELECT_LONG[E.plan.sel]);
    const SelV &sel = R->run.sel;
    double budget = E.plan.budget, remaining = budget - sel.cost;
    metricRow(app, "plan", 4, 4, [&](int i, float w, float h) {
        switch (i) {
        case 0: Metric(app, "m_sel", "Selected charging stations", fint(sel.n),
                       sel.optimal ? "provably optimal for the benefit scores" : "heuristic choice (not guaranteed optimal)", w, h, &P.primary); break;
        case 1: Metric(app, "m_budget", "Installation budget", rupees(budget), C.default_budget == E.plan.budget ? "the dataset's budget" : "changed in Station Placement", w, h); break;
        case 2: Metric(app, "m_cost", "Total installation cost", rupees(sel.cost), "benefit score " + fint(sel.value), w, h); break;
        default: Metric(app, "m_left", "Remaining budget", rupees(remaining), sel.over_budget ? "locked sites alone exceed the budget" : "never negative: the budget is a hard limit", w, h,
                        sel.over_budget ? &P.error : nullptr); break;
        }
    });
    ImGui::Dummy(ImVec2(0, 6 * S));

    // ---- trips and capacity
    const KpiV &k = R->run.kpi;
    float avail = ImGui::GetContentRegionAvail().x, gap = 10 * S;
    float half = (avail - gap) / 2;
    BeginPanel(app, "service", ImVec2(half, 0));
    SectionTitle(app, "Trips with this plan");
    ImGui::PushFont(app.fonts.monoBig);
    ImGui::Text("%s%%", f1(k.feasible_pct).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.ink2, "of trips can be completed (%s of %s)", fint(k.feasible).c_str(), fint(k.trips).c_str());
    if (ImGui::BeginTable("svc", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [&](const char *a, const std::string &b) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", a);
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextUnformatted(b.c_str()); ImGui::PopFont();
        };
        row("Shortest route beyond the starting charge", fint(k.need_charge) + " trips, " + fint(k.charged_ok) + " still completed");
        row("Not possible", fint(k.infeasible) + " trips (reasons in Trip Planner)");
        row("Average completed trip", f1(k.avg_km) + " km, " + f2(k.avg_kwh) + " kWh");
        row("Average charging", f2(k.avg_stops) + " stops, " + f1(k.avg_charge_min) + " min");
        row("Average door-to-door time", fdur(k.avg_travel_min));
        row("Computed in", fms(R->total_ms) + " (" + fint((double)R->run.sssp) + " shortest-path trees)");
        ImGui::EndTable();
    }
    EndPanel();
    ImGui::SameLine(0, gap);
    BeginPanel(app, "capacity", ImVec2(half, 0));
    SectionTitle(app, "Charging capacity");
    const CapV &cap = R->cap;
    ImGui::PushFont(app.fonts.monoBig);
    ImGui::Text("%s", fint((double)cap.flow_dfs).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.ink2, "of %s charge-needing trips fit the port slots", fint(cap.trips_needing).c_str());
    RichText(flowNote(sc, cap), app.fonts, col(P.ink2));
    ImGui::PushFont(app.fonts.small);
    if (cap.consistent) Tag("Ford-Fulkerson = Edmonds-Karp = min cut = bipartite matching", P.primarySoft, P.primary);
    else Tag("The flow methods disagree", P.errorSoft, P.error);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2 * S));
    RichText(surgeNote(R->ses), app.fonts, col(P.ink2));
    ImGui::TextColored(P.ink2, "%s of %s planned charging sessions can be served with the installed ports.",
                       fint((double)R->ses.served).c_str(), fint((double)R->ses.sessions).c_str());
    EndPanel();
    ImGui::Dummy(ImVec2(0, 6 * S));

    // ---- hourly energy against grid supply, busiest stations
    BeginPanel(app, "hourly", ImVec2(half, 0));
    SectionTitle(app, "Charging energy by hour", "Bars: kWh drawn by planned charging sessions. Dashed line: grid supply per hour. Amber: energy above the supply.");
    std::vector<std::string> labels;
    std::vector<double> base, over;
    double grid = sc.cfg.grid_kw;
    for (size_t h = 0; h < R->ses.hourly_kwh.size(); h++) {
        char b[8];
        std::snprintf(b, sizeof b, "%02d", (int)h);
        labels.push_back(b);
        double v = R->ses.hourly_kwh[h];
        base.push_back(std::min(v, grid));
        over.push_back(std::max(0.0, v - grid));
    }
    ChartOptions o;
    o.hLine = grid;
    std::string gl = "grid supply " + fint(grid) + " kWh";
    o.hLineLabel = gl.c_str();
    o.xLabel = "hour of the day";
    o.yLabel = "kWh";
    BarChart(app, "hourbars", ImVec2(ImGui::GetContentRegionAvail().x, 230 * S), labels, base, &over, P.secondary, P.warn, o);
    EndPanel();
    ImGui::SameLine(0, gap);
    BeginPanel(app, "busiest", ImVec2(half, 0));
    SectionTitle(app, "Busiest charging stations", "Sessions planned at each station against the chargers it has (interval partitioning gives the chargers needed).");
    std::vector<LoadV> loads = R->ses.st;
    ev::merge_sort(loads, [](const LoadV &a, const LoadV &b) { return a.sessions > b.sessions; });
    if (ImGui::BeginTable("busy", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Station");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Sessions");
        ImGui::TableSetupColumn("Chargers needed");
        ImGui::TableSetupColumn("Has");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < loads.size() && i < 8; i++) {
            const LoadV &L = loads[i];
            if (L.sessions == 0) break;
            const SiteV &s = sc.stations[(size_t)L.station];
            int has = R->ports[(size_t)L.station];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d  %s", s.id, s.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(s.existing ? P.secondary : P.primary, "%s", s.existing ? "existing" : "new");
            ImGui::TableNextColumn(); ImGui::Text("%d", L.sessions);
            ImGui::TableNextColumn(); ImGui::TextColored(L.chargers_needed > has ? P.error : P.ink, "%d", L.chargers_needed);
            ImGui::TableNextColumn(); ImGui::Text("%d", has);
        }
        ImGui::EndTable();
    }
    EndPanel();
}

} // namespace ev33
