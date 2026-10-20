// views.cpp - the sidebar and the seven pages of the EV33 desktop app.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "content.h"
#include "implot.h"
#include "ui.h"

namespace ev33 {

/* plan changes are applied at the start of the next frame, so results stay valid while a frame draws */
static Plan &edit(App &app) {
    if (!app.has_next) { app.next = app.eng.plan; app.has_next = true; }
    return app.next;
}


static void smallText(App &app, const std::string &s, const ImVec4 *c = nullptr) {
    ImGui::PushFont(app.fonts.small);
    ImGui::PushStyleColor(ImGuiCol_Text, c ? *c : app.pal.ink2);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static void label(App &app, const char *s) {
    ImGui::PushFont(app.fonts.semi);
    ImGui::TextUnformatted(s);
    ImGui::PopFont();
}

/* ======================================================================== sidebar */
void drawSidebar(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;

    label(app, "City");
    for (int i = 0; i < (int)E.cities.size(); i++) {
        City &c = E.cities[(size_t)i];
        E.load(i);
        bool sel = i == E.plan.city;
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = ImGui::GetTextLineHeight() * 2.4f + 10 * S;
        if (ImGui::InvisibleButton("city", ImVec2(w, h)) && !sel) app.pending_city = i;
        bool hov = ImGui::IsItemHovered();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(sel ? mix(P.cobaltSoft, P.paper, 0.4f) : hov ? P.paper2 : P.paper), 6 * S);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), col(sel ? P.cobalt : P.line), 6 * S, 0, 1);
        if (sel) dl->AddRectFilled(p, ImVec2(p.x + 3 * S, p.y + h), col(P.cobalt), 6 * S, ImDrawFlags_RoundCornersLeft);
        dl->AddText(app.fonts.semi, app.fonts.semi->FontSize, ImVec2(p.x + 11 * S, p.y + 5 * S), col(P.ink), c.label.c_str());
        std::string sub = c.loaded ? fint(c.sc.nn) + " junctions, " + fint(c.sc.ns) + " sites, " + fint(c.sc.nt) + " trips"
                                   : "Could not load: " + c.error;
        dl->AddText(app.fonts.small, app.fonts.small->FontSize, ImVec2(p.x + 11 * S, p.y + 7 * S + app.fonts.semi->FontSize),
                    col(c.loaded ? P.ink3 : P.red), sub.c_str());
        if (hov) ImGui::SetTooltip("%s", c.blurb.c_str());
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6 * S));

    label(app, "Site selection");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sel", SELECT_LONG[E.plan.sel])) {
        for (int k = 0; k < EV_SEL_COUNT; k++) {
            int m = SEL_ORDER[k];
            if (ImGui::Selectable(SELECT_LONG[m], m == E.plan.sel)) edit(app).sel = m;
        }
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0, 4 * S));

    label(app, "Routing");
    bool bigCity = E.city().loaded && E.city().sc.nn > 1600;
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * S));
    for (int r = 0; r < EV_ROUTE_COUNT; r++) {
        bool on = E.plan.route == r, off = r == EV_ROUTE_FLOYD && bigCity;
        ImGui::PushID(r);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? P.ink : P.paper);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? P.ink : P.paper2);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? P.ink : P.paper3);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? P.paper : P.ink);
        ImGui::BeginDisabled(off);
        std::string t = ROUTE_LABEL[r];
        if (ImGui::Button(t.c_str(), ImVec2(-1, 0)) && !on) edit(app).route = r;
        ImGui::EndDisabled();
        ImGui::PopStyleColor(4);
        if (off && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Not offered above 1,600 junctions: the all-pairs table would be too large.");
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    smallText(app, ROUTE_HINT[E.plan.route], &P.ink3);
    ImGui::Dummy(ImVec2(0, 4 * S));

    label(app, "Installation budget");
    int def = E.city().default_budget;
    int maxb = std::max(100, (int)std::lround(def * 2 / 10.0) * 10);
    int b = app.has_next ? app.next.budget : E.plan.budget;
    {
        std::string v = rupees(b);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize(v.c_str()).x - 30 * S);
        ImGui::PushFont(app.fonts.semi);
        ImGui::TextColored(P.cobalt, "%s", v.c_str());
        ImGui::PopFont();
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##budget", &b, 0, maxb, "", ImGuiSliderFlags_AlwaysClamp)) edit(app).budget = b;
    smallText(app, "Default for this city: " + rupees(def) + ". Costs are in \xE2\x82\xB9 lakh.", &P.ink3);
    ImGui::Dummy(ImVec2(0, 4 * S));

    // what-if editing
    Run *R = E.run();
    if (R) {
        label(app, "Edit a site");
        const ev_scenario &sc = E.city().sc;
        std::string cur = app.panel >= 0 ? "Station " + std::to_string(sc.stations[app.panel].id) : "Choose a site, or click one on the map";
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##site", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            std::vector<int> order((size_t)sc.ns);
            for (int i = 0; i < sc.ns; i++) order[(size_t)i] = i;
            std::sort(order.begin(), order.end(), [&](int a, int c) {
                if (R->built[(size_t)a] != R->built[(size_t)c]) return R->built[(size_t)a] > R->built[(size_t)c];
                return sc.stations[a].id < sc.stations[c].id;
            });
            for (int i : order) {
                const char *tag = E.plan.edits.lock.count(i) ? "always built" : E.plan.edits.ban.count(i) ? "never built"
                                : R->built[(size_t)i] ? "built" : "not built";
                char buf[160];
                std::snprintf(buf, sizeof buf, "Station %d, junction %s (%s)", sc.stations[i].id, sc.nodes[sc.stations[i].node].name, tag);
                if (ImGui::Selectable(buf, i == app.panel)) { app.panel = i; app.force_page = PAGE_PLANNER; }
            }
            ImGui::EndCombo();
        }
        if (!E.plan.edits.empty()) {
            ImGui::Dummy(ImVec2(0, 2 * S));
            label(app, "What-if edits");
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("Clear all").x - ImGui::GetStyle().FramePadding.x * 2);
            ImGui::PushStyleColor(ImGuiCol_Text, P.cobalt);
            if (ImGui::SmallButton("Clear all")) edit(app).edits = Edits();
            ImGui::PopStyleColor();
            auto chip = [&](const std::string &text, int kind, int i) {
                ImGui::PushID(kind * 100000 + i);
                ImVec4 bg = kind == 0 ? P.cobaltSoft : kind == 1 ? mix(P.paper, P.red, 0.15f) : P.paper3;
                ImGui::PushStyleColor(ImGuiCol_Button, bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(bg, P.ink, 0.1f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20 * S);
                std::string t = text + "   \xC3\x97";
                if (ImGui::Button(t.c_str())) {
                    Plan &n = edit(app);
                    if (kind == 2) n.edits.ports.erase(i); else { n.edits.lock.erase(i); n.edits.ban.erase(i); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this edit");
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            };
            for (int i : E.plan.edits.lock) chip("Always build " + std::to_string(sc.stations[i].id), 0, i);
            for (int i : E.plan.edits.ban) chip("Never build " + std::to_string(sc.stations[i].id), 1, i);
            for (auto &kv : E.plan.edits.ports) chip("Station " + std::to_string(sc.stations[kv.first].id) + ": " + std::to_string(kv.second) + " ports", 2, kv.first);
        }
    }
    ImGui::Dummy(ImVec2(0, 6 * S));
    ImGui::Separator();
    bool err = !E.status.empty() && E.status[0] == '!';
    smallText(app, err ? E.status.substr(1) : E.status, err ? &P.red : &P.ink3);
}

/* ======================================================================== planner */
static void kpiCard(App &app, const char *id, const char *title, const std::string &value, const std::string &sub,
                    double delta, bool hasDelta, bool higherBetter, const char *unit, int digits, float w, float h) {
    const Palette &P = app.pal;
    float S = app.scale;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, P.paper);
    ImGui::BeginChild(id, ImVec2(w, h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "%s", title);
    ImGui::PopFont();
    ImGui::PushFont(app.fonts.h1);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    ImGui::PushFont(app.fonts.small);
    ImGui::PushStyleColor(ImGuiCol_Text, P.ink2);
    ImGui::TextWrapped("%s", sub.c_str());
    ImGui::PopStyleColor();
    if (hasDelta) {
        double v = std::fabs(delta) < std::pow(10.0, -digits) / 2 ? 0 : delta;
        std::string t = v == 0 ? "same as baseline"
                               : std::string(v > 0 ? "+" : "\xE2\x88\x92") + (digits ? f2(std::fabs(v)) : fint(std::fabs(v))) + unit + " vs baseline";
        bool good = (v > 0) == higherBetter;
        Pill(t.c_str(), v == 0 ? P.paper3 : good ? P.greenSoft : mix(P.paper, P.red, 0.16f), v == 0 ? P.ink3 : good ? P.green : P.red);
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    (void)S;
}

static void stationPanel(App &app, ImVec2 mapMin, ImVec2 mapSize) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    Run *R = E.run();
    int i = app.panel;
    if (!R || i < 0) return;
    const ev_scenario &sc = E.city().sc;
    const ev_station &st = sc.stations[i];
    float w = std::min(350 * S, mapSize.x - 24 * S);
    ImGui::SetCursorScreenPos(ImVec2(mapMin.x + mapSize.x - w - 12 * S, mapMin.y + 52 * S));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, P.paper);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * S, 12 * S));
    ImGui::BeginChild("station-panel", ImVec2(w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(app.fonts.h2);
    ImGui::Text("Station %d", st.id);
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    if (ImGui::Button("\xC3\x97", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) app.panel = -1;
    ImGui::TextColored(P.ink3, "Junction %s", sc.nodes[st.node].name);
    bool built = R->built[(size_t)i] != 0;
    int mode = E.plan.edits.lock.count(i) ? 1 : E.plan.edits.ban.count(i) ? 2 : 0;
    const char *status = built ? (mode == 1 ? "Built, by your edit" : "Built by the engine") : mode == 2 ? "Never built, by your edit" : "Not built";
    Pill(status, built ? P.cobaltSoft : mode == 2 ? mix(P.paper, P.red, 0.15f) : P.paper3, built ? P.cobalt : mode == 2 ? P.red : P.ink2);
    int ports = E.effectivePorts(i);
    if (ImGui::BeginTable("facts", 2, ImGuiTableFlags_SizingFixedFit)) {
        auto row = [&](const char *k, const std::string &v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink3, "%s", k);
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.semi); ImGui::TextUnformatted(v.c_str()); ImGui::PopFont();
        };
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d at %g kW", ports, st.power_kw);
        row("Ports", buf);
        row("Cost", rupees(st.cost));
        row("Service value", fint(st.value));
        for (int q = 0; q < R->ses.n; q++)
            if (R->ses.st[q].station == i)
                row("Sessions", fint(R->ses.st[q].sessions) + ", needs " + std::to_string(R->ses.st[q].chargers_needed) +
                                    (R->ses.st[q].chargers_needed == 1 ? " charger" : " chargers"));
        if (built) {
            SimView *sv = E.sim(app.policy);
            if (sv) {
                auto it = sv->local.find(i);
                if (it != sv->local.end()) {
                    const ev_sim_station &ss = sv->sim.st[it->second];
                    row("Queueing", ss.waited ? fint(ss.waited) + " drivers, " + fdur(ss.wait_min) + " in all" : "nobody waits");
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 2 * S));
    {
        static const char *const ml[] = { "Engine decides", "Always build", "Never build" };
        int m = mode;
        ImGui::PushFont(app.fonts.small);
        if (Segmented("build", ml, 3, &m, P)) {
            Plan &n = edit(app);
            n.edits.lock.erase(i);
            n.edits.ban.erase(i);
            if (m == 1) n.edits.lock.insert(i);
            if (m == 2) n.edits.ban.insert(i);
        }
        ImGui::PopFont();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Ports");
    ImGui::SameLine(80 * S);
    auto setPorts = [&](int n) {
        Plan &np = edit(app);
        n = std::max(1, std::min(64, n));
        if (n == E.basePorts(i)) np.edits.ports.erase(i); else np.edits.ports[i] = n;
    };
    ImGui::BeginDisabled(ports <= 1);
    if (ImGui::Button("\xE2\x88\x92", ImVec2(34 * S, 0))) setPorts(ports - 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushFont(app.fonts.semi);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d port%s", ports, ports == 1 ? "" : "s");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::BeginDisabled(ports >= 64);
    if (ImGui::Button("+", ImVec2(34 * S, 0))) setPorts(ports + 1);
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

static void batteryChart(App &app, const TripDetail &D, float height) {
    const Palette &P = app.pal;
    const ev_scenario &sc = app.eng.city().sc;
    double B = sc.cfg.battery_kwh, reserve = sc.cfg.reserve_frac * B, target = sc.cfg.target_frac * B;
    std::vector<double> km, kwh, elev;
    for (auto &p : D.profile) { km.push_back(p[0]); kwh.push_back(p[1]); elev.push_back(p[2]); }
    double total = km.empty() ? 1 : std::max(1e-6, km.back());
    double emin = 1e9, emax = -1e9;
    for (double e : elev) { emin = std::min(emin, e); emax = std::max(emax, e); }
    if (ImPlot::BeginPlot("##battery", ImVec2(-1, height), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("km", "kWh", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, total, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, B, ImPlotCond_Always);
        ImPlot::SetupAxis(ImAxis_Y2, "elevation, m", ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_Y2, emin - 20, emax + (emax - emin) * 1.5 + 20, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        if (!km.empty()) {
            ImPlot::SetNextFillStyle(P.contour, 0.35f);
            ImPlot::PlotShaded("Elevation", km.data(), elev.data(), (int)km.size(), -INFINITY);
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
        ImPlot::SetNextLineStyle(P.red, 1.5f);
        ImPlot::PlotInfLines("Reserve", &reserve, 1, ImPlotInfLinesFlags_Horizontal);
        ImPlot::SetNextLineStyle(P.ink3, 1.0f);
        ImPlot::PlotInfLines("Charge target", &target, 1, ImPlotInfLinesFlags_Horizontal);
        if (!km.empty()) {
            ImPlot::SetNextLineStyle(P.cobalt, 2.5f);
            ImPlot::PlotLine("Battery", km.data(), kwh.data(), (int)km.size());
        }
        double prog = std::min(1.0, (ImGui::GetTime() - app.trip_anim) / 2.2) * total;
        if (prog < total) {
            ImPlot::SetNextLineStyle(P.ink, 1.0f);
            ImPlot::PlotInfLines("##now", &prog, 1);
        }
        ImPlot::EndPlot();
    }
}

void drawPlanner(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    Run *R = E.run();
    if (!R) { RichText("The engine could not load this city: " + E.city().error, app.fonts); return; }
    const ev_scenario &sc = E.city().sc;
    const ev_kpi &k = R->run.kpi;
    const ev_selection &s = R->run.sel;
    const Baseline &BL = E.baseline;
    bool bl = BL.set && BL.plan.city == E.plan.city;

    // headline figures
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float w = (ImGui::GetContentRegionAvail().x - gap * 4) / 5;
    float h = 128 * S;
    kpiCard(app, "k1", "Trips completed", f1(k.feasible_pct) + "%", fint(k.feasible) + " of " + fint(k.trips) + " trips",
            k.feasible - BL.kpi.feasible, bl, true, " trips", 0, w, h);
    ImGui::SameLine();
    kpiCard(app, "k2", "Needed a charge and got one", fint(k.charged_ok), "of " + fint(k.need_charge) + " that could not finish on their starting charge",
            k.charged_ok - BL.kpi.charged_ok, bl, true, "", 0, w, h);
    ImGui::SameLine();
    kpiCard(app, "k3", "Stations built", fint(s.n), rupees(s.cost) + " of " + rupees(E.plan.budget) + (s.optimal ? ", provably optimal" : "") +
            (s.over_budget ? ". Your locked sites cost more than the budget." : ""), (double)(s.cost - BL.cost), bl, false, " lakh", 0, w, h);
    ImGui::SameLine();
    kpiCard(app, "k4", "Average trip", f1(k.avg_km) + " km",
            f2(k.avg_kwh) + " kWh, " + f2(k.avg_stops) + " stops, " + f1(k.avg_charge_min) + " min charging", k.avg_kwh - BL.kpi.avg_kwh, bl, false, " kWh", 2, w, h);
    ImGui::SameLine();
    kpiCard(app, "k5", "Engine time", fms(R->total_ms), fint(ev_router_sssp_runs(R->run.router)) + " shortest-path trees", 0, false, false, "", 0, w, h);

    // baseline and export
    if (ImGui::Button(BL.set ? "Clear baseline" : "Pin as baseline")) { if (BL.set) E.clearBaseline(); else E.pinBaseline(); }
    ImGui::SameLine();
    if (ImGui::Button("Copy built sites as CSV")) {
        ImGui::SetClipboardText(E.sitesCsv().c_str());
        app.notify("Copied " + std::to_string(s.n) + " built sites as CSV (" + E.planLabel(E.plan) + ").", ImGui::GetTime());
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.ink3, "%s", BL.set ? ("Baseline: " + BL.label + ". Changes now show against it, here and on Live day.").c_str()
                                           : "Pin a plan to compare every later run against it.");

    // map and trip inspector
    float side = 380 * S;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float mapW = avail.x - side - gap, mapH = std::max(360 * S, avail.y - 4 * S);
    ImVec2 mapMin = ImGui::GetCursorScreenPos();
    int picked = drawCityMap(app, app.plannerMap, "planner-map", ImVec2(mapW, mapH), MAP_PLANNER, nullptr, nullptr);
    if (picked >= 0) app.panel = picked;
    ImVec2 after = ImGui::GetCursorScreenPos();
    stationPanel(app, mapMin, ImVec2(mapW, mapH));
    ImGui::SetCursorScreenPos(ImVec2(mapMin.x + mapW + gap, mapMin.y));

    ImGui::BeginChild("inspector", ImVec2(side, mapH), ImGuiChildFlags_None);
    Heading("Trip inspector", app.fonts);
    smallText(app, "Choose a trip to replay it on the map. The chart shows the battery along the way, with the reserve the planner never crosses.", &P.ink3);
    if (app.trip < 0 && !R->samples.empty()) { app.trip = R->samples[0].trip; app.trip_anim = ImGui::GetTime(); }
    ImGui::BeginChild("trips", ImVec2(-1, 150 * S), ImGuiChildFlags_Borders);
    for (auto &smp : R->samples) {
        ImGui::PushID(smp.trip);
        char buf[96];
        const char *kind = smp.kind == 2 ? "stops" : smp.kind == 1 ? "1 stop" : smp.kind == 0 ? "Direct" : "Not possible";
        if (smp.kind == 2) std::snprintf(buf, sizeof buf, "Trip %d", sc.trips[smp.trip].id);
        else std::snprintf(buf, sizeof buf, "Trip %d", sc.trips[smp.trip].id);
        if (ImGui::Selectable("##t", app.trip == smp.trip, 0, ImVec2(0, ImGui::GetTextLineHeight()))) { app.trip = smp.trip; app.trip_anim = ImGui::GetTime(); }
        ImGui::SameLine(6 * S);
        std::string badge = smp.kind == 2 ? std::to_string(smp.stops) + " " + kind : kind;
        ImVec4 bc = smp.kind == -1 ? P.red : smp.kind >= 1 ? P.cobalt : P.ink3;
        ImGui::PushFont(app.fonts.small);
        ImGui::TextColored(bc, "%-12s", badge.c_str());
        ImGui::PopFont();
        ImGui::SameLine(110 * S);
        ImGui::TextUnformatted(buf);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 70 * S);
        ImGui::TextColored(P.ink3, "%s km", f1(smp.km).c_str());
        ImGui::PopID();
    }
    if (R->samples.empty()) ImGui::TextColored(P.ink3, "This plan has no trips to replay.");
    ImGui::EndChild();

    if (app.trip >= 0) {
        const TripDetail &D = E.trip(app.trip);
        const ev_trip &tr = sc.trips[app.trip];
        ImGui::PushFont(app.fonts.semi);
        ImGui::Text("Trip %d: %s to %s", tr.id, sc.nodes[tr.src].name, sc.nodes[tr.dst].name);
        ImGui::PopFont();
        ImGui::PushFont(app.fonts.small);
        RichText(tripFacts(sc, D), app.fonts);
        ImGui::PopFont();
        batteryChart(app, D, 170 * S);
        ImGui::PushFont(app.fonts.small);
        for (auto &st : tripItinerary(sc, D)) {
            ImGui::TextColored(P.ink3, "%s", st.first.c_str());
            ImGui::SameLine(48 * S);
            RichText(st.second, app.fonts);
        }
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(after);
    ImGui::Dummy(ImVec2(0, 0));
}

/* ======================================================================== live day */
void drawLive(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    Run *R = E.run();
    SimView *sv = E.sim(app.policy);
    if (!R || !sv) { RichText("The engine could not run this plan.", app.fonts); return; }
    if (!app.autoplayed) { app.autoplayed = true; app.playing = true; }
    if (app.playing) {
        app.T += std::min(0.25f, ImGui::GetIO().DeltaTime) * app.speed;
        if (app.T > sv->end_min) app.T = 0;
    }
    if (app.T > sv->end_min) app.T = std::fmod(app.T, sv->end_min);
    LiveState st;
    sv->at(app.T, false, st);
    const ev_scenario &sc = E.city().sc;

    float gap = ImGui::GetStyle().ItemSpacing.x;
    float side = 360 * S;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float chartH = 150 * S, controlsH = ImGui::GetFrameHeightWithSpacing() * 2 + 8 * S;
    float mapW = avail.x - side - gap, mapH = std::max(320 * S, avail.y - chartH - controlsH - 12 * S);
    ImVec2 top = ImGui::GetCursorScreenPos();

    ImGui::BeginGroup();
    drawCityMap(app, app.liveMap, "live-map", ImVec2(mapW, mapH), MAP_LIVE, &st, sv);
    // clock overlay
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 c(top.x + 16 * S, top.y + 10 * S);
        dl->AddRectFilled(ImVec2(c.x - 6 * S, c.y - 4 * S), ImVec2(c.x + 170 * S, c.y + app.fonts.big->FontSize + 30 * S), col(P.paper, 0.85f), 8 * S);
        dl->AddText(app.fonts.big, app.fonts.big->FontSize, c, col(P.ink), fclock(app.T).c_str());
        int day = (int)std::floor(app.T / 1440) + 1;
        std::string dtext = day > 1 ? "Day " + std::to_string(day) + ": queues from yesterday" : "Day 1";
        dl->AddText(app.fonts.small, app.fonts.small->FontSize, ImVec2(c.x + 2 * S, c.y + app.fonts.big->FontSize + 2 * S), col(P.ink3), dtext.c_str());
    }
    // controls
    if (ImGui::Button(app.playing ? "Pause" : "Play", ImVec2(90 * S, 0))) app.playing = !app.playing;
    ImGui::SameLine();
    {
        static const char *const sl[] = { "Real time", "1 min/s", "10 min/s", "1 h/s" };
        static const double sv4[] = { 1.0 / 60, 1, 10, 60 };
        int cur = 2;
        for (int q = 0; q < 4; q++) if (std::fabs(app.speed - sv4[q]) < 1e-9) cur = q;
        if (Segmented("speed", sl, 4, &cur, P, 330 * S)) app.speed = sv4[cur];
        ImGui::SameLine();
    }
    if (ImGui::Button("Go to the current time")) {
        time_t now = time(nullptr);
        struct tm *lt = localtime(&now);
        app.T = lt->tm_hour * 60 + lt->tm_min + lt->tm_sec / 60.0;
        app.speed = 1.0 / 60;
        app.playing = true;
    }

    float tf = (float)app.T;
    ImGui::SetNextItemWidth(mapW);
    std::string tl = fclock(app.T);
    if (ImGui::SliderFloat("##time", &tf, 0, (float)sv->end_min, tl.c_str())) app.T = tf;
    // grid draw over the day
    if (ImPlot::BeginPlot("##grid", ImVec2(mapW, chartH), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
        double grid = sc.cfg.grid_kw, ymax = grid * 1.15;
        for (double v : sv->series_kw) ymax = std::max(ymax, v * 1.1);
        ImPlot::SetupAxes("hour", "kW", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, sv->end_min / 60, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, ymax, ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, TickFormat);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Horizontal);
        ImPlot::SetNextFillStyle(P.cobalt, 0.18f);
        ImPlot::PlotShaded("##draw", sv->series_t.data(), sv->series_kw.data(), (int)sv->series_t.size(), 0);
        ImPlot::SetNextLineStyle(P.cobalt, 2);
        ImPlot::PlotLine("Charging power drawn", sv->series_t.data(), sv->series_kw.data(), (int)sv->series_t.size());
        ImPlot::SetNextLineStyle(P.ink, 1.2f);
        ImPlot::PlotInfLines("Grid supply", &grid, 1, ImPlotInfLinesFlags_Horizontal);
        double th = app.T / 60;
        ImPlot::SetNextLineStyle(P.amber, 2);
        ImPlot::PlotInfLines("##now", &th, 1);
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) app.T = std::max(0.0, ImPlot::GetPlotMousePos().x * 60);
        ImPlot::EndPlot();
    }
    ImGui::EndGroup();

    // side column
    ImGui::SetCursorScreenPos(ImVec2(top.x + mapW + gap, top.y));
    ImGui::BeginChild("live-side", ImVec2(side, avail.y), ImGuiChildFlags_None);
    ImGui::PushFont(app.fonts.semi);
    ImGui::TextUnformatted("When every port at a station is busy");
    ImGui::PopFont();
    {
        static const char *const pl[] = { "Drivers wait", "Drivers switch after 20 min" };
        Segmented("policy", pl, 2, &app.policy, P);
    }
    ImGui::Dummy(ImVec2(0, 6 * S));
    if (ImGui::BeginTable("stats", 2, ImGuiTableFlags_BordersInner | ImGuiTableFlags_SizingStretchSame)) {
        auto stat = [&](const char *k, const std::string &v, bool hot = false) {
            ImGui::TableNextColumn();
            ImGui::PushFont(app.fonts.small);
            ImGui::TextColored(P.ink3, "%s", k);
            ImGui::PopFont();
            ImGui::PushFont(app.fonts.h1);
            ImGui::TextColored(hot ? P.amber : P.ink, "%s", v.c_str());
            ImGui::PopFont();
        };
        stat("On the road", fint(st.road));
        stat("Charging", fint(st.charging_total));
        stat("Waiting for a port", fint(st.waiting_total), st.waiting_total > 0);
        stat("Arrived", fint(st.arrived) + " of " + fint(sv->sim.nv));
        stat("Grid draw now", st.kw >= 1000 ? f2(st.kw / 1000) + " MW" : fint(st.kw) + " kW");
        stat("Energy delivered", st.kwh >= 1000 ? f1(st.kwh / 1000) + " MWh" : fint(st.kwh) + " kWh");
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 6 * S));
    ImGui::PushFont(app.fonts.semi);
    ImGui::TextUnformatted("Busiest stations right now");
    ImGui::PopFont();
    struct Row { int k, busy, wait, ports; };
    std::vector<Row> rows;
    for (int k = 0; k < sv->sim.nst; k++) {
        int s = sv->sim.st[k].station;
        if (st.charging[(size_t)k] || st.waiting[(size_t)k]) rows.push_back({ k, st.charging[(size_t)k], st.waiting[(size_t)k], R->ports[(size_t)s] });
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.wait != b.wait ? a.wait > b.wait : (double)a.busy / a.ports > (double)b.busy / b.ports;
    });
    if (rows.size() > 6) rows.resize(6);
    if (rows.empty()) ImGui::TextColored(P.ink3, "Every station is idle at this hour.");
    for (auto &r : rows) {
        int s = sv->sim.st[r.k].station;
        ImGui::PushID(s);
        ImGui::Text("Station %d: %d of %d ports busy", sc.stations[s].id, r.busy, r.ports);
        if (r.wait) { ImGui::SameLine(); ImGui::TextColored(P.amber, "%d waiting", r.wait); }
        if (r.ports < 63) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 70 * S);
            if (ImGui::SmallButton("+2 ports")) {
                Plan &n = edit(app);
                int np = std::min(64, r.ports + 2);
                if (np == E.basePorts(s)) n.edits.ports.erase(s); else n.edits.ports[s] = np;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add 2 ports at station %d and re-run the plan", sc.stations[s].id);
        }
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + 5 * S), col(P.paper3), 3 * S);
        dl->AddRectFilled(p, ImVec2(p.x + bw * std::min(1.f, (float)r.busy / r.ports), p.y + 5 * S), col(r.busy >= r.ports ? P.amber : P.cobalt), 3 * S);
        ImGui::Dummy(ImVec2(bw, 9 * S));
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6 * S));
    ImGui::Separator();
    // day summary, comparison with the other policy and the baseline
    std::string sum = liveSummary(E, *sv, E.sim(1 - app.policy), app.policy);
    ImGui::PushStyleColor(ImGuiCol_Text, P.ink2);
    RichText(sum, app.fonts);
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

/* ======================================================================== strategies */
void drawStrategies(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    Heading("Same city, fifteen strategies", app.fonts,
            "Every pairing of a site-selection method with a routing method, run on identical data at the city's default budget. Darker cells are better for the measure you choose.");
    ImGui::Dummy(ImVec2(0, 4 * S));
    static const char *metrics[] = { "Trips completed", "Energy per trip", "Routing time", "Selection value" };
    Segmented("metric", metrics, 4, &app.metric, P, std::min(ImGui::GetContentRegionAvail().x, 640 * S));
    Compare &C = E.compare(E.plan.city);
    if (!C.done) {
        E.stepCompare(C);
        int total = E.city().sc.nn > 1600 ? 10 : 15;
        ImGui::ProgressBar((float)C.rows.size() / total, ImVec2(360 * S, 0), ("Running strategy " + std::to_string(C.rows.size() + 1) + " of " + std::to_string(total)).c_str());
        if (C.rows.empty()) return;
    }
    auto val = [&](const CompareRow &r) {
        switch (app.metric) {
        case 0: return (double)r.feasible;
        case 1: return r.avg_kwh;
        case 2: return std::log10(std::max(r.route_ms, 1e-3));
        default: return (double)r.value;
        }
    };
    bool higher = app.metric == 0 || app.metric == 3;
    double lo = 1e300, hi = -1e300;
    for (auto &r : C.rows) { lo = std::min(lo, val(r)); hi = std::max(hi, val(r)); }
    double best = higher ? hi : lo;
    int nbest = 0;
    for (auto &r : C.rows) if (val(r) == best) nbest++;
    auto find = [&](int sm, int rm) { return findCompare(C, sm, rm); };
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (ImGui::BeginTable("matrix", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Site selection", ImGuiTableColumnFlags_WidthFixed, 200 * S);
        for (int r = 0; r < EV_ROUTE_COUNT; r++) ImGui::TableSetupColumn(ROUTE_LABEL[r]);
        ImGui::TableHeadersRow();
        for (int k = 0; k < EV_SEL_COUNT; k++) {
            int sm = SEL_ORDER[k];
            ImGui::TableNextRow(0, 58 * S);
            ImGui::TableNextColumn();
            ImGui::PushFont(app.fonts.semi);
            ImGui::TextUnformatted(SELECT_LABEL[sm]);
            ImGui::PopFont();
            for (int rm = 0; rm < EV_ROUTE_COUNT; rm++) {
                ImGui::TableNextColumn();
                const CompareRow *r = find(sm, rm);
                if (!r) {
                    bool pending = !C.done;
                    ImGui::TextColored(P.ink3, "%s", pending ? "running\xE2\x80\xA6" : "\xE2\x80\x93");
                    if (!pending) smallText(app, "not offered at this size", &P.ink3);
                    continue;
                }
                double sc = hi == lo ? 1 : higher ? (val(*r) - lo) / (hi - lo) : (hi - val(*r)) / (hi - lo);
                ImVec4 bg = mix(P.paper, P.cobalt, (float)(0.06 + 0.6 * sc));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, col(bg));
                bool strong = sc > 0.62;
                ImVec4 fg = strong ? P.paper : P.ink;
                std::string v, sub;
                switch (app.metric) {
                case 0: v = fint(r->feasible); sub = f1(r->feasible_pct) + "% of trips"; break;
                case 1: v = f2(r->avg_kwh) + " kWh"; sub = f1(r->avg_km) + " km per trip"; break;
                case 2: v = fms(r->route_ms); sub = fint((double)r->sssp) + " shortest-path trees"; break;
                default: v = fint((double)r->value); sub = std::to_string(r->stations) + " stations" + (r->search_nodes ? ", " + fint((double)r->search_nodes) + " nodes" : ""); break;
                }
                if (nbest == 1 && val(*r) == best) v += "   best";
                ImGui::PushFont(app.fonts.semi);
                ImGui::TextColored(fg, "%s", v.c_str());
                ImGui::PopFont();
                ImGui::PushFont(app.fonts.small);
                ImGui::TextColored(strong ? mix(P.paper, P.cobalt, 0.2f) : P.ink2, "%s", sub.c_str());
                ImGui::PopFont();
            }
        }
        ImGui::EndTable();
    }
    if (!C.done) return;
    std::string fnd = strategiesFinding(C);
    ImGui::Dummy(ImVec2(0, 8 * S));
    RichText(fnd, app.fonts, 0, 760 * S);
}

/* ======================================================================== capacity */
void drawCapacity(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    Run *R = E.run();
    if (!R) return;
    const ev_scenario &sc = E.city().sc;
    const ev_sessions &se = R->ses;
    Heading("Ports and grid under load", app.fonts,
            "Charging sessions from the current plan, hour by hour against grid supply, and station by station against installed ports.");
    ImGui::Dummy(ImVec2(0, 4 * S));
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float w = (ImGui::GetContentRegionAvail().x - gap) / 2;
    float h = 330 * S;

    ImGui::BeginChild("hourly", ImVec2(w, h + 80 * S), ImGuiChildFlags_Borders);
    ImGui::PushFont(app.fonts.semi); ImGui::TextUnformatted("Charging energy by hour"); ImGui::PopFont();
    int n = sc.cfg.horizon_h;
    std::vector<double> xs((size_t)n), under((size_t)n), over((size_t)n), total((size_t)n);
    double grid = sc.cfg.grid_kw, ymax = grid * 1.12;
    for (int k = 0; k < n; k++) {
        xs[(size_t)k] = k + 0.5;
        total[(size_t)k] = se.hourly_kwh[k];
        under[(size_t)k] = std::min(se.hourly_kwh[k], grid);
        over[(size_t)k] = se.hourly_kwh[k];
        ymax = std::max(ymax, se.hourly_kwh[k] * 1.12);
    }
    if (ImPlot::BeginPlot("##hourly", ImVec2(-1, h - 20 * S), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("hour of day", "kWh", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, n, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, ymax, ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, TickFormat);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        ImPlot::SetNextFillStyle(P.amber, 1);
        ImPlot::PlotBars("Above grid supply", xs.data(), over.data(), n, 0.68);
        ImPlot::SetNextFillStyle(P.cobalt, 1);
        ImPlot::PlotBars("Within supply", xs.data(), under.data(), n, 0.68);
        ImPlot::SetNextLineStyle(P.ink, 1.4f);
        ImPlot::PlotInfLines("Grid supply", &grid, 1, ImPlotInfLinesFlags_Horizontal);
        if (ImPlot::IsPlotHovered()) {
            int k = (int)std::floor(ImPlot::GetPlotMousePos().x);
            if (k >= 0 && k < n) ImGui::SetTooltip("%02d:00, %s kWh", k, fint(total[(size_t)k]).c_str());
        }
        ImPlot::EndPlot();
    }
    std::string note = surgeNote(se);
    ImGui::PushFont(app.fonts.small);
    RichText(note, app.fonts);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("busiest", ImVec2(w, h + 80 * S), ImGuiChildFlags_Borders);
    ImGui::PushFont(app.fonts.semi); ImGui::TextUnformatted("Busiest stations"); ImGui::PopFont();
    smallText(app, "Blue: sessions the installed ports can serve. Amber: sessions they cannot.", &P.ink3);
    std::vector<int> order;
    for (int q = 0; q < se.n; q++) if (se.st[q].sessions > 0) order.push_back(q);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return se.st[a].sessions > se.st[b].sessions; });
    if (order.size() > 12) order.resize(12);
    int mx = 1;
    for (int q : order) mx = std::max(mx, se.st[q].sessions);
    if (order.empty()) ImGui::TextColored(P.ink3, "No charging sessions in this plan.");
    for (int q : order) {
        const ev_station_load &L = se.st[q];
        int ports = R->ports[(size_t)L.station];
        ImGui::Text("Station %d", sc.stations[L.station].id);
        ImGui::SameLine(110 * S);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x - 150 * S;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float y = p.y + ImGui::GetTextLineHeight() * 0.25f, hh = ImGui::GetTextLineHeight() * 0.55f;
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + bw, y + hh), col(P.paper2), 3 * S);
        float a = bw * L.served / mx, b = bw * (L.sessions - L.served) / mx;
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + a, y + hh), col(P.cobalt), 3 * S);
        if (b > 0) dl->AddRectFilled(ImVec2(p.x + a, y), ImVec2(p.x + a + b, y + hh), col(P.amber), 3 * S);
        ImGui::Dummy(ImVec2(bw, ImGui::GetTextLineHeight()));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d of %d sessions servable", L.served, L.sessions);
        ImGui::SameLine();
        ImGui::TextColored(L.chargers_needed > ports ? P.red : P.ink2, "needs %d, has %d", L.chargers_needed, ports);
    }
    ImGui::EndChild();

    // max-flow checks
    ImGui::Dummy(ImVec2(0, 4 * S));
    const ev_capacity &c = R->cap;
    float cw = (ImGui::GetContentRegionAvail().x - gap * 3) / 4;
    auto card = [&](const char *id, const char *t, long v, const std::string &sub) {
        ImGui::BeginChild(id, ImVec2(cw, 110 * S), ImGuiChildFlags_Borders);
        ImGui::PushFont(app.fonts.small); ImGui::TextColored(P.ink3, "%s", t); ImGui::PopFont();
        ImGui::PushFont(app.fonts.h1); ImGui::TextUnformatted(fint((double)v).c_str()); ImGui::PopFont();
        ImGui::PushFont(app.fonts.small); ImGui::TextColored(P.ink2, "%s", sub.c_str()); ImGui::PopFont();
        ImGui::EndChild();
    };
    card("ff", "Ford\xE2\x80\x93" "Fulkerson, depth-first", c.flow_dfs, fms(c.ms_dfs) + ", " + fint((double)c.aug_dfs) + " augmenting paths");
    ImGui::SameLine();
    card("ek", "Edmonds\xE2\x80\x93Karp, breadth-first", c.flow_bfs, fms(c.ms_bfs) + ", " + fint((double)c.aug_bfs) + " augmenting paths");
    ImGui::SameLine();
    card("bm", "Bipartite matching to port slots", c.matching, fms(c.ms_match));
    ImGui::SameLine();
    ImGui::BeginChild("agree", ImVec2(cw, 110 * S), ImGuiChildFlags_Borders);
    bool agree = c.flow_dfs == c.flow_bfs && c.flow_bfs == c.matching;
    Pill(agree ? "All three agree" : "Results differ", agree ? P.greenSoft : mix(P.paper, P.red, 0.16f), agree ? P.green : P.red);
    std::string t = flowNote(sc, c);
    ImGui::PushFont(app.fonts.small);
    RichText(t, app.fonts);
    ImGui::PopFont();
    ImGui::EndChild();
}

/* ======================================================================== algorithms */
void drawAlgorithms(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    City &city = E.city();
    Heading("Every decision and the algorithm behind it", app.fonts,
            ("The engine checks each answer against a second, independent algorithm on the same city. This is its report for " + lowerLabel(city) + ".").c_str());
    Analysis &A = E.analysis(E.plan.city);
    static int pending = 0;
    if (!A.done) {
        ImGui::TextColored(P.ink3, "Running the engine's full report\xE2\x80\xA6");
        if (++pending > 1) { E.computeAnalysis(E.plan.city); pending = 0; }
        return;
    }
    std::vector<AlgoRow> rows = algorithmRows(city, E.run(), A);
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (!ImGui::BeginTable("algo", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) return;
    ImGui::TableSetupColumn("Decision", 0, 1.1f);
    ImGui::TableSetupColumn("Algorithm", 0, 1.1f);
    ImGui::TableSetupColumn("Result on this city", 0, 1.9f);
    ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed, 130 * S);
    ImGui::TableHeadersRow();
    for (const AlgoRow &r : rows) {
        ImGui::TableNextRow();
        if (r.family) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col(P.paper3));
            ImGui::TableNextColumn();
            ImGui::PushFont(app.fonts.semi);
            ImGui::TextUnformatted(r.decision.c_str());
            ImGui::PopFont();
            continue;
        }
        ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.semi); ImGui::TextWrapped("%s", r.decision.c_str()); ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::PushStyleColor(ImGuiCol_Text, P.ink2); ImGui::TextWrapped("%s", r.algorithm.c_str()); ImGui::PopStyleColor();
        ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r.result.c_str());
        ImGui::TableNextColumn();
        ImGui::PushFont(app.fonts.small);
        switch (r.check) {
        case CHECK_OK: Pill(checkLabel(r.check), P.greenSoft, P.green); break;
        case CHECK_BAD: Pill(checkLabel(r.check), mix(P.paper, P.red, 0.16f), P.red); break;
        case CHECK_NA: Pill(checkLabel(r.check), P.paper3, P.ink3); break;
        default: Pill(checkLabel(r.check), P.paper3, P.ink2); break;
        }
        ImGui::PopFont();
    }
    ImGui::EndTable();
    ImGui::Dummy(ImVec2(0, 8 * S));
    if (ImGui::CollapsingHeader("Show the engine's own report")) {
        ImGui::PushFont(app.fonts.mono);
        ImGui::TextUnformatted(A.text.c_str());
        ImGui::PopFont();
    }
}

/* ======================================================================== data model */
void drawModel(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    Run *R = E.run();
    if (!R) return;
    City &city = E.city();
    Analysis &A = E.analysis(E.plan.city);
    static int pending = 0;
    if (!A.done && ++pending > 1) { E.computeAnalysis(E.plan.city); pending = 0; }
    int t0 = app.trip >= 0 ? app.trip : (R->samples.empty() ? -1 : R->samples[0].trip);
    const TripDetail *D = t0 >= 0 ? &E.trip(t0) : nullptr;
    std::vector<RecordRow> recs = modelRecords(E, *R, D);
    std::vector<StructureRow> rows = modelStructures(E, *R, A);

    Heading("Records, trees and graphs", app.fonts,
            "How the system represents road nodes, trips, batteries, stations and installation budgets: the typed C records each city loads into, "
            "and the arrays, trees and weighted or capacitated graphs the engine builds from them.");
    ImGui::Dummy(ImVec2(0, 6 * S));
    ImGui::PushFont(app.fonts.h2); ImGui::TextUnformatted("Typed records"); ImGui::PopFont();
    smallText(app, "Each CSV file of a city loads into a typed C record. These are the real values for " + lowerLabel(city) + ".", &P.ink2);
    auto codeText = [&](const std::string &t) { ImGui::PushFont(app.fonts.mono); ImGui::TextColored(P.cobalt, "%s", t.c_str()); ImGui::PopFont(); };
    auto begin = [&](const char *id, std::initializer_list<const char *> heads) {
        if (!ImGui::BeginTable(id, (int)heads.size(), ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) return false;
        for (auto h : heads) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        return true;
    };
    if (begin("records", { "Represents", "C record", "Holds", "In this city", "Example", "Defined in" })) {
        for (auto &x : recs) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.semi); ImGui::TextWrapped("%s", x.what.c_str()); ImGui::PopFont();
            ImGui::TableNextColumn(); codeText(x.type);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", x.holds.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", x.count.c_str());
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextWrapped("%s", x.example.c_str()); ImGui::PopFont();
            ImGui::TableNextColumn(); codeText(x.file);
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 14 * S));
    ImGui::PushFont(app.fonts.h2); ImGui::TextUnformatted("Structures built from them"); ImGui::PopFont();
    smallText(app, "The engine turns those records into arrays, trees and weighted or capacitated graphs. Sizes are for the current plan.", &P.ink2);
    if (begin("structures", { "Structure", "Built from", "Size here", "What it does", "Code" })) {
        for (auto &x : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.semi); ImGui::TextWrapped("%s", x.name.c_str()); ImGui::PopFont();
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", x.from.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", x.size.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", x.what.c_str());
            ImGui::TableNextColumn(); codeText(x.code);
        }
        ImGui::EndTable();
    }
}

/* ======================================================================== benchmarks */
void drawBench(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    Heading("How each algorithm scales", app.fonts,
            "Measured run time against input size on logarithmic axes, from results/bench.csv. A line that bends upward faster is the one that will not survive a bigger city.");
    const auto &rows = E.bench();
    if (rows.empty()) { ImGui::TextColored(P.red, "%s", E.bench_error.c_str()); return; }
    ImGui::Dummy(ImVec2(0, 4 * S));
    {
        std::vector<const char *> labels;
        for (auto &f : FAMILIES) labels.push_back(f.label);
        Chips("families", labels.data(), (int)labels.size(), &app.bench_family, P);
    }
    const Family &F = FAMILIES[(size_t)app.bench_family];
    std::map<std::string, std::vector<std::pair<double, double>>> series;
    for (auto &r : rows) {
        if (std::find(F.fam.begin(), F.fam.end(), r.family) == F.fam.end() || benchSkip(r.dataset) || r.ms <= 0 || r.n <= 0) continue;
        series[r.alg].push_back({ r.n, r.ms });
    }
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float side = 380 * S;
    float w = ImGui::GetContentRegionAvail().x - side - gap;
    ImGui::Dummy(ImVec2(0, 4 * S));
    ImVec2 top = ImGui::GetCursorScreenPos();
    static const unsigned PAL[] = { 0x2C55E8, 0xB97710, 0x2D7D53, 0xC2433A, 0x7A4FC2, 0x15233B, 0x2A9BB0 };
    if (ImPlot::BeginPlot("##bench", ImVec2(w, 440 * S), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("input size n", "time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, TickFormat);
        ImPlot::SetupAxisFormat(ImAxis_Y1, TickFormat);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        int k = 0;
        for (auto &kv : series) {
            auto pts = kv.second;
            std::sort(pts.begin(), pts.end());
            std::vector<double> xs, ys;
            for (auto &p : pts) { xs.push_back(p.first); ys.push_back(p.second); }
            unsigned c = PAL[k % 7];
            ImVec4 cv(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1);
            if (app.dark) cv = mix(cv, ImVec4(1, 1, 1, 1), 0.35f);
            std::string nm = algName(kv.first);
            ImPlot::SetNextLineStyle(cv, 2);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.5f, cv, 0);
            ImPlot::PlotLine(nm.c_str(), xs.data(), ys.data(), (int)xs.size());
            k++;
        }
        ImPlot::EndPlot();
    }
    ImGui::SetCursorScreenPos(ImVec2(top.x + w + gap, top.y));
    ImGui::BeginChild("adv", ImVec2(side, 440 * S), ImGuiChildFlags_None);
    ImGui::PushFont(app.fonts.semi); ImGui::TextUnformatted("Inputs built to break them"); ImGui::PopFont();
    for (auto &nt : adversarialNotes(rows)) {
        ImGui::Dummy(ImVec2(0, 4 * S));
        ImGui::PushFont(app.fonts.semi); ImGui::TextWrapped("%s", nt.first.c_str()); ImGui::PopFont();
        ImGui::PushFont(app.fonts.small); ImGui::PushStyleColor(ImGuiCol_Text, P.ink2); ImGui::TextWrapped("%s", nt.second.c_str()); ImGui::PopStyleColor(); ImGui::PopFont();
    }
    ImGui::EndChild();
}

} // namespace ev33
