// view_data.cpp - inspect the loaded dataset, check another dataset folder and load it.
//
// Checking calls checkDataset (engine), which runs the same loader as every other program:
// ev::load_scenario either returns a fully validated scenario or throws "file:line: reason".
// Nothing is repaired or replaced silently, and the current dataset stays loaded if a check fails.
#include <algorithm>
#include <cstring>
#include <filesystem>

#include "ev/algorithms/sorting.hpp"
#include "ui.h"

namespace ev33 {

namespace fs = std::filesystem;

static std::vector<std::string> findDatasets(App &app) {
    std::vector<std::string> out;
    std::vector<std::string> roots = { app.eng.data_dir, app.eng.data_dir + "/..", "data", "data/scenarios" };
    for (const std::string &r : roots) {
        std::error_code ec;
        if (!fs::is_directory(r, ec)) continue;
        for (const auto &e : fs::directory_iterator(r, ec)) {
            if (!e.is_directory(ec)) continue;
            if (!fs::exists(e.path() / "scenario.cfg", ec)) continue;
            std::string p = fs::weakly_canonical(e.path(), ec).generic_string();
            if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
        }
    }
    ev::merge_sort(out, [](const std::string &a, const std::string &b) { return a < b; });
    return out;
}

static void checkResult(App &app) {
    const Palette &P = app.pal;
    const DatasetCheck &d = app.check;
    if (!app.has_check) return;
    if (!d.ok) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, P.errorSoft);
        ImGui::BeginChild("bad", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(P.error, "The dataset was rejected. Nothing was loaded; the current dataset is unchanged.");
        ImGui::PushFont(app.fonts.mono);
        ImGui::TextWrapped("%s", d.error.c_str());
        ImGui::PopFont();
        ImGui::TextWrapped("The message names the file and line (file:line: reason). Fix that record and check again.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }
    BeginPanel(app, "good", ImVec2(0, 0));
    ImGui::TextColored(P.primary, "Valid: every file and every cross-reference passed.");
    ImGui::PushFont(app.fonts.smallSemi);
    if (d.source == "synthetic") Tag("SYNTHETIC DATASET", P.warnSoft, P.warn);
    else if (d.source == "real") Tag("REAL-WORLD DATA (as declared)", P.secondarySoft, P.secondary);
    else Tag("MIXED DATA", P.warnSoft, P.warn);
    ImGui::PopFont();
    if (!d.source_note.empty()) ImGui::TextColored(P.ink2, "Source: %s", d.source_note.c_str());
    if (ImGui::BeginTable("sum", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [&](const char *k, const std::string &v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", k);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(v.c_str());
        };
        row("Name", d.name);
        row("Road nodes / roads", fint(d.nodes) + " / " + fint(d.roads) + (d.components > 1 ? "  (" + fint(d.components) + " separate parts)" : ""));
        row("Existing stations", fint(d.stations) + (d.offline ? " (" + fint(d.offline) + " offline or in maintenance)" : ""));
        row("Candidate sites", fint(d.candidates));
        row("Vehicles / trips", fint(d.vehicles) + " / " + fint(d.trips));
        row("Booked sessions", fint(d.sessions));
        row("Budget", rupees(d.budget_lakh));
        ImGui::EndTable();
    }
    EndPanel();
}

void DrawDataManagement(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    City &C = E.city();
    const Scene &sc = C.sc;
    ViewHeader(app, "Data Management",
               "Datasets are folders of CSV files (nodes, roads, stations, candidates, vehicles, trips). Loading validates every record; a bad file is "
               "reported with its line and is never repaired silently.");

    float gap = 12 * S, avail = ImGui::GetContentRegionAvail().x, half = (avail - gap) / 2;
    BeginPanel(app, "current", ImVec2(half, 0));
    SectionTitle(app, "Loaded dataset");
    ImGui::Text("%s", C.label.c_str());
    SyntheticTag(app, sc);
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "Folder: %s", (C.dir.empty() ? E.data_dir + "/" + C.name : C.dir).c_str());
    if (!sc.cfg.source_note.empty()) ImGui::TextColored(P.ink3, "Source: %s", sc.cfg.source_note.c_str());
    ImGui::PopFont();
    if (ImGui::BeginTable("files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Holds");
        ImGui::TableSetupColumn("Records");
        ImGui::TableHeadersRow();
        auto row = [&](const char *f, const char *h, const std::string &n) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextUnformatted(f); ImGui::PopFont();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", h);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(n.c_str());
        };
        row("nodes.csv", "road nodes (junctions)", fint(sc.nn));
        row("roads.csv", "road edges", fint(sc.ne));
        row("stations.csv", "existing charging stations", fint(sc.nexist));
        row("candidates.csv", "candidate locations", fint(sc.candidates()));
        row("vehicles.csv", "electric vehicles", fint(sc.nveh));
        row("trips.csv", "trips", fint(sc.nt));
        row("sessions.csv", "booked charging sessions", fint((double)sc.sessions));
        row("scenario.cfg", "budget and model settings", "1");
        ImGui::EndTable();
    }
    EndPanel();
    ImGui::SameLine(0, gap);
    BeginPanel(app, "open", ImVec2(half, 0));
    SectionTitle(app, "Check or load another dataset", "Type a folder path (relative to where the program was started, or absolute), or pick one found on disk.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##path", "e.g. data/sample  or  C:/Users/you/my_city", app.data_path, sizeof app.data_path);
    std::string path = app.data_path;
    while (!path.empty() && (path.back() == '/' || path.back() == '\\' || path.back() == ' ')) path.pop_back();
    ImGui::BeginDisabled(path.empty());
    if (ImGui::Button("Check")) { app.check = checkDataset(path); app.has_check = true; }
    ImGui::SameLine();
    if (ImGui::Button("Load as a new city")) {
        app.check = checkDataset(path);
        app.has_check = true;
        if (app.check.ok) {
            std::string err;
            std::string label = fs::path(path).filename().string();
            if (label.empty()) label = path;
            int k = E.addCity(path, label + " (loaded)", err);
            if (k >= 0) { app.pending_city = k; app.notify("Loaded " + path + " and switched to it.", ImGui::GetTime()); }
            else app.notify("Could not load: " + err, ImGui::GetTime(), true);
        }
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 2 * S));
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink2, "Dataset folders found:");
    ImGui::PopFont();
    static std::vector<std::string> found;
    static double scanned = -1;
    if (scanned < 0 || ImGui::GetTime() - scanned > 5) { found = findDatasets(app); scanned = ImGui::GetTime(); }
    for (const std::string &f : found) {
        bool sample = f.find("sample") != std::string::npos;
        std::string l = f + (sample ? "   (hand-made sample)" : "");
        if (ImGui::Selectable(l.c_str(), path == f)) {
            std::strncpy(app.data_path, f.c_str(), sizeof app.data_path - 1);
            app.data_path[sizeof app.data_path - 1] = 0;
            app.check = checkDataset(f);
            app.has_check = true;
        }
    }
    ImGui::Dummy(ImVec2(0, 4 * S));
    checkResult(app);
    EndPanel();

    ImGui::Dummy(ImVec2(0, 6 * S));
    BeginPanel(app, "rules", ImVec2(0, 0));
    SectionTitle(app, "What is checked when a dataset loads");
    static const char *const RULES[][2] = {
        { "Missing files", "scenario.cfg, nodes.csv and roads.csv are required; an empty file is an error" },
        { "Missing columns", "every required column must be in the header; unknown or duplicate columns are errors" },
        { "Malformed records", "wrong number of fields, empty required values, text where a number belongs" },
        { "Duplicate identifiers", "node, road, station and candidate ids must be unique (stations and candidates share one id space)" },
        { "Negative or impossible distances", "distance must be positive, finite and at most 100,000 km; no road may join a node to itself" },
        { "Invalid installation costs", "amounts in rupees with at most 2 decimals; cost > 0; budget >= 0" },
        { "Invalid battery specifications", "capacity > 0, consumption > 0, 0 <= reserve < capacity, charge <= capacity" },
        { "Missing road-node references", "roads, stations, candidates and trips must name existing nodes; trips must name existing vehicles" },
        { "Coordinates", "latitude in [-90, 90], longitude in [-180, 180], both or neither" },
    };
    if (ImGui::BeginTable("rules", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Rule", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableHeadersRow();
        for (auto &r : RULES) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r[0]);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", r[1]);
        }
        ImGui::EndTable();
    }
    ImGui::PushFont(app.fonts.small);
    ImGui::TextColored(P.ink3, "File formats, the synthetic-data labels and how to prepare real data (for example from OpenStreetMap) are described in docs/data.md.");
    ImGui::TextColored(P.ink3, "A dataset declares its origin with data_source = synthetic | real | mixed in scenario.cfg; the app shows that label wherever its numbers appear.");
    ImGui::PopFont();
    EndPanel();
}

} // namespace ev33
