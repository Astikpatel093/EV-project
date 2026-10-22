# EV33 — EV Charging Station Placement and Trip Planning System

EV33 answers two questions for an electric-vehicle network:

1. **Where should new charging stations be installed** within an installation budget?
2. **How should each trip be driven and recharged** so that the battery never falls below the
   vehicle's reserve — and, when that is impossible, why?

The whole system is **C++17** (engine, command line, tests, benchmarks, website server and the
native desktop app **EV33 Charge Atlas**). Every assessed algorithm is written by hand; the engine
uses only the C++ standard library. The desktop app adds three external C/C++ libraries (Dear ImGui,
GLFW, OpenGL 3), and the tests use GoogleTest — see [Requirements](#4-requirements).
Live website: <https://ev-project-84u5.onrender.com>.

Contents: [1 Features](#1-features) · [2 Data honesty](#2-data-honesty) · [3 Repository layout](#3-repository-layout) ·
[4 Requirements](#4-requirements) · [5 Build on Linux or macOS](#5-build-on-linux-or-macos) ·
[6 Build on Windows (MinGW-w64)](#6-build-on-windows-mingw-w64) · [7 Command-line use](#7-command-line-use) ·
[8 Input data](#8-input-data) · [9 Desktop app and website](#9-desktop-app-and-website) ·
[10 Tests](#10-tests) · [11 Benchmarks](#11-benchmarks) · [12 Algorithms](#12-algorithms) ·
[13 Results](#13-results) · [14 Limitations and responsible use](#14-limitations-and-responsible-use) ·
[15 Documentation and checklist](#15-documentation-and-checklist)

## 1. Features

| Stage | Decision | Algorithms |
|---|---|---|
| Load and validate | typed CSV records; any problem reported as `file:line: reason` | CSV parser by column name, merge sort + binary search indexes |
| Benefit | how much each candidate site is worth (given, or estimated from the trips) | shortest-path trees over all trips |
| **Placement** | which candidates to build within the budget (exact integer paise) | **0/1 knapsack DP**, **branch and bound (FIFO / LIFO / least cost)**, greedy ratio |
| **Trip planning** | route + charging stops that keep the battery above the reserve, or a stated reason why not | **exact state-space Dijkstra** over (junction, battery), fleet **charging-leg planner** with **Dijkstra / Floyd–Warshall / Bellman–Ford** legs |
| Capacity | how many charge-needing trips the ports can absorb | **Ford–Fulkerson**, **Edmonds–Karp**, **bipartite matching**, min cut (must agree) |
| Sessions | bays each station needs; sessions it can serve | **interval partitioning**, **activity selection** |
| Grid surge | worst run of hours above grid supply | **maximum subarray (divide and conquer)** |
| Day simulation | queues and delays with the real port counts; drivers wait or divert | event heap, FIFO queues |
| Analysis report | backbone cable, maintenance tour and windows, station index, event log | **Prim / Kruskal**, **Held–Karp**, **CRT**, **optimal BST**, **Huffman**, **KMP / Rabin–Karp / automaton / naive**, **LCS**, **Strassen**, **matrix chain**, **fast power**, **celebrity**, **N-Queens**, **subset sum** |

Every trip result has a status — `direct`, `with-charging`, `zero-length`, `no-road-connection`,
`start-below-reserve`, `out-of-range` or `charging-insufficient` — and a sentence explaining it.
Existing stations can be offline or have incompatible connectors; vehicles have their own battery,
consumption, reserve and connector.

## 2. Data honesty

All bundled data is **synthetic**: a hand-made 10-node sample whose answers are worked out by
hand (`data/sample/README.md`), five generated cities (`data/scenarios/`, seed 33) and generated
benchmark datasets (seed 12345). Every generated file starts with a `# SYNTHETIC` comment, the
scenario files record `data_source = synthetic`, and coordinates sit near 0° N 0° E on purpose.
They are not real places, operators or vehicles. Every number in this README and in `docs/` was
produced by running the programs; nothing is estimated by hand. See `docs/data.md` for how to use
real data (for example OpenStreetMap roads) and the privacy rules that come with it.

## 3. Repository layout

```
include/ev/            public headers, one folder per layer
  core/                  Paise money type, Rng, Stopwatch, DataError
  algorithms/            sorting, searching, divide & conquer, greedy, DP, backtracking,
                         branch and bound, number theory, string matching, binary heap
  graph/                 Graph, Dijkstra / Bellman-Ford / Floyd-Warshall, Prim / Kruskal, max flow
  models/                RoadNetwork, ChargingStation, CandidateStation, ElectricVehicle, TripRequest, Scenario
  io/                    CSV reader, scenario loader/writer, JSON writer
  planner/               EnergyModel, Router, ExactPlanner, LegPlanner, TripPlan
  placement/             benefits and budgeted placement
  operations/            capacity, sessions, day simulation
  analysis/              PlanningRun pipeline, reports, comparison, textbook demos
  datagen/               scenario and benchmark-dataset generators
src/                   implementations (same folders) + app/ (the ev33 command line)
tools/ev33_gen.cpp     scenario generator
tests/                 unit/, integration/ (IT01-IT08), adversarial/ (AD01-AD25), fixtures/
benchmarks/            benchmark.cpp (ev33_bench), gbench.cpp (Google Benchmark),
                       generator.cpp (benchmark_data_generator), results.csv
shared/ server/ gui/   website and desktop app (shared adapter, web.cpp, Dear ImGui views)
data/                  sample/, scenarios/, generated/ (benchmark data, regenerated on demand)
docs/                  architecture, algorithms, testing, benchmark methodology, data, user guide
third_party/           googletest, benchmark, imgui, glfw (vendored, licences included)
assets/fonts/          IBM Plex Sans and Mono for the desktop app (SIL OFL)
cmake/                 MinGW-w64 cross-compilation toolchain file
Makefile               short Make commands that drive CMake (make, make gui, make test, ...)
Dockerfile render.yaml website image and Render blueprint
```

## 4. Requirements

* A C++17 compiler: GCC ≥ 9, Clang ≥ 10, or MinGW-w64 GCC ≥ 9 / llvm-mingw (tested: GCC 13.3 on
  Linux, llvm-mingw Clang 19.1 for Windows x64). MSVC 2019+ should work but has not been tested.
* CMake ≥ 3.16 and a build tool (Ninja or Make). GNU Make is optional; the `Makefile` only
  shortens the CMake commands.
* Libraries, all vendored in `third_party/` so nothing has to be downloaded:

| Library | Version | Used by | Licence |
|---|---|---|---|
| C++ standard library | C++17 | engine, CLI, website, everything | — |
| GoogleTest | 1.15.2 | tests only | BSD-3 |
| Google Benchmark | 1.9.1 | `ev33_gbench` only | Apache-2.0 |
| Dear ImGui | 1.91.5 | desktop app: windows, widgets, tables, drawing | MIT |
| GLFW | 3.4 | desktop app: window, input, OpenGL context | zlib |
| OpenGL | 3.0+ (GLSL 130) | desktop app: rendering; provided by the graphics driver | — |

  OpenGL is part of every Windows graphics driver. On Linux the desktop app also needs the X11 and
  OpenGL development headers: `sudo apt install libx11-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev libgl-dev` (Debian/Ubuntu) or `sudo dnf install libX11-devel
  libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel mesa-libGL-devel` (Fedora).
  The engine, CLI, tests, benchmarks and website need none of these.

## 5. Build on Linux or macOS

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Or with Make (the `Makefile` runs the same CMake commands):

| Command | Does |
|---|---|
| `make` | configure (Release) and build the engine, CLI, tests, benchmarks and website |
| `make gui` | also build the desktop app `ev33_gui` (needs the OpenGL/X11 headers above) |
| `make run` | build and start the desktop app |
| `make test` | build and run all four test suites through CTest |
| `make bench` | build and run the benchmarks, writing `benchmarks/results.csv` |
| `make data` | regenerate the benchmark datasets in `data/generated` (seed 12345) |
| `make web` | build and start the website on <http://localhost:8033> |
| `make clean` | delete the build folder |

Variables: `BUILD=dir` (default `build`), `CONFIG=Release|Debug`, `GENERATOR="Ninja"`, `JOBS=8`.

Options (all `ON` unless noted): `EV33_BUILD_TESTS`, `EV33_BUILD_BENCHMARKS`, `EV33_BUILD_GBENCH`,
`EV33_BUILD_WEB`, `EV33_GUI` (`OFF`; desktop app), `EV33_SANITIZE` (`OFF`; AddressSanitizer +
UndefinedBehaviorSanitizer). Programs land in `build/` (`ev33`, `ev33_gen`, `ev33_web`,
`ev33_gui`), `build/tests/` and `build/benchmarks/`.

## 6. Build on Windows (MinGW-w64)

Use a 64-bit MinGW-w64 toolchain. The old 32-bit MinGW.org GCC 6.x is **too old** for C++17.

1. Install MSYS2 from <https://www.msys2.org>, open the **MSYS2 UCRT64** shell and run
   `pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`.
2. Add `C:\msys64\ucrt64\bin` to `PATH` (so PowerShell and VS Code find `g++`, `cmake`, `ninja`).
3. In the project folder (PowerShell or the UCRT64 shell):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEV33_GUI=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Executables:

| Program | Path |
|---|---|
| command line | `build\ev33.exe` |
| scenario generator | `build\ev33_gen.exe` |
| website server | `build\ev33_web.exe` (then open <http://localhost:8033>) |
| desktop app | `build\ev33_gui.exe` |
| tests | `build\tests\ev33_unit_tests.exe`, `ev33_integration_tests.exe`, `ev33_adversarial_tests.exe` |
| benchmarks | `build\benchmarks\ev33_bench.exe`, `benchmark_data_generator.exe`, `ev33_gbench.exe` |

Run the programs from the project folder so they find `data\`, `benchmarks\` and `assets\`.
With MSYS2's `make` (`pacman -S make` in the UCRT64 shell) or `mingw32-make` in PowerShell the
Make commands work too: `mingw32-make gui GENERATOR="MinGW Makefiles"`, then
`mingw32-make run GENERATOR="MinGW Makefiles"`. In VS Code, open the folder with the CMake Tools
extension, pick the UCRT64 GCC kit, set `EV33_GUI` to `ON` and choose `ev33_gui` as the launch
target. The desktop app is a Windows GUI program (no console window); start it by double-clicking
`ev33_gui.exe` or from a terminal.

Cross-compiling from Linux works too:
`cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DMINGW_ROOT=/path/to/mingw`
(this is how the Windows executables in the release ZIP were built; they link statically and need
no DLLs).

## 7. Command-line use

```
ev33 validate  --data DIR                               load and validate a scenario
ev33 run       --data DIR [--place M] [--route R] [--budget INR] [--out DIR]
ev33 plan      --data DIR --trip ID [--method exact|leg|both]
ev33 plan      --data DIR --from NODE --to NODE --vehicle V [--charge KWH] [--depart HH:MM]
ev33 place     --data DIR [--place M] [--budget INR] [--out DIR]
ev33 schedule  --data DIR [--station ID]                booked sessions: bays needed, sessions served
ev33 compare   --data DIR [--out DIR]                   every placement x routing method
ev33 analyze   --data DIR [--out DIR]                   every algorithm on the scenario
ev33 verify    --data DIR [--samples N] [--route R]     fleet planner vs exact planner
ev33 algorithms                                         textbook example of every algorithm
ev33 demo      [--data ROOT] [--out DIR]                the demonstration script
ev33 menu      [--data DIR]                             interactive menu
    M = greedy | dp | bnb-lc | bnb-fifo | bnb-lifo   R = dijkstra | floyd | bellman
```

Examples:

```
build/ev33 plan --data data/sample --trip 2 --method both     # a trip that needs one charging stop
build/ev33 run  --data data/scenarios/shock --place bnb-lc --route bellman --out out/shock
build/ev33_gen  --profile large --seed 7 --out data/my_city   # a new synthetic city
```

`run` writes `placement.csv`, `plans.csv` and `summary.json`. Exit codes: 0 success, 1 data or
usage error, 2 internal error. More in `docs/USER_GUIDE.md`.

## 8. Input data

A scenario is a folder with `scenario.cfg`, `nodes.csv`, `roads.csv` and optionally
`stations.csv`, `candidates.csv`, `vehicles.csv`, `trips.csv`, `sessions.csv`, `events.txt`.
Columns are matched by name; money is in rupees (stored exactly as paise). Full formats,
validation rules and real-data guidance: `docs/data.md`.

## 9. Desktop app and website

There are two front ends on the same engine code (`shared/engine.*` → `ev::PlanningRun`). They are
**separate programs and separate deployment targets**: the desktop app is a native Windows/Linux
executable that runs on your own computer, and the website is a server deployed on Render. The
desktop app does not replace the website, and the website is not part of the desktop app (the app
contains no HTML, CSS or JavaScript).

### 9.1 Desktop app — EV33 Charge Atlas (`ev33_gui`)

C++17 with Dear ImGui on GLFW + OpenGL 3. Every figure on screen comes from an engine call on the
loaded dataset; nothing is hard-coded or invented. A **SYNTHETIC DATASET** tag sits in the
sidebar and on each view whenever the loaded data is generated.

| View (Ctrl+1…8) | What it does |
|---|---|
| Dashboard | nodes, roads, existing stations, candidate sites, trips, selected stations, budget, total cost, money left; trip results; capacity (three max-flow methods); hourly charging energy against grid supply; busiest stations |
| Trip Planner | form: source and destination junction, EV model (or custom), battery, starting charge, consumption, reserve, departure time, routing method; **Calculate route** runs the exact planner or the charging-leg planner (Dijkstra / Floyd–Warshall / Bellman–Ford legs). Shows feasibility, a sentence explaining the result, distance, energy, driving and charging time, stops, battery profile chart, route sequence and a small map |
| Station Placement | method (0/1 knapsack DP, branch and bound FIFO / LIFO / least cost, greedy ratio), budget slider; selected count, total cost, money left, total benefit; candidate table with cost, benefit, benefit per lakh, ports and force/forbid; existing stations; a comparison of every method; a paragraph on why these sites were chosen and what the model does **not** solve (geographic coverage) |
| Road Network | the network drawn with ImDrawList: existing (blue square), offline (red cross), candidate (ring), new (green diamond), route and numbered stops; legend; wheel zoom, drag pan, Fit; scale bar; junction dots hidden on large networks until zoomed in; the projection is stated and synthetic graphs say so. A day-simulation mode replays every trip minute by minute with queues |
| Algorithms | name, purpose, input, output, time and space complexity and role of 26 algorithms, core workflow separate from educational ones; a button runs every algorithm on the loaded city with its independent check |
| Benchmarks | reads `benchmarks/results.csv`: algorithm, size, vertices, edges, runtime, repetitions, median, min, max, correctness; log–log charts per family. Without the file it says "No benchmark results available. Run the benchmark executable to generate measurements." **Run quick benchmark** starts `ev33_bench --quick` in the background and loads its results |
| Data Management | the loaded dataset's files and record counts; check any folder against the validation rules (missing files or columns, malformed records, duplicate ids, negative distances, invalid costs, invalid battery values, references to unknown nodes) with the exact `file:line: reason`; load a valid folder as a new dataset. Nothing is fixed silently; the hand-made sample is labelled as such |
| Settings | light or charcoal theme, UI scale, default trip method, simulation policy and map layers, folders in use, library versions |

Run it with `build/ev33_gui` (Windows: `build\ev33_gui.exe`, or `make run`). Options:

```
--data DIR --results DIR --assets DIR     folders of the cities, results.csv and fonts
--load DIR                                also load this dataset folder and open it
--view dashboard|trip|placement|network|algorithms|benchmarks|data|settings
--city small|medium|shock|large|adversarial   --select dp|bnb-lc|bnb-fifo|bnb-lifo|greedy
--route dijkstra|floyd|bellman   --budget LAKH
--trip ID [--method exact|dijkstra|floyd|bellman]   plan a dataset trip in the Trip Planner
--day [--time HH:MM] [--play] [--policy wait|divert]
--dark | --light   --size WxH   --scale F   --shot FILE.png [--frames N]   (save a screenshot and exit)
```

### 9.2 Website (`ev33_web`)

The website shows seven views:

| View | What it shows |
|---|---|
| Planner | headline figures, map of existing, offline, candidate and new stations, what-if edits (force or forbid a site, change ports), baseline comparison, trip inspector with route, battery and elevation |
| Live day | the day simulation on the map: vehicles drive, queue and charge; wait or divert policy; grid draw |
| Strategies | all 15 placement × routing pairings on identical data |
| Capacity | charging energy by hour against grid supply, busiest stations, the three max-flow checks |
| Algorithms | each decision, its algorithm, the result on this city and the independent check |
| Data model | the typed C++ records with real values and the graphs and trees built from them |
| Benchmarks | log–log scaling charts from `benchmarks/results.csv` with notes on worst cases |

**Website** — `build/ev33_web` serves <http://localhost:8033> (the `PORT` variable overrides). Pages
are HTML + SVG generated in C++ with no JavaScript; the plan travels in the address, so any view
can be bookmarked. Deployment: the `Dockerfile` builds the server, **runs all four test suites**
(a failing test stops the deploy), and copies the scenarios, benchmark results and fonts into a
small image; `render.yaml` is the Render blueprint (*New → Blueprint* on render.com). `/health`
answers `ok`.

## 10. Tests

```
ctest --test-dir build --output-on-failure        # all suites
ctest --test-dir build -L unit                    # 154 unit tests
ctest --test-dir build -L integration -V          # IT01-IT08 + regression, prints ACTUAL lines
ctest --test-dir build -L adversarial             # AD01-AD25
ctest --test-dir build -L frontend                # FE01-FE09, the engine calls of the desktop app
```

Latest run (Linux, GCC 13.3, Release): **4 / 4 suites passed** — 154 unit tests, IT01–IT08 +
regression, AD01–AD25 and FE01–FE09.

Unit tests cover each algorithm's normal, boundary and invalid cases against independent oracles
(brute force, a second algorithm, `std::sort`, textbook answers). Integration tests run complete
workflows on the hand-checked sample. Adversarial tests feed malformed and extreme input and check
for a precise rejection or a controlled result. The suites also pass under ASan + UBSan
(`-DEV33_SANITIZE=ON`). Tables with input, procedure, expected and actual results:
`docs/testing.md`.

## 11. Benchmarks

```
build/benchmarks/benchmark_data_generator --seed 12345 --size all --out data/generated
build/benchmarks/benchmark_data_generator --out data/generated --verify
build/benchmarks/ev33_bench --out benchmarks/results.csv          # ~25 s, Release build
build/benchmarks/ev33_bench --only dense                          # one family
build/benchmarks/ev33_gbench                                      # Google Benchmark
```

Datasets B1–B6 (100–50,000 vertices; connected-varied, connected-equal, disconnected) and dense
D100–D400, all from seed 12345 with checksums in `data/generated/metadata.json`. Each measurement
reports the median of repeated runs, min, max and peak heap bytes, and is validated outside the
timed region. Columns and procedure: `docs/benchmark_methodology.md`.

## 12. Algorithms

`docs/algorithms.md` has the full algorithm → file → test mapping, complexities, correctness
arguments, the energy model, both trip planners, the placement model and a selection guide.
In short: the engine never calls `std::sort`, `qsort`, `std::priority_queue` or a graph library
for assessed work; the binary heap, sorts, searches and graph algorithms are all in
`include/ev/algorithms` and `include/ev/graph`.

## 13. Results

From `ev33 run` (DP placement, Dijkstra legs) on the generated cities, and `ev33 verify`:

| City | New stations | Trips feasible | Shortest route beyond the starting charge → completed | Capacity (3 methods agree) | Grid surge |
|---|---|---|---|---|---|
| small | 2 (₹1.66 Cr of 2 Cr) | 55 / 60 | 21 → 16 | 16 trips in 60 port slots | none |
| medium | 9 (₹8.97 Cr of 9 Cr) | 1,195 / 1,200 | 463 → 458 | 250 in 264 | 18:00–19:00, 382 kWh over |
| shock | 5 (₹4.91 Cr of 4.95 Cr) | 1,869 / 1,920 | 817 → 766 | 128 in 128 | 16:00–20:00, 5,882 kWh over |
| large | 29 (₹25 Cr of 25 Cr) | 5,951 / 6,000 | 3,719 → 3,670 | 872 in 872 | 08:00–11:00, 9,736 kWh over |
| adversarial | 24 (₹6 Cr of 6 Cr) | **0 / 1,500** | 1,500 → 0 | 40 in 464 | none |

* Exact and greedy placement on medium: benefit 3,335 vs 3,305; on the sample greedy gets 140
  where the optimum is 155.
* Planner cross-check (`ev33 verify`): medium 200 of 200 sampled trips agree; large 298 of 300
  both feasible, 1 only by the fleet planner, 1 by neither, 0 only by the exact planner, and no
  fleet plan longer or with more stops than the exact plan.
* The adversarial corridor shows the placement model's blind spot: additive benefits cluster
  stations in the middle and leave the ends out of range (`docs/algorithms.md`, section 5).

## 14. Limitations and responsible use

* The energy model is simple (distance × consumption plus climb and regeneration); no speed,
  temperature, payload or charging-curve effects. Results are model outputs.
* Placement maximises additive benefit scores, not geographic coverage; a coverage-aware objective
  is future work and **not implemented**.
* The fleet planner is sound but not complete; the exact planner discretises the battery
  conservatively. `ev33 verify` measures the difference.
* All bundled data is synthetic. With real data, protect trip privacy (aggregate, anonymise),
  check that under-served areas are not ignored, and keep a safety margin in any driver-facing use
  (`docs/data.md`).
* Desktop app: built and run on Windows x64 only (cross-compiled with llvm-mingw, then
  started and checked view by view on the PC). On Linux its own sources compile cleanly with GCC 13
  (`-Wall -Wextra`, syntax check), but the full link was **not** verified because the X11/OpenGL
  development packages could not be installed on the build machine. macOS is not supported (GLFW's
  macOS backend is Objective-C and is left out).
* The map draws latitude/longitude on a flat plane; it is a schematic, not a geographic map, and
  has no background tiles. Floyd–Warshall is disabled above 1,600 junctions (its n² table).
* "Run quick benchmark" uses the quick settings (fewer sizes and repetitions); for report-quality
  numbers run `ev33_bench` from a terminal on an idle machine.

## 15. Documentation and checklist

| Document | Content |
|---|---|
| [docs/architecture.md](docs/architecture.md) | layers, data flow, error handling, build targets |
| [docs/algorithms.md](docs/algorithms.md) | algorithm → file → test table, complexity, correctness, energy model, planners, placement, selection guide, limitations |
| [docs/testing.md](docs/testing.md) | unit coverage, IT01–IT08 and AD01–AD25 tables with actual results, sanitizers |
| [docs/benchmark_methodology.md](docs/benchmark_methodology.md) | datasets, procedure, CSV columns, environment, results |
| [docs/data.md](docs/data.md) | file formats, synthetic labelling, real-world data and privacy |
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | every command with examples and outputs |
| [docs/DEMO_SCRIPT.md](docs/DEMO_SCRIPT.md) | a 15-minute demonstration |
| [docs/OWNERSHIP.md](docs/OWNERSHIP.md) | who owns which algorithm, test and benchmark |

| Requirement | Status |
|---|---|
| C++17, CMake, STL; Windows (MinGW-w64) and Linux builds | done (GCC 13.3 Linux; llvm-mingw Clang 19.1 Windows x64) |
| Assessed algorithms hand-written, no library replacements | done (`docs/algorithms.md`) |
| Domain model: nodes with lat/lon, roads, stations with status/connector/cost, candidates, vehicles with own consumption, trips | done |
| Energy-feasible planning with charging stops and clear failure reasons | done (exact + fleet planner, 7 statuses) |
| Placement under budget with exact money | done (integer paise, DP / branch and bound / greedy) |
| CSV ingestion with `file:line` validation | done |
| Unit tests, integration IT01–IT08, adversarial AD01–AD25 (GoogleTest, CTest labels) | done, all passing |
| ASan + UBSan run | done, clean |
| Benchmark datasets B1–B6, seed 12345, metadata with checksums | done |
| Benchmark CSV with median/min/max, peak bytes, validation; Google Benchmark | done (228 rows, 0 failures) |
| Documentation set | done (this README + `docs/`) |
| Synthetic data clearly labelled; no fabricated results | done |
| Native desktop app in C++17 + Dear ImGui / GLFW / OpenGL 3, no web code | done (8 views, `gui/`) |
| Desktop app uses real engine calls only, synthetic data labelled | done |
| Makefile driving CMake | done (`make test` passes on Linux) |
| Desktop app built and run on Windows | done (Windows x64, static exe, no DLLs) |
| Desktop app full build on Linux | **not verified** (sources compile with GCC 13; link needs X11/GL headers) |
| Coverage-aware placement objective | **not done** (optional; documented as future work) |
| MSVC build | **not tested** |
