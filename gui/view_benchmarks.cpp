// view_benchmarks.cpp - measurements read from the CSV written by ev33_bench (benchmarks/results.csv).
// Nothing is estimated: if the file is missing the view says so. The optional "Run quick benchmark"
// button starts the separate ev33_bench program in a background thread and loads its output.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "content.h"
#include "ev/algorithms/sorting.hpp"
#include "ui.h"

namespace ev33 {

static bool fileExists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static std::string findBench(App &app) {
#ifdef _WIN32
    const char *name = "ev33_bench.exe";
#else
    const char *name = "ev33_bench";
#endif
    for (const std::string &d : { app.exe_dir + "/benchmarks", app.exe_dir, app.exe_dir + "/../benchmarks", std::string("build/benchmarks") })
        if (fileExists(d + "/" + name)) return d + "/" + name;
    return "";
}

static void startJob(App &app, const std::string &exe) {
    BenchJob &j = app.job;
    if (j.worker.joinable()) j.worker.join();
    j.output_file = app.eng.results_dir + "/results_local.csv";
    j.command = "\"" + exe + "\" --quick --out \"" + j.output_file + "\"";
#ifdef _WIN32
    std::string cmd = "\"" + j.command + " > NUL 2>&1\"";   // cmd.exe strips the outer quotes
#else
    std::string cmd = j.command + " > /dev/null 2>&1";
#endif
    j.running = true;
    j.finished = false;
    j.worker = std::thread([&j, cmd] {
        int rc = std::system(cmd.c_str());
        j.exit_code = rc;
        j.running = false;
    });
}

void DrawBenchmarks(App &app) {
    Engine &E = app.eng;
    const Palette &P = app.pal;
    float S = app.scale;
    ViewHeader(app, "Benchmarks",
               "Run times measured by the separate benchmark program (ev33_bench) on generated datasets with seed 12345. Each row is the median of repeated runs "
               "and was checked for correctness outside the timed part.");

    // finished background run: load its file
    BenchJob &j = app.job;
    if (!j.running && j.worker.joinable()) {
        j.worker.join();
        j.finished = true;
        if (j.exit_code == 0 && fileExists(j.output_file)) {
            E.reloadBench(j.output_file);
            app.notify("Loaded the new measurements from " + j.output_file, ImGui::GetTime());
        } else {
            app.notify("The benchmark program did not finish successfully (exit code " + std::to_string(j.exit_code) + ").", ImGui::GetTime(), true);
        }
    }

    const std::vector<BenchRow> &rows = E.bench();
    BeginPanel(app, "src", ImVec2(0, 0));
    int failed = 0;
    for (const BenchRow &r : rows) failed += !r.pass;
    ImGui::PushFont(app.fonts.small);
    if (!rows.empty())
        ImGui::TextColored(P.ink2, "File: %s   |   %s measurements, %d failed checks   |   %s, %s", E.bench_path.c_str(), fint((double)rows.size()).c_str(), failed,
                           rows[0].compiler.c_str(), rows[0].build.c_str());
    else
        ImGui::TextColored(P.ink2, "File: %s", E.bench_path.c_str());
    ImGui::PopFont();
    if (ImGui::Button("Reload")) E.reloadBench();
    ImGui::SameLine();
    std::string def = E.results_dir + "/results.csv";
    if (E.bench_path != def && ImGui::Button("Show the committed results")) E.reloadBench(def);
    if (E.bench_path != def) ImGui::SameLine();
    std::string exe = findBench(app);
    ImGui::BeginDisabled(exe.empty() || j.running);
    if (ImGui::Button(j.running ? "Running ev33_bench --quick ..." : "Run quick benchmark (about 15 s)")) startJob(app, exe);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushFont(app.fonts.small);
    ImGui::AlignTextToFramePadding();
    if (exe.empty()) ImGui::TextColored(P.ink3, "ev33_bench was not found next to this program; run it from a terminal (README, Benchmarks).");
    else ImGui::TextColored(P.ink3, "Writes %s/results_local.csv and shows it; the committed results.csv is not changed.", E.results_dir.c_str());
    ImGui::PopFont();
    EndPanel();

    if (rows.empty()) {
        ImGui::Dummy(ImVec2(0, 8 * S));
        ImGui::TextColored(P.warn, "No benchmark results available. Run the benchmark executable to generate measurements.");
        if (!E.bench_error.empty()) ImGui::TextColored(P.ink3, "%s", E.bench_error.c_str());
        return;
    }

    // families (shared list + trip planning)
    std::vector<Family> fams = FAMILIES;
    fams.push_back({ "Trip planning", { "trip_planning" } });
    std::vector<const char *> labels;
    for (auto &f : fams) labels.push_back(f.label);
    app.bench_family = std::max(0, std::min((int)fams.size() - 1, app.bench_family));
    ImGui::Dummy(ImVec2(0, 4 * S));
    {
        float x = 0, avail = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
        for (int i = 0; i < (int)labels.size(); i++) {
            bool on = i == app.bench_family;
            float w = ImGui::CalcTextSize(labels[(size_t)i]).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i) { if (x + sp + w > avail) x = 0; else { ImGui::SameLine(); x += sp; } }
            ImGui::PushStyleColor(ImGuiCol_Button, on ? P.primary : P.panel);
            ImGui::PushStyleColor(ImGuiCol_Text, on ? P.panel : P.ink);
            if (ImGui::Button(labels[(size_t)i])) app.bench_family = i;
            ImGui::PopStyleColor(2);
            x += w;
        }
    }
    const Family &F = fams[(size_t)app.bench_family];
    auto inFamily = [&](const BenchRow &r) { return std::find(F.fam.begin(), F.fam.end(), r.family) != F.fam.end(); };

    float gap = 12 * S, avail = ImGui::GetContentRegionAvail().x;
    float chartW = std::max(360 * S, avail * 0.62f - gap);
    BeginPanel(app, "chart", ImVec2(chartW, 0));
    static const unsigned PAL[] = { 0x2E6A4F, 0x4B6D8E, 0xA4670F, 0xB13A31, 0x6E5A8A, 0x3F8A8A };
    auto color = [&](size_t k) {
        unsigned c = PAL[k % 6];
        ImVec4 v(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1);
        return app.dark ? mix(v, ImVec4(1, 1, 1, 1), 0.3f) : v;
    };
    if (F.fam[0] == "trip_planning") {
        SectionTitle(app, "Median time for 200 trips", "Bars: milliseconds per algorithm and city (same trips for both planners).");
        std::vector<std::string> lab;
        std::vector<double> val;
        for (const BenchRow &r : rows)
            if (inFamily(r)) { lab.push_back(std::string(r.alg == "exact_state_space" ? "exact " : "legs ") + (r.dataset.size() > 9 ? r.dataset.substr(9) : r.dataset)); val.push_back(r.ms); }
        ChartOptions o;
        o.yLabel = "ms";
        BarChart(app, "tpbars", ImVec2(ImGui::GetContentRegionAvail().x, 300 * S), lab, val, nullptr, P.secondary, P.warn, o);
    } else {
        SectionTitle(app, "Run time against input size (log-log)", "Each line is one algorithm on the family's typical input; the table lists every measurement.");
        std::map<std::string, Series> series;
        for (const BenchRow &r : rows) {
            if (!inFamily(r) || benchSkip(r) || r.ms <= 0 || r.n <= 0) continue;
            Series &s = series[r.alg];
            s.name = algName(r.alg);
            s.x.push_back(r.n);
            s.y.push_back(r.ms);
        }
        std::vector<Series> list;
        size_t k = 0;
        for (auto &kv : series) {
            Series s = kv.second;
            std::vector<size_t> idx(s.x.size());
            for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
            ev::merge_sort(idx, [&](size_t a, size_t b) { return s.x[a] < s.x[b]; });
            Series t;
            t.name = s.name;
            t.color = color(k++);
            for (size_t i : idx) { t.x.push_back(s.x[i]); t.y.push_back(s.y[i]); }
            list.push_back(t);
        }
        ChartOptions o;
        o.logX = o.logY = true;
        o.xLabel = "input size n";
        o.yLabel = "median time (ms)";
        LineChart(app, "benchchart", ImVec2(ImGui::GetContentRegionAvail().x, 300 * S), list, o);
        for (size_t i = 0; i < list.size(); i++) {
            if (i) ImGui::SameLine(0, 14 * S);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float fs = app.fonts.small->FontSize;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + fs * 0.35f), ImVec2(p.x + 10 * S, p.y + fs * 0.35f + 4 * S), col(list[i].color));
            ImGui::Dummy(ImVec2(14 * S, fs));
            ImGui::SameLine(0, 2);
            ImGui::PushFont(app.fonts.small);
            ImGui::TextUnformatted(list[i].name.c_str());
            ImGui::PopFont();
        }
    }
    EndPanel();
    ImGui::SameLine(0, gap);
    BeginPanel(app, "notes", ImVec2(0, 0));
    SectionTitle(app, "Worst cases and trade-offs");
    for (auto &nt : adversarialNotes(rows)) {
        ImGui::PushFont(app.fonts.semi);
        ImGui::TextWrapped("%s", nt.first.c_str());
        ImGui::PopFont();
        ImGui::PushFont(app.fonts.small);
        ImGui::PushStyleColor(ImGuiCol_Text, P.ink2);
        ImGui::TextWrapped("%s", nt.second.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 3 * S));
    }
    EndPanel();

    ImGui::Dummy(ImVec2(0, 6 * S));
    SectionTitle(app, "Measurements");
    if (ImGui::BeginTable("bench", 12, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
        for (const char *h : { "Algorithm", "Dataset", "Variant", "Input size", "Vertices", "Edges", "Reps", "Median", "Min", "Max", "Peak memory", "Correct" })
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const BenchRow &r : rows) {
            if (!inFamily(r)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(algName(r.alg).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.dataset.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", r.variant.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(fint(r.n).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.vertices > 0 ? fint(r.vertices).c_str() : "\xE2\x80\x93");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.edges > 0 ? fint(r.edges).c_str() : "\xE2\x80\x93");
            ImGui::TableNextColumn(); ImGui::Text("%d", r.reps);
            ImGui::TableNextColumn(); ImGui::PushFont(app.fonts.mono); ImGui::TextUnformatted(fms(r.ms).c_str()); ImGui::PopFont();
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", fms(r.min_ms).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(P.ink2, "%s", fms(r.max_ms).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(fbytes((double)r.peak).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(r.pass ? P.primary : P.error, "%s", r.pass ? "pass" : "FAIL");
            if (!r.note.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.note.c_str());
        }
        ImGui::EndTable();
    }
}

} // namespace ev33
