// view_trip.cpp - plan one trip typed in by the user. "Calculate route" calls Engine::planTrip,
// which runs the selected planner from the engine (exact battery-state search or the charging-leg
// planner) on the current city with the chargers of the current plan. Nothing is precomputed.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui.h"

namespace ev33 {

static int nodeById(const Scene &sc, int id) {
    for (int i = 0; i < sc.nn; i++)
        if (sc.nodes[(size_t)i].id == id) return i;
    return -1;
}

void FillTripForm(App &app, int t) {
    const Scene &sc = app.eng.city().sc;
    TripForm &f = app.form;
    f.city = app.eng.plan.city;
    if (t < 0 || t >= sc.nt) return;
    const TripV &tr = sc.trips[(size_t)t];
    const VehicleV &v = sc.vehicles[(size_t)tr.vehicle];
    f.src_id = sc.nodes[(size_t)tr.src].id;
    f.dst_id = sc.nodes[(size_t)tr.dst].id;
    f.vehicle = tr.vehicle;
    f.battery = v.battery_kwh;
    f.consumption = v.consumption;
    f.reserve = v.reserve_kwh;
    f.charge = tr.start_kwh;
    f.depart_h = tr.depart_min / 60 % 24;
    f.depart_m = tr.depart_min % 60;
    f.from_trip = t;
}

static void runQuery(App &app) {
    const Scene &sc = app.eng.city().sc;
    TripForm &f = app.form;
    TripQuery q;
    q.src = nodeById(sc, f.src_id);
    q.dst = nodeById(sc, f.dst_id);
    if (f.vehicle < sc.nveh) {
        const VehicleV &m = sc.vehicles[(size_t)f.vehicle];
        q.vehicle.id = m.id;
        q.vehicle.model = m.model;
        q.vehicle.connector = m.connector;
        q.vehicle.max_charge_kw = m.max_charge_kw;
    } else {
        q.vehicle.id = "custom";
        q.vehicle.model = "Custom vehicle";
        q.vehicle.connector = "CCS2";
    }
    q.vehicle.battery_kwh = f.battery;
    q.vehicle.charge_kwh = std::min(f.charge, f.battery);
    q.vehicle.consumption_kwh_per_km = f.consumption;
    q.vehicle.reserve_kwh = f.reserve;
    q.start_kwh = f.charge;
    q.depart_min = f.depart_h * 60 + f.depart_m;
    q.method = f.method;
    app.answer = app.eng.planTrip(q);
    if (app.answer.error.empty()) app.tripNet.reset();
}

static void label(App &app, const char *text) {
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(app.pal.ink2, "%s", text);
    ImGui::PopFont();
}

static void drawForm(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    const Scene &sc = E.city().sc;
    Run *R = E.run();
    TripForm &f = app.form;
    float w = ImGui::GetContentRegionAvail().x;

    SectionTitle(app, "Trip");
    label(app, "Start from a trip in the dataset (optional)");
    ImGui::SetNextItemWidth(w);
    std::string cur = f.from_trip >= 0 && f.from_trip < sc.nt ? "Trip " + std::to_string(sc.trips[(size_t)f.from_trip].id) : "Typed in";
    if (ImGui::BeginCombo("##fromtrip", cur.c_str())) {
        if (R)
            for (const Sample &s : R->samples) {
                const TripV &tr = sc.trips[(size_t)s.trip];
                std::string l = "Trip " + std::to_string(tr.id) + ": " + sc.nodes[(size_t)tr.src].name + " to " + sc.nodes[(size_t)tr.dst].name + ", " + f1(s.km) +
                                " km, " + (s.kind == 2 ? std::to_string(s.stops) + " stops" : s.kind == 1 ? "1 stop" : s.kind == 0 ? "direct" : "not possible");
                if (ImGui::Selectable(l.c_str(), f.from_trip == s.trip)) { FillTripForm(app, s.trip); app.answer = TripAnswer(); }
            }
        ImGui::EndCombo();
    }
    auto nodeField = [&](const char *lab, const char *id, int &value) {
        label(app, lab);
        ImGui::SetNextItemWidth(w * 0.42f);
        if (ImGui::InputInt(id, &value, 0, 0)) f.from_trip = -1;
        ImGui::SameLine();
        int k = nodeById(sc, value);
        ImGui::AlignTextToFramePadding();
        if (k >= 0) ImGui::TextColored(P.ink2, "%s, %s m", sc.nodes[(size_t)k].name.c_str(), fint(sc.nodes[(size_t)k].elev).c_str());
        else ImGui::TextColored(P.error, "no junction with this id");
    };
    nodeField("Source junction (node id)", "##src", f.src_id);
    nodeField("Destination junction (node id)", "##dst", f.dst_id);
    if (ImGui::SmallButton("Swap source and destination")) { std::swap(f.src_id, f.dst_id); f.from_trip = -1; }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Node ids run from %d to %d in this city.", sc.nodes.front().id, sc.nodes.back().id);
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 4 * S));
    SectionTitle(app, "Vehicle");
    label(app, "EV model");
    ImGui::SetNextItemWidth(w);
    std::string vm = f.vehicle < sc.nveh ? sc.vehicles[(size_t)f.vehicle].id + "  " + sc.vehicles[(size_t)f.vehicle].model : "Custom vehicle";
    if (ImGui::BeginCombo("##ev", vm.c_str())) {
        for (int i = 0; i <= sc.nveh; i++) {
            std::string l = i < sc.nveh ? sc.vehicles[(size_t)i].id + "  " + sc.vehicles[(size_t)i].model : "Custom vehicle";
            if (ImGui::Selectable(l.c_str(), f.vehicle == i)) {
                f.vehicle = i;
                if (i < sc.nveh) {
                    const VehicleV &v = sc.vehicles[(size_t)i];
                    f.battery = v.battery_kwh;
                    f.consumption = v.consumption;
                    f.reserve = v.reserve_kwh;
                    f.charge = std::min(f.charge, f.battery);
                }
                f.from_trip = -1;
            }
        }
        ImGui::EndCombo();
    }
    if (f.vehicle < sc.nveh) {
        const VehicleV &v = sc.vehicles[(size_t)f.vehicle];
        ImGui::PushFont(app.fonts.small);
        ImGui::TextColored(P.ink3, "Connector %s%s", v.connector.empty() ? "not recorded" : v.connector.c_str(),
                           v.max_charge_kw > 0 ? (", charges at up to " + fint(v.max_charge_kw) + " kW").c_str() : "");
        ImGui::PopFont();
    }
    auto num = [&](const char *lab, const char *id, double &value, const char *fmt, const char *unit) {
        label(app, lab);
        ImGui::SetNextItemWidth(w * 0.55f);
        if (ImGui::InputDouble(id, &value, 0, 0, fmt)) { if (f.vehicle < sc.nveh && std::string(id) != "##charge") f.vehicle = sc.nveh; f.from_trip = -1; }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(P.ink3, "%s", unit);
    };
    num("Battery capacity", "##cap", f.battery, "%.2f", "kWh");
    num("Initial battery charge", "##charge", f.charge, "%.2f", f.battery > 0 ? ("kWh  (" + fint(f.charge / f.battery * 100) + "%)").c_str() : "kWh");
    num("Energy consumption on flat road", "##cons", f.consumption, "%.3f", "kWh per km");
    num("Minimum battery reserve", "##res", f.reserve, "%.2f", "kWh, never planned below");
    if (f.consumption > 0 && f.charge > f.reserve) {
        ImGui::PushFont(app.fonts.small);
        ImGui::TextColored(P.ink3, "About %s km on flat road before reaching the reserve.", fint((f.charge - f.reserve) / f.consumption).c_str());
        ImGui::PopFont();
    }
    label(app, "Departure time (hh:mm)");
    ImGui::SetNextItemWidth(w * 0.25f);
    ImGui::InputInt("##dh", &f.depart_h, 0, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted(":");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.25f);
    ImGui::InputInt("##dm", &f.depart_m, 0, 0);
    f.depart_h = std::max(0, std::min(23, f.depart_h));
    f.depart_m = std::max(0, std::min(59, f.depart_m));

    ImGui::Dummy(ImVec2(0, 4 * S));
    SectionTitle(app, "Routing algorithm");
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("##method", PLANNER_LABEL[f.method])) {
        for (int m = 0; m < PLANNER_COUNT; m++) {
            bool off = m == PLANNER_LEG_FLOYD && sc.nn > FLOYD_MAX_NODES;
            ImGui::BeginDisabled(off);
            if (ImGui::Selectable(PLANNER_LABEL[m], f.method == m)) f.method = m;
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::PushStyleColor(ImGuiCol_Text, P.ink3);
    ImGui::TextWrapped("%s", PLANNER_HINT[f.method]);
    if (R) {
        int avail = 0;
        for (int i = 0; i < sc.nexist; i++) avail += sc.stations[(size_t)i].available;
        ImGui::TextWrapped("Charging is possible at the %d available existing stations and the %d new stations of the current placement, where the connector matches.",
                           avail, R->run.sel.n);
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4 * S));
    ImGui::PushStyleColor(ImGuiCol_Button, P.primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(P.primary, P.ink, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(P.primary, P.ink, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_Text, P.panel);
    if (ImGui::Button("Calculate route", ImVec2(w, 34 * S))) runQuery(app);
    ImGui::PopStyleColor(4);
}

static void drawAnswer(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    const Scene &sc = E.city().sc;
    const TripAnswer &a = app.answer;
    if (!a.done || a.city != E.plan.city) {
        BeginPanel(app, "empty", ImVec2(0, 0));
        SectionTitle(app, "No route calculated yet");
        ImGui::TextWrapped("Choose two junctions and a vehicle, then press Calculate route. The result shows the route, the charging stops, the battery level along the "
                           "way and, when the trip is not possible, the reason.");
        EndPanel();
        return;
    }
    if (!a.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, P.errorSoft);
        ImGui::BeginChild("err", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(P.error, "Nothing was planned.");
        ImGui::TextWrapped("%s", a.error.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }
    const ev::TripPlan &p = a.plan;
    const ev::ElectricVehicle &veh = a.query.vehicle;
    bool ok = p.feasible();
    // status line
    ImGui::PushFont(app.fonts.smallSemi);
    if (ok) Tag(p.stops.empty() ? "FEASIBLE, NO CHARGING NEEDED" : "FEASIBLE WITH CHARGING", P.primarySoft, P.primary);
    else Tag("NOT FEASIBLE", P.errorSoft, P.error);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushFont(app.fonts.mono);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.ink2, "status = %s", ev::to_string(p.status));
    ImGui::PopFont();
    std::string how = std::string(PLANNER_LABEL[a.query.method]) + ", " + fms(a.ms) + (a.states ? ", " + fint((double)a.states) + " states settled" : std::string());
    ImGui::PushFont(app.fonts.small);
    // same line when it fits, otherwise below the status
    float need = ImGui::CalcTextSize(how.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < need) ImGui::NewLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.ink3, "%s", how.c_str());
    ImGui::PopFont();
    ImGui::TextWrapped("%s", p.explanation.c_str());

    // figures
    float gap = 8 * S, avail = ImGui::GetContentRegionAvail().x;
    int cols = avail > 760 * S ? 6 : 3;
    float mw = (avail - gap * (float)(cols - 1)) / (float)cols, mh = 78 * S;
    struct Fig { const char *label; std::string value, note; };
    std::vector<Fig> figs;
    if (ok) {
        figs = { { "Total distance", f1(p.distance_km) + " km", "shortest road route " + f1(p.shortest_km) + " km" },
                 { "Energy used", f2(p.energy_kwh) + " kWh", "for this vehicle on this route" },
                 { "Driving time", fdur(p.drive_min), "from road travel times" },
                 { "Charging", p.stops.empty() ? std::string("none") : fdur(p.charge_min), fint((double)p.stops.size()) + (p.stops.size() == 1 ? " stop" : " stops") },
                 { "Arrival", fclock(p.arrive_min), "departs " + fclock(p.depart_min) },
                 { "Battery on arrival", f2(p.arrive_kwh) + " kWh", "lowest " + f2(p.min_kwh) + " kWh, reserve " + f2(veh.reserve_kwh) } };
    } else {
        figs = { { "Shortest road route", p.shortest_km >= 0 ? f1(p.shortest_km) + " km" : std::string("none"), p.shortest_km >= 0 ? "ignoring the battery" : "no road connects them" },
                 { "Energy it needs", p.shortest_km >= 0 ? f2(p.shortest_kwh) + " kWh" : std::string("\xE2\x80\x93"), "for this vehicle" },
                 { "Usable at departure", f2(a.query.start_kwh - veh.reserve_kwh) + " kWh", "start charge minus reserve" } };
    }
    for (size_t i = 0; i < figs.size(); i++) {
        if (i % (size_t)cols) ImGui::SameLine(0, gap);
        std::string id = "fig" + std::to_string(i);
        Metric(app, id.c_str(), figs[i].label, figs[i].value, figs[i].note, mw, mh);
    }
    ImGui::Dummy(ImVec2(0, 4 * S));

    // battery chart and map side by side
    avail = ImGui::GetContentRegionAvail().x;
    float half = (avail - gap) / 2;
    BeginPanel(app, "batt", ImVec2(half, 0));
    SectionTitle(app, "Battery along the route", "Green: battery. Red: reserve. Dashed: charge target at a stop. Shaded: elevation profile.");
    BatteryChart(app, "battery", ImVec2(ImGui::GetContentRegionAvail().x, 250 * S), a.profile, veh.battery_kwh, veh.reserve_kwh,
                 sc.cfg.target_frac * veh.battery_kwh);
    EndPanel();
    ImGui::SameLine(0, gap);
    BeginPanel(app, "tripmap", ImVec2(half, 0));
    SectionTitle(app, ok ? "Route on the map" : "Shortest road route (not drivable)");
    NetLayers L;
    L.terrain = true;
    L.junctions = false;
    L.candidates = false;
    DrawNetwork(app, app.tripNet, "##tripnet", ImVec2(ImGui::GetContentRegionAvail().x, 250 * S), NET_PLAN, L, &a.path, ok ? &a.stop_station : nullptr,
                nullptr, nullptr);
    EndPanel();

    // stops
    if (ok && !p.stops.empty()) {
        ImGui::Dummy(ImVec2(0, 4 * S));
        SectionTitle(app, "Charging stops");
        if (ImGui::BeginTable("stops", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            for (const char *h : { "#", "Station", "Junction", "Arrive", "Arrive with", "Charge", "Leave with" }) ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (size_t k = 0; k < p.stops.size(); k++) {
                const ev::ChargingStop &st = p.stops[k];
                const SiteV &s = sc.stations[(size_t)a.stop_station[k]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%d", (int)k + 1);
                ImGui::TableNextColumn(); ImGui::Text("%d %s (%s)", s.id, s.name.c_str(), s.existing ? "existing" : "new");
                ImGui::TableNextColumn(); ImGui::TextUnformatted(sc.nodes[(size_t)st.node].name.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(fclock(st.arrive_min).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%s kWh", f2(st.arrive_kwh).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%s at %s kW", fdur(st.charge_min).c_str(), fint(s.power_kw).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%s kWh", f2(st.depart_kwh).c_str());
            }
            ImGui::EndTable();
        }
    }

    // route sequence
    ImGui::Dummy(ImVec2(0, 4 * S));
    SectionTitle(app, "Route sequence", ok ? "Junctions in driving order; charging stops are marked." : "Junctions of the plain shortest road route.");
    std::string seq;
    size_t next = 0;
    for (size_t i = 0; i < a.path.size(); i++) {
        if (i) seq += "  \xE2\x86\x92  ";
        seq += sc.nodes[(size_t)a.path[i]].name;
        if (ok && next < p.stops.size() && p.stops[next].route_pos == i) { seq += " **[charge " + std::to_string(next + 1) + "]**"; next++; }
        if (seq.size() > 6000) { seq += "  \xE2\x80\xA6 (" + fint((double)(a.path.size() - i - 1)) + " more)"; break; }
    }
    if (a.path.size() <= 1) seq = ok ? "Origin and destination are the same junction." : "No road path exists between these junctions.";
    RichText(seq, app.fonts, col(P.ink2));
}

void DrawTripPlanner(App &app) {
    Engine &E = app.eng;
    float S = app.scale;
    const Scene &sc = E.city().sc;
    if (app.form.city != E.plan.city) {
        Run *R = E.run();
        FillTripForm(app, R && !R->samples.empty() ? R->samples[0].trip : 0);
        app.form.method = app.default_method;
        app.answer = TripAnswer();
    }
    ViewHeader(app, "Trip Planner",
               "Plan one trip on the current city. The route keeps the battery above the vehicle's reserve and adds charging stops where needed; if that is "
               "impossible, the reason is shown.");
    SyntheticTag(app, sc);
    ImGui::Dummy(ImVec2(0, 2 * S));
    float formW = 340 * S;
    BeginPanel(app, "form", ImVec2(formW, ImGui::GetContentRegionAvail().y), true);
    drawForm(app);
    EndPanel();
    ImGui::SameLine(0, 12 * S);
    ImGui::BeginChild("answer", ImVec2(0, 0), ImGuiChildFlags_None);
    drawAnswer(app);
    ImGui::EndChild();
}

} // namespace ev33
