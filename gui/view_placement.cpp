// view_placement.cpp - choose which candidate sites to build within the installation budget.
//
// The plan (method, budget, what-if edits) is applied by the engine on the next frame; the engine
// runs ev::place_stations (0/1 knapsack DP, branch and bound or greedy) and then plans every trip
// with the new charger sites. This view only shows the result and collects the choices.
#include <algorithm>
#include <cmath>

#include "content.h"
#include "ev/algorithms/sorting.hpp"
#include "ui.h"

namespace ev33 {

static void controls(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    City &C = E.city();
    const Scene &sc = C.sc;
    const Plan &cur = app.has_next ? app.next : E.plan;
    BeginPanel(app, "controls", ImVec2(0, 0));
    float w = ImGui::GetContentRegionAvail().x;
    float colw = std::min(360 * S, (w - 20 * S) / 2);
    ImGui::BeginGroup();
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "Placement algorithm");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(colw);
    if (ImGui::BeginCombo("##method", SELECT_LONG[cur.sel])) {
        for (int k = 0; k < SEL_COUNT; k++) {
            int m = SEL_ORDER[k];
            if (ImGui::Selectable(SELECT_LONG[m], m == cur.sel)) app.edit().sel = m;
        }
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 20 * S);
    ImGui::BeginGroup();
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "Installation budget (lakh rupees)");
    ImGui::PopFont();
    double total = 0;
    for (int i = sc.nexist; i < sc.ns; i++) total += sc.stations[(size_t)i].cost;
    int maxb = std::max(C.default_budget * 2, (int)std::ceil(total));
    int b = cur.budget;
    ImGui::SetNextItemWidth(colw - 120 * S);
    if (ImGui::SliderInt("##budget", &b, 0, maxb, "", ImGuiSliderFlags_AlwaysClamp)) app.edit().budget = b;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(112 * S);
    int typed = cur.budget;
    if (ImGui::InputInt("##budgetin", &typed, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) app.edit().budget = std::max(0, std::min(10000000, typed));
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Dataset budget %s; all %d candidates together cost %s.", rupees(C.default_budget).c_str(), sc.candidates(), rupees(total).c_str());
    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 2 * S));
    if (ImGui::Button("Reset to the dataset budget")) app.edit().budget = C.default_budget;
    ImGui::SameLine();
    ImGui::BeginDisabled(cur.edits.empty());
    std::string ce = "Clear what-if edits (" + std::to_string(cur.edits.count()) + ")";
    if (ImGui::Button(ce.c_str())) app.edit().edits = Edits();
    ImGui::EndDisabled();
    EndPanel();
}

static void summary(App &app, Run &R) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    const Scene &sc = E.city().sc;
    const SelV &sel = R.run.sel;
    double budget = E.plan.budget, left = budget - sel.cost;
    float gap = 8 * S, avail = ImGui::GetContentRegionAvail().x;
    int cols = avail > 900 * S ? 6 : 3;
    float w = (avail - gap * (float)(cols - 1)) / (float)cols, h = 88 * S;
    struct Fig { const char *l; std::string v, n; const ImVec4 *accent; };
    std::string opt = sel.optimal ? "proven optimal" : "heuristic";
    Fig figs[] = {
        { "Installation budget", rupees(budget), "hard limit, never exceeded", nullptr },
        { "Selected stations", fint(sel.n) + " of " + fint(sc.candidates()), "candidate sites to build", &P.primary },
        { "Total installation cost", rupees(sel.cost), fint(sel.cost / std::max(1e-9, budget) * 100) + "% of the budget", nullptr },
        { "Remaining budget", rupees(left), sel.over_budget ? "locked sites exceed the budget" : "not spent", sel.over_budget ? &P.error : nullptr },
        { "Total benefit", fint(sel.value), "sum of the selected sites' scores", nullptr },
        { "Search", fms(sel.ms), opt + (sel.search_nodes ? ", " + fint((double)sel.search_nodes) + " B&B nodes" : std::string()), nullptr },
    };
    for (int i = 0; i < 6; i++) {
        if (i % cols) ImGui::SameLine(0, gap);
        std::string id = "pf" + std::to_string(i);
        Metric(app, id.c_str(), figs[i].l, figs[i].v, figs[i].n, w, h, figs[i].accent);
    }
}

static void explanation(App &app, Run &R) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    const Scene &sc = E.city().sc;
    const SelV &sel = R.run.sel;
    BeginPanel(app, "explain", ImVec2(0, 0));
    SectionTitle(app, "Why these sites");
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
        t += "Your edits are respected: " + fint((double)E.plan.edits.lock.size()) + " site(s) forced in (paid first), " + fint((double)E.plan.edits.ban.size()) +
             " ruled out, " + fint((double)E.plan.edits.ports.size()) + " port change(s). ";
    bool estimated = false;
    for (int i = sc.nexist; i < sc.ns; i++) estimated = estimated || sc.stations[(size_t)i].estimated;
    t += estimated ? "Benefit scores were estimated from the trips: each trip adds 1 to every junction on its shortest route (4 if the vehicle cannot drive that route on its starting charge)."
                   : "Benefit scores come from the dataset's candidates file.";
    RichText(t, app.fonts);
    ImGui::Dummy(ImVec2(0, 2));
    std::string limit = "**Limits of this model.** The knapsack adds up benefit scores, so two neighbouring sites that serve the same drivers are both credited; it does not "
                        "solve the full geographic facility-location (coverage) problem. The trip planner re-checks every placement: with this one, **" +
                        f1(R.run.kpi.feasible_pct) + "%** of trips can be completed.";
    RichText(limit, app.fonts, col(P.ink2));
    EndPanel();
}

static void candidatesTable(App &app, Run &R) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    const Scene &sc = E.city().sc;
    static const char *const F[] = { "All", "Selected", "Not selected" };
    Segmented("candfilter", F, 3, &app.cand_filter, P, 330 * S);
    ImGui::SameLine();
    ImGui::PushFont(app.fonts.small);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(P.ink3, "Click a column header to sort (merge sort). Decision: Auto = the algorithm decides.");
    ImGui::PopFont();
    std::vector<int> rows;
    for (int i = sc.nexist; i < sc.ns; i++) {
        bool built = R.built[(size_t)i] != 0;
        if (app.cand_filter == 1 && !built) continue;
        if (app.cand_filter == 2 && built) continue;
        rows.push_back(i);
    }
    ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Sortable |
                         ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("cands", 9, fl)) {
        ImGui::TableSetupColumn("Selected", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Junction", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Cost");
        ImGui::TableSetupColumn("Benefit");
        ImGui::TableSetupColumn("Benefit per lakh");
        ImGui::TableSetupColumn("Ports", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Decision", ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs *ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs &sp = ss->Specs[0];
                auto key = [&](int i) -> double {
                    const SiteV &s = sc.stations[(size_t)i];
                    switch (sp.ColumnIndex) {
                    case 0: return R.built[(size_t)i];
                    case 1: return s.id;
                    case 4: return s.cost;
                    case 5: return (double)s.value;
                    case 6: return s.value / std::max(1e-9, s.cost);
                    default: return 0;
                    }
                };
                bool desc = sp.SortDirection == ImGuiSortDirection_Descending;
                if (sp.ColumnIndex == 2)
                    ev::merge_sort(rows, [&](int a, int b) { return desc ? sc.stations[(size_t)b].name < sc.stations[(size_t)a].name : sc.stations[(size_t)a].name < sc.stations[(size_t)b].name; });
                else
                    ev::merge_sort(rows, [&](int a, int b) { return desc ? key(b) < key(a) : key(a) < key(b); });
            }
        }
        for (int i : rows) {
            const SiteV &s = sc.stations[(size_t)i];
            bool built = R.built[(size_t)i] != 0;
            int mode = E.plan.edits.lock.count(i) ? 1 : E.plan.edits.ban.count(i) ? 2 : 0;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (built) {
                ImVec2 c = ImGui::GetCursorScreenPos();
                float r = 5 * S, h = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddQuadFilled(ImVec2(c.x + r, c.y + h / 2 - r), ImVec2(c.x + 2 * r, c.y + h / 2), ImVec2(c.x + r, c.y + h / 2 + r),
                                                          ImVec2(c.x, c.y + h / 2), col(P.primary));
                ImGui::Dummy(ImVec2(2 * r, h));
                ImGui::SameLine();
                ImGui::TextColored(P.primary, "built");
            } else ImGui::TextColored(P.ink3, "\xE2\x80\x93");
            ImGui::TableNextColumn(); ImGui::Text("%d", s.id);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(sc.nodes[(size_t)s.node].name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rupees(s.cost).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s%s", fint((double)s.value).c_str(), s.estimated ? " (est.)" : "");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(f2(s.value / std::max(1e-9, s.cost)).c_str());
            ImGui::TableNextColumn();
            int ports = E.effectivePorts(i);
            if (ImGui::SmallButton("\xE2\x88\x92") && ports > 1) { Plan &n = app.edit(); if (ports - 1 == E.basePorts(i)) n.edits.ports.erase(i); else n.edits.ports[i] = ports - 1; }
            ImGui::SameLine();
            ImGui::Text("%d", ports);
            ImGui::SameLine();
            if (ImGui::SmallButton("+") && ports < 64) { Plan &n = app.edit(); if (ports + 1 == E.basePorts(i)) n.edits.ports.erase(i); else n.edits.ports[i] = ports + 1; }
            ImGui::TableNextColumn();
            static const char *const M[] = { "Auto", "Always", "Never" };
            for (int m = 0; m < 3; m++) {
                if (m) ImGui::SameLine(0, 2 * S);
                bool on = m == mode;
                ImGui::PushStyleColor(ImGuiCol_Button, on ? (m == 2 ? P.error : P.primary) : P.panel2);
                ImGui::PushStyleColor(ImGuiCol_Text, on ? P.panel : P.ink);
                if (ImGui::SmallButton(M[m]) && !on) {
                    Plan &n = app.edit();
                    n.edits.lock.erase(i);
                    n.edits.ban.erase(i);
                    if (m == 1) n.edits.lock.insert(i);
                    if (m == 2) n.edits.ban.insert(i);
                }
                ImGui::PopStyleColor(2);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void existingTable(App &app, Run &R) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    const Scene &sc = E.city().sc;
    if (sc.nexist == 0) {
        ImGui::TextColored(P.ink2, "This dataset has no existing charging stations.");
        return;
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Existing stations are always part of the network and are not part of the budget. Offline or maintenance stations cannot be used for charging.");
    ImGui::PopFont();
    std::vector<int> sessions((size_t)sc.ns, 0);
    for (const LoadV &L : R.ses.st) sessions[(size_t)L.station] = L.sessions;
    if (ImGui::BeginTable("exist", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        for (const char *h : { "Id", "Name", "Junction", "Status", "Connector", "Power", "Ports", "Planned sessions" }) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int i = 0; i < sc.nexist; i++) {
            const SiteV &s = sc.stations[(size_t)i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", s.id);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(sc.nodes[(size_t)s.node].name.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(s.available ? P.ink : P.error, "%s", s.status.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.connector.empty() ? "not recorded" : s.connector.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s kW", fint(s.power_kw).c_str());
            ImGui::TableNextColumn();
            int ports = E.effectivePorts(i);
            if (ImGui::SmallButton("\xE2\x88\x92") && ports > 1) { Plan &n = app.edit(); if (ports - 1 == E.basePorts(i)) n.edits.ports.erase(i); else n.edits.ports[i] = ports - 1; }
            ImGui::SameLine();
            ImGui::Text("%d", ports);
            ImGui::SameLine();
            if (ImGui::SmallButton("+") && ports < 64) { Plan &n = app.edit(); if (ports + 1 == E.basePorts(i)) n.edits.ports.erase(i); else n.edits.ports[i] = ports + 1; }
            ImGui::TableNextColumn(); ImGui::Text("%d", sessions[(size_t)i]);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void compareTable(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    Compare &C = E.compare(E.plan.city);
    if (!C.done) E.stepCompare(C);   // one full run per frame keeps the window responsive
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Every placement method with every routing method on the same data at the dataset's budget (%s).",
                       C.done ? "complete" : ("computing, " + std::to_string(C.rows.size()) + " runs so far").c_str());
    ImGui::PopFont();
    if (ImGui::BeginTable("cmp", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        for (const char *h : { "Placement", "Routing", "Stations", "Cost", "Benefit", "Optimal", "Placement time", "Trips completed", "Routing time" })
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const CompareRow &r : C.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(SELECT_LABEL[r.sel]);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ROUTE_LABEL[r.route]);
            ImGui::TableNextColumn(); ImGui::Text("%d", r.stations);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rupees(r.cost).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(fint(r.value).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(r.optimal ? P.primary : P.warn, "%s", r.optimal ? "yes" : "no");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(fms(r.select_ms).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s (%s%%)", fint(r.feasible).c_str(), f1(r.feasible_pct).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(fms(r.route_ms).c_str());
        }
        ImGui::EndTable();
    }
    if (C.done) {
        ImGui::Dummy(ImVec2(0, 4));
        RichText(strategiesFinding(C), app.fonts, col(P.ink2));
    }
}

void DrawPlacement(App &app) {
    Engine &E = app.eng;
    float S = app.scale;
    ViewHeader(app, "Station Placement",
               "Choose which candidate sites to build within the installation budget. The 0/1 knapsack treats each site as an item: weight = installation "
               "cost, value = benefit score.");
    SyntheticTag(app, E.city().sc);
    ImGui::Dummy(ImVec2(0, 2 * S));
    controls(app);
    Run *R = E.run();
    if (!R) { ImGui::TextColored(app.pal.error, "%s", E.status.c_str()); return; }
    ImGui::Dummy(ImVec2(0, 4 * S));
    summary(app, *R);
    ImGui::Dummy(ImVec2(0, 4 * S));
    explanation(app, *R);
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (ImGui::BeginTabBar("placetabs")) {
        if (ImGui::BeginTabItem("Candidate sites")) { candidatesTable(app, *R); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Existing stations")) { existingTable(app, *R); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Compare methods")) { compareTable(app); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}

} // namespace ev33
