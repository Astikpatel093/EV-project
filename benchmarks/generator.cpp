// benchmark_data_generator - writes the reproducible benchmark datasets and their metadata.
//
//   benchmark_data_generator [--seed 12345] [--size B1..B6|all] [--out data/generated] [--verify]
//
// Files (all SYNTHETIC, integer data, identical on every platform for the same seed):
//   graphs/<size>-<variant>.csv   u,v,weight     (undirected; vertices 0..n-1)
//   sort/keys-<order>-<n>.csv     key
//   placement/candidates-<n>.csv  id,cost_lakh,benefit
//   sessions/sessions-<n>.csv     id,start_min,end_min
//   metadata.json                 seed, parameters, counts and an FNV-1a checksum of every file
// --verify regenerates everything in memory and compares the checksums with metadata.json.
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "ev/datagen/benchmark_generator.hpp"
#include "ev/graph/spanning_tree.hpp"
#include "ev/io/csv.hpp"
#include "ev/io/json_writer.hpp"
#include "ev/io/scenario_io.hpp"

using namespace ev;

namespace {

struct FileOut {
    std::string path;      // relative to the output directory
    std::string text;
    std::string kind;
    std::map<std::string, std::int64_t> counts;
};

std::string graph_csv(const GraphDataset &d) {
    std::string t = "# SYNTHETIC benchmark graph " + d.name + " seed=" + std::to_string(d.seed) + "\nu,v,weight\n";
    char buf[64];
    for (const Edge &e : d.edges) {
        std::snprintf(buf, sizeof buf, "%d,%d,%lld\n", e.u, e.v, static_cast<long long>(e.weight));
        t += buf;
    }
    return t;
}

std::vector<FileOut> build(std::uint64_t seed, const std::string &only) {
    std::vector<FileOut> out;
    for (const GraphSize &g : benchmark_graph_sizes()) {
        if (only != "all" && only != g.name) continue;
        for (GraphVariant v : {GraphVariant::ConnectedVaried, GraphVariant::ConnectedEqual, GraphVariant::Disconnected}) {
            GraphDataset d = make_graph_dataset(g.vertices, g.edges, v, seed, std::string(g.name) + "-" + to_string(v));
            out.push_back({"graphs/" + d.name + ".csv", graph_csv(d), "graph",
                           {{"vertices", d.vertices}, {"edges", static_cast<std::int64_t>(d.edges.size())}, {"components", d.components}}});
        }
    }
    if (only == "all") {
        for (int n : {100, 200, 400}) {
            GraphDataset d = make_graph_dataset(n, n * (n - 1) / 4, GraphVariant::DenseSmall, seed, "D" + std::to_string(n) + "-dense-small");
            out.push_back({"graphs/" + d.name + ".csv", graph_csv(d), "graph",
                           {{"vertices", d.vertices}, {"edges", static_cast<std::int64_t>(d.edges.size())}, {"components", d.components}}});
        }
        for (KeyOrder k : {KeyOrder::Random, KeyOrder::Sorted, KeyOrder::Reversed, KeyOrder::FewDistinct})
            for (std::size_t n : {1000u, 10000u, 100000u, 1000000u}) {
                std::vector<std::int64_t> keys = make_sort_keys(n, k, seed);
                std::string t = "# SYNTHETIC sort keys order=" + std::string(to_string(k)) + " seed=" + std::to_string(seed) + "\nkey\n";
                for (std::int64_t x : keys) t += std::to_string(x) + "\n";
                out.push_back({"sort/keys-" + std::string(to_string(k)) + "-" + std::to_string(n) + ".csv", t, "sort", {{"n", static_cast<std::int64_t>(n)}}});
            }
        for (std::size_t n : {25u, 50u, 100u, 200u, 400u, 800u}) {
            std::vector<PlacementItem> items = make_placement_items(n, seed);
            std::string t = "# SYNTHETIC placement candidates seed=" + std::to_string(seed) + "\nid,cost_lakh,benefit\n";
            for (std::size_t i = 0; i < items.size(); i++)
                t += std::to_string(i + 1) + "," + std::to_string(items[i].cost_lakh) + "," + std::to_string(items[i].benefit) + "\n";
            out.push_back({"placement/candidates-" + std::to_string(n) + ".csv", t, "placement", {{"n", static_cast<std::int64_t>(n)}}});
        }
        for (std::size_t n : {1000u, 10000u, 100000u, 1000000u}) {
            std::vector<SessionItem> s = make_sessions(n, seed);
            std::string t = "# SYNTHETIC charging sessions seed=" + std::to_string(seed) + "\nid,start_min,end_min\n";
            for (const SessionItem &x : s) t += std::to_string(x.id) + "," + std::to_string(x.start_min) + "," + std::to_string(x.end_min) + "\n";
            out.push_back({"sessions/sessions-" + std::to_string(n) + ".csv", t, "sessions", {{"n", static_cast<std::int64_t>(n)}}});
        }
    }
    return out;
}

std::string hex(std::uint64_t h) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

std::string metadata(std::uint64_t seed, const std::vector<FileOut> &files) {
    JsonWriter j;
    j.begin_object();
    j.string("generator", "benchmark_data_generator (EV33)");
    j.string("data", "SYNTHETIC - generated, not measured");
    j.integer("seed", static_cast<std::int64_t>(seed));
    j.string("rng", "xorshift64* seeded through splitmix64 (ev/core/rng.hpp); integer arithmetic only");
    j.begin_object("parameters");
    j.string("graph_construction", "random spanning tree per component, then random extra edges; no self-loops, no duplicate edges");
    j.string("weights", "connected-varied / disconnected: integers 1..1000; connected-equal: all 10");
    j.string("disconnected", "4 components of equal size, edges split in proportion");
    j.string("dense_small", "n(n-1)/4 edges (half of all pairs)");
    j.string("placement_items", "cost_lakh 10..100, benefit = 2*cost + 0..50 (weakly correlated)");
    j.string("sessions", "start 0..1439 min, duration 10..90 min");
    j.end_object();
    j.begin_array("sizes");
    for (const GraphSize &g : benchmark_graph_sizes()) {
        j.begin_object();
        j.string("name", g.name);
        j.integer("vertices", g.vertices);
        j.integer("edges", g.edges);
        j.end_object();
    }
    j.end_array();
    j.begin_array("files");
    for (const FileOut &f : files) {
        j.begin_object();
        j.string("path", f.path);
        j.string("kind", f.kind);
        for (const auto &kv : f.counts) j.integer(kv.first.c_str(), kv.second);
        j.integer("bytes", static_cast<std::int64_t>(f.text.size()));
        j.string("fnv1a64", hex(fnv1a(f.text)));
        j.end_object();
    }
    j.end_array();
    j.end_object();
    return j.str();
}

} // namespace

int main(int argc, char **argv) {
    std::uint64_t seed = kBenchmarkSeed;
    std::string out = "data/generated", only = "all";
    bool verify = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--seed" && i + 1 < argc) {
            std::int64_t v;
            if (!parse_int64(argv[++i], v) || v < 0) { std::cerr << "error: --seed expects a non-negative integer\n"; return 1; }
            seed = static_cast<std::uint64_t>(v);
        } else if (a == "--size" && i + 1 < argc) only = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--verify") verify = true;
        else {
            std::cerr << "usage: benchmark_data_generator [--seed N] [--size B1..B6|all] [--out DIR] [--verify]\n";
            return 1;
        }
    }
    bool known = only == "all";
    for (const GraphSize &g : benchmark_graph_sizes()) known = known || only == g.name;
    if (!known) { std::cerr << "error: --size must be B1..B6 or all\n"; return 1; }
    try {
        std::vector<FileOut> files = build(seed, only);
        if (verify) {
            auto meta = read_text_file(out + "/metadata.json");
            if (!meta) { std::cerr << "error: " << out << "/metadata.json not found\n"; return 1; }
            int bad = 0;
            for (const FileOut &f : files) {
                const bool ok = meta->find("\"path\": \"" + f.path + "\"") != std::string::npos &&
                                meta->find(hex(fnv1a(f.text))) != std::string::npos;
                if (!ok) { std::cout << "MISMATCH " << f.path << "\n"; bad++; }
            }
            std::cout << (bad ? "verification FAILED: " : "verified: ") << files.size() - static_cast<std::size_t>(bad) << "/" << files.size()
                      << " files reproduce metadata.json checksums (seed " << seed << ")\n";
            return bad ? 2 : 0;
        }
        std::size_t bytes = 0;
        for (const FileOut &f : files) {
            const std::string path = out + "/" + f.path;
            make_directories(path.substr(0, path.find_last_of('/')));
            write_text_file(path, f.text);
            bytes += f.text.size();
        }
        if (only == "all") write_text_file(out + "/metadata.json", metadata(seed, files));
        std::cout << "wrote " << files.size() << " files (" << bytes / 1024 << " KiB) to " << out << " with seed " << seed
                  << (only == "all" ? "; metadata.json lists their checksums" : "") << "\n";
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
