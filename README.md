# EV33 — EV Charging Station Placement and Trip Planning System

Project 33 · 8 weeks · C11 engine, C++17 desktop app · student product with individual algorithm ownership

EV33 decides **where to install charging stations under a budget** and **how each electric
vehicle trip should be routed and recharged** so that it never drops below its battery reserve.
Every mandatory syllabus algorithm is implemented from scratch in a reusable library and is
mapped to a concrete electric-mobility decision, benchmarked against a sibling algorithm, and
verified against an oracle.

Everything is written in **C and C++ only**: the engine, CLI, benchmark runner, generator and tests
in C11, the desktop app and the website server in C++17.

```
make            # builds ev33, ev33_bench, ev33_gen, ev33_tests (gcc or clang, C11, no dependencies)
make test       # 59,000+ checks: unit, oracle, integration, invalid input, regression, leak balance
make demo       # the three demonstration scenarios -> results/demo/ (runs ./build/ev33 demo)
make run-gui    # builds and opens the desktop app (C++17, CMake, OpenGL)
make web        # builds the website server build/web/ev33_web (C++17, deployed on Render)
```

## What it does

| Stage | Decision | Algorithms |
|---|---|---|
| Load & index | parse 5 typed CSV/config files, reject bad data with `file:line: reason` | merge sort + binary search node-id index |
| Service value | how much each candidate site is worth | Dijkstra trees, trip traversal counts |
| **Station selection** | which sites to build within the budget | greedy ratio, **0/1 knapsack DP**, **branch-and-bound FIFO / LIFO / least-cost** |
| **Trip planning** | route + charging stops that respect battery range and reserve | **Dijkstra** (km), **Floyd-Warshall** (all pairs), **Bellman-Ford** (energy with regenerative braking ⇒ negative arcs) over a "charging-leg graph" |
| Capacity allocation | how many charge-needing trips the installed ports can absorb | **Ford-Fulkerson (DFS)**, **Edmonds-Karp (BFS)**, **bipartite matching** (cross-checked) |
| Charger sessions | chargers each station needs; sessions servable | **interval partitioning**, **activity selection** |
| Grid surge | worst consecutive hours of load above grid supply | **max-subarray (divide & conquer)** |
| Operations | backbone cable, maintenance tour, maintenance alignment, lookups | **Prim / Kruskal**, **Held-Karp TSP**, **CRT**, **optimal BST**, **celebrity** (overflow hub) |
| Analytics | log compression, fault-pattern detection, route adherence, demand propagation | **Huffman**, **naive / Rabin-Karp / automaton / KMP**, **LCS**, **Strassen vs classical**, **matrix-chain**, **fast power** |
| Validation | backtracking framework | **N-Queens**, **subset sum** |

## Demonstration results (from `results/demo/transcript.txt`)

| Scenario | Data | Exact (DP + Dijkstra) | Alternative (greedy + Bellman-Ford) |
|---|---|---|---|
| Normal operation | 256 junctions, 60 sites, 1,200 trips, budget 900 | 10 stations, value 2,480, **1,181/1,200 trips feasible** | value 2,417 (97.5 %), 1,177 feasible, 0.4 % less energy per trip |
| Constraint shock | budget −45 %, 30 % port outages, bridge closures, +60 % evening demand | 6 stations, 1,856/1,920 feasible; flow shows only **144 of 751** charge-needing trips fit the ports; 5 stations overloaded; grid surge 17:00–20:00 | 1,834 feasible (−22 trips) |
| Large-data stress | 1,024 junctions, 220 sites, 6,000 trips | 26 stations, 5,827/6,000 feasible; whole pipeline < 1 s | Floyd-Warshall routing 5.5× slower (730 ms vs 132 ms) with identical plans |

Full numbers: [docs/COMPLEXITY_REPORT.md](docs/COMPLEXITY_REPORT.md) and `results/bench.csv` (242 benchmark rows, all checks passing).

## Website (`ev33_web`, hosted on Render)

`server/web.cpp` is a small HTTP server in C++17 on the same engine. It generates every page
itself: HTML for the layout, SVG for the map and charts, CSS from a string in the source. There is
no JavaScript; choices are links and forms, and the live day plays by reloading once a second. It
has the same seven views as the desktop app (Planner, Live day, Strategies, Capacity, Algorithms,
Data model, Benchmarks), and the plan travels in the address, so any view can be bookmarked or
shared.

```
make web                      # build/web/ev33_web (CMake, no graphics libraries needed)
./build/web/ev33_web          # http://127.0.0.1:8033 (PORT in the environment changes the port)
```

**Deploying on Render.** The repository contains a `Dockerfile` (builds the server, runs the test
suite, then makes a small runtime image with the data and fonts) and `render.yaml` (one free web
service). In Render choose *New → Blueprint*, pick this GitHub repository and apply; Render builds
the image and gives the site an `onrender.com` address. `/health` answers `ok` for the health
check. Free instances sleep when idle, so the first visit after a while takes a few seconds.

## Desktop app (`ev33_gui`)

A C++17 window app that links the C engine directly and recomputes as you change anything. It
uses [Dear ImGui](https://github.com/ocornut/imgui) and [ImPlot](https://github.com/epezent/implot)
(C++) on [GLFW](https://github.com/glfw/glfw) (C) with OpenGL; all three are vendored in
`third_party/`, so the build needs only CMake and a C/C++ compiler.

```
make gui                                    # or: cmake -S . -B build/gui && cmake --build build/gui --target ev33_gui
./build/gui/ev33_gui                        # run from the project folder (it finds data/, results/, assets/)
```

On Linux install the X11 development headers once (`sudo apt install libx11-dev libxrandr-dev
libxinerama-dev libxcursor-dev libxi-dev libgl-dev`). On Windows use Visual Studio or MinGW with
CMake; no extra packages are needed. (macOS builds the C programs only: GLFW's macOS backend is
Objective-C, so it is left out.)

The left sidebar holds the plan (city, selection method, routing method, budget, what-if edits)
and every tab uses it:

| Tab | What it shows |
|---|---|
| Planner | five headline figures, map with built stations, what-if editing of any site, baseline comparison, copy built sites as CSV, trip inspector with an animated route and a battery chart |
| Live day | the day simulation: vehicles drive, queue and charge on the map; play at real time up to 1 h/s, scrub, jump to now; drivers wait or switch station after 20 min; busiest stations with +2 ports; grid draw over the day |
| Strategies | all 15 selection × routing pairings, coloured by trips completed, energy, routing time or selection value |
| Capacity | charging energy by hour against grid supply, busiest stations against their ports, the three max-flow checks |
| Algorithms | every decision, the algorithm behind it, its result on this city and its independent check, plus the engine's full report |
| Data model | the typed records (nodes, roads, stations, trips, battery and budget) with real values, and the arrays, trees and weighted or capacitated graphs built from them |
| Benchmarks | log-log scaling charts from `results/bench.csv` and the adversarial inputs |

Map: wheel to zoom at the pointer, drag to pan, double-click to zoom in, *Fit* to reset; *Traffic*
colours each road by planned trips. *Save screenshot* writes the window to a PNG. *Dark theme*
switches colours.

Command-line options open a given state, which is useful for reports:
`./build/gui/ev33_gui --city shock --page live --time 19:10 --policy divert --shot shock.png`
saves that view as a PNG and exits (`--help` lists every option).

## Repository layout

```
include/        public headers - one per algorithm family + domain
src/lib/        reusable algorithm library (no domain knowledge)
  ev_mem.c        tracked allocator (peak-heap metric), timer, deterministic RNG
  ev_sort.c       merge sort, quick sort (median-of-3 / first / random pivot), binary search
  ev_dnc.c        max subarray, fast power, celebrity, classical + Strassen matmul
  ev_greedy.c     activity selection, interval partitioning, Huffman
  ev_graph.c      CSR graph, Dijkstra, Bellman-Ford, Floyd-Warshall, Prim, Kruskal, union-find
  ev_dp.c         0/1 knapsack, matrix chain, LCS, optimal BST, Held-Karp TSP
  ev_search.c     N-Queens, subset sum, knapsack branch-and-bound (FIFO/LIFO/LC)
  ev_flow.c       Ford-Fulkerson, Edmonds-Karp, min-cut, bipartite matching
  ev_nt.c         gcd / extended gcd / CRT, naive, Rabin-Karp, finite automaton, KMP
src/domain/     EV model, loader + validation, router, planner, capacity, sessions, reports
src/app/        main.c (CLI, interactive menu, demo), bench.c (benchmark runner)
shared/         C++17 layer used by both front ends: engine.cpp (calls the C library), content.cpp
                (report rows, data-model tables, summaries), fmt.cpp (number formatting), json.h
gui/            desktop app: main.cpp (window, tabs), map.cpp (city map), views.cpp (the seven
                tabs), ui.cpp (theme, text, PNG export)
server/         web.cpp - the website: HTTP server that generates HTML + SVG pages
Dockerfile      image Render builds; render.yaml is the Render blueprint
third_party/    Dear ImGui, ImPlot, GLFW (vendored, licences included)
assets/fonts/   IBM Plex Sans and Mono (SIL Open Font License)
tools/          ev33_gen.c - deterministic scenario generator (5 profiles)
tests/          test_main.c + regression fixtures
data/           small, medium, shock, large, adversarial scenarios (seed 33)
results/        bench.csv, bench_console.txt, demo/ (transcript, CSV, JSON)
docs/           DESIGN, USER_GUIDE, COMPLEXITY_REPORT, DEMO_SCRIPT, OWNERSHIP, PROJECT_PLAN
```

## Quick CLI tour

```
./build/ev33 validate --data data/medium
./build/ev33 run      --data data/shock --select bnb-lc --route bellman --out out/shock
./build/ev33 compare  --data data/large --out out/large          # 5 x 3 strategy matrix
./build/ev33 analyze  --data data/medium --out out/medium        # every syllabus module on real data
./build/ev33 plan     --data data/large --trip 22                # explain one trip's stops
./build/ev33 menu     --data data/small                          # interactive
./build/ev33 demo                                               # three scenarios -> results/demo/
./build/ev33_bench    [--quick] [--only knapsack]                # -> results/bench.csv
./build/ev33_gen      --profile large --seed 7 --out data/my_city
```

Windows: build with CMake (`cmake -S . -B build && cmake --build build --config Release`) using
MSVC or MinGW; the C code uses only C11 + `<sys/stat.h>`/`<direct.h>`. The same CMake build also
makes `ev33_gui` (turn it off with `-DEV33_GUI=OFF`).

## Release

`make release TAG=v1.0.0` runs the tests, makes the first commit under your own Git identity
and creates the release tag.

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) — architecture, data model, energy model, planning algorithm, interfaces
- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) — build, data formats, every command, outputs
- [docs/COMPLEXITY_REPORT.md](docs/COMPLEXITY_REPORT.md) — recurrences, asymptotics vs measurements, correctness evidence, complexity classes, reductions, production recommendation
- [docs/DEMO_SCRIPT.md](docs/DEMO_SCRIPT.md) — 15-minute demonstration walkthrough
- [docs/OWNERSHIP.md](docs/OWNERSHIP.md) — per-member algorithm ownership matrix
- [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md) — 8-week milestone plan
