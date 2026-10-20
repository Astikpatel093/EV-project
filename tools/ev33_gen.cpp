// ev33_gen - writes a deterministic SYNTHETIC scenario directory.
//
//   ev33_gen --profile small|medium|shock|large|adversarial [--seed N] --out DIR
//   ev33_gen --all [--seed N] --out ROOT        (every profile into ROOT/<profile>)
#include <cstdlib>
#include <iostream>
#include <string>

#include "ev/datagen/scenario_generator.hpp"
#include "ev/io/csv.hpp"
#include "ev/io/scenario_io.hpp"

int main(int argc, char **argv) {
    std::string profile, out;
    std::uint64_t seed = 33;
    bool all = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--profile" && i + 1 < argc) profile = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--all") all = true;
        else if (a == "--seed" && i + 1 < argc) {
            std::int64_t v;
            if (!ev::parse_int64(argv[++i], v) || v < 0) { std::cerr << "error: --seed expects a non-negative integer\n"; return 1; }
            seed = static_cast<std::uint64_t>(v);
        } else {
            std::cerr << "usage: ev33_gen --profile small|medium|shock|large|adversarial [--seed N] --out DIR\n"
                         "       ev33_gen --all [--seed N] --out ROOT\n";
            return 1;
        }
    }
    if (out.empty() || (profile.empty() && !all)) {
        std::cerr << "error: give --profile NAME (or --all) and --out DIR\n";
        return 1;
    }
    try {
        for (const std::string &p : all ? ev::scenario_profiles() : std::vector<std::string>{profile}) {
            ev::Scenario s = ev::generate_scenario(p, seed);
            const std::string dir = all ? out + "/" + p : out;
            ev::write_scenario(s, dir, ev::synthetic_banner(p, seed));
            std::cout << p << ": " << s.network.node_count() << " nodes, " << s.network.edge_count() << " roads, " << s.stations.size()
                      << " existing stations, " << s.candidates.size() << " candidates, " << s.vehicles.size() << " vehicles, "
                      << s.trips.size() << " trips, " << s.sessions.size() << " sessions, " << s.events.size() << " events -> " << dir << "\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
