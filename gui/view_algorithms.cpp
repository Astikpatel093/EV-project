// view_algorithms.cpp - the algorithms EV33 implements: purpose, input, output, cost and role,
// split into the core workflow and the analysis/teaching report, plus the live cross-checks on the
// current city (ev::analyze, run on request).
#include <algorithm>

#include "content.h"
#include "ui.h"

namespace ev33 {

static void catalogTable(App &app, const char *id, bool core) {
    const Palette &P = app.pal;
    float S = app.scale;
    ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, 6, fl)) return;
    ImGui::TableSetupColumn("Algorithm", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("Purpose", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Input \xE2\x86\x92 output", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Space", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("Role in EV33", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableHeadersRow();
    for (const AlgoInfo &a : algorithmCatalog()) {
        if (a.core != core) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushFont(app.fonts.semi);
        ImGui::TextWrapped("%s", a.name);
        ImGui::PopFont();
        ImGui::PushFont(app.fonts.small);
        ImGui::TextColored(P.ink3, "%s", a.family);
        ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::TextWrapped("%s", a.purpose);
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", a.input);
        ImGui::TextColored(P.ink3, "\xE2\x86\x92");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", a.output);
        ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextWrapped("%s", a.time); ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextWrapped("%s", a.space); ImGui::PopFont();
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", a.role);
        ImGui::PushFont(app.fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, P.ink3);
        ImGui::TextWrapped("%s", a.where);
        ImGui::TextWrapped("Checked by: %s", a.check);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndTable();
    (void)S;
}

void DrawAlgorithms(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    ViewHeader(app, "Algorithms",
               "Every algorithm below is written by hand in the engine (include/ev, src/) and tested against an independent method. The standard library's "
               "sort, priority queue and searches are not used for this work. n = items, V = junctions, E = roads.");
    if (ImGui::CollapsingHeader("Core EV workflow: used every time a plan is computed", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushFont(app.fonts.small);
        ImGui::TextColored(P.ink3, "Load data \xE2\x86\x92 benefit of each site \xE2\x86\x92 placement \xE2\x86\x92 route every trip \xE2\x86\x92 capacity \xE2\x86\x92 charging sessions \xE2\x86\x92 day simulation");
        ImGui::PopFont();
        catalogTable(app, "core", true);
    }
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (ImGui::CollapsingHeader("Analysis report and teaching: run on the city's data, not needed to plan", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushFont(app.fonts.small);
        ImGui::TextColored(P.ink3, "Uses marked illustrative show the algorithm on scenario data; the placement and routing workflow does not depend on them.");
        ImGui::PopFont();
        catalogTable(app, "edu", false);
    }
    ImGui::Dummy(ImVec2(0, 4 * S));
    if (ImGui::CollapsingHeader("Cross-checks on this city", ImGuiTreeNodeFlags_DefaultOpen)) {
        Analysis &A = E.analysis(E.plan.city);
        if (!A.done) {
            ImGui::TextWrapped("Runs every algorithm on \"%s\" and compares each answer with a second, independent method (takes up to a few seconds on the large city).",
                               E.city().label.c_str());
            if (ImGui::Button("Run the analysis on this city")) E.computeAnalysis(E.plan.city);
        } else {
            std::vector<AlgoRow> rows = algorithmRows(E.city(), E.run(), A);
            if (ImGui::BeginTable("checks", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Decision", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("Algorithm", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("Result on this city", ImGuiTableColumnFlags_WidthStretch, 2.2f);
                ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableHeadersRow();
                for (const AlgoRow &r : rows) {
                    ImGui::TableNextRow();
                    if (r.family) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col(P.panel2));
                        ImGui::TableNextColumn();
                        ImGui::PushFont(app.fonts.semi);
                        ImGui::TextUnformatted(r.decision.c_str());
                        ImGui::PopFont();
                        continue;
                    }
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r.decision.c_str());
                    ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", r.algorithm.c_str());
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r.result.c_str());
                    ImGui::TableNextColumn();
                    const char *l = checkLabel(r.check);
                    ImGui::PushFont(app.fonts.smallSemi);
                    if (r.check == CHECK_OK) Tag(l, P.primarySoft, P.primary);
                    else if (r.check == CHECK_BAD) Tag(l, P.errorSoft, P.error);
                    else Tag(l, P.panel2, P.ink2);
                    ImGui::PopFont();
                }
                ImGui::EndTable();
            }
        }
    }
}

} // namespace ev33
