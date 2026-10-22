# Architecture

EV33 is one C++17 code base with a single engine library (`ev33_core`) and five programs on top
of it. Nothing outside the C++ standard library is used by the engine; GoogleTest and Google
Benchmark are used only by the test and micro-benchmark programs, and Dear ImGui, GLFW and OpenGL 3
only by the optional desktop app. All of them except OpenGL (supplied by the graphics driver) are
vendored in `third_party/`.

```
                    data/*.csv, scenario.cfg                     benchmarks/results.csv
                               │                                          │
                ┌──────────────▼──────────────┐                           │
                │  io        CSV reader, typed │  DataError("file:line:    │
                │            scenario loader   │  reason") on bad input    │
                └──────────────┬──────────────┘                           │
                               │ ev::Scenario                             │
   ┌───────────────────────────▼────────────────────────────────┐        │
   │ models      RoadNetwork, ChargingStation, CandidateStation,  │        │
   │             ElectricVehicle, TripRequest, ScenarioConfig     │        │
   ├────────────────────────────────────────────────────────────┤        │
   │ planner     EnergyModel → Router (Dijkstra / Floyd / BF)     │        │
   │             ExactPlanner (battery states) · LegPlanner       │        │
   │ placement   benefits, knapsack DP / branch and bound / greedy│        │
   │ operations  capacity (max flow), sessions, day simulation    │        │
   │ analysis    PlanningRun pipeline, reports, comparison        │        │
   ├────────────────────────────────────────────────────────────┤        │
   │ algorithms  sorting, searching, D&C, greedy, DP, backtracking│        │
   │ graph       Graph, shortest paths, spanning trees, max flow  │        │
   │ core        Paise money type, Rng, Stopwatch, DataError      │        │
   └───────┬──────────────┬──────────────┬──────────────┬────────┘        │
           │              │              │              │                 │
      ev33 (CLI)   tests (gtest)   ev33_bench      shared/ (adapter) ◄────┘
                                   ev33_gbench       │          │
                                   generators     ev33_web   ev33_gui
                                                  (website)  (desktop)
```

## Layers and rules

| Layer | Folder | Depends on | Responsibility |
|---|---|---|---|
| core | `include/ev/core`, `src/core` | std only | exact money (`Paise` = int64 paise), deterministic `Rng` (xorshift64*), `Stopwatch`, `DataError` |
| algorithms | `include/ev/algorithms`, `src/algorithms` | core | every assessed algorithm, hand-written and generic (templates or plain vectors); no domain types |
| graph | `include/ev/graph`, `src/graph` | algorithms | `Graph` (adjacency lists with edge ids), shortest paths, spanning trees, flow networks |
| models | `include/ev/models`, `src/models` | core, graph | domain records and their validation; `RoadNetwork` owns the id → position index |
| io | `include/ev/io`, `src/io` | models | CSV parsing by column name, scenario loading and writing, JSON writer |
| planner | `include/ev/planner`, `src/planner` | models, graph | energy model, router, the two trip planners, plan construction and explanations |
| placement | `include/ev/placement`, `src/placement` | models, algorithms | benefit estimation and the budgeted selection of candidate sites |
| operations | `include/ev/operations`, `src/operations` | analysis pipeline | capacity, charging sessions, discrete-event day simulation |
| analysis | `include/ev/analysis`, `src/analysis` | everything above | `PlanningRun` (the one code path every front end uses), reports, comparison, demos |
| datagen | `include/ev/datagen`, `src/datagen` | models, core | synthetic scenarios and benchmark datasets from a seed |
| runner / UI | `src/app`, `tools`, `benchmarks`, `shared`, `server`, `gui` | analysis | command line, generators, benchmark harness, website, desktop app |

Rules the code follows:

* **Domain logic never prints.** Algorithms and planners return values (`TripPlan`,
  `PlacementResult`, …); only `analysis/report.cpp` and the front ends format text.
* **Validation happens at the boundary.** Loading a scenario either returns a fully validated
  `Scenario` or throws `DataError` naming `file:line: reason`; the application keeps its previous
  valid state (integration test IT05).
* **Positions, not ids, inside algorithms.** Files use stable ids; `RoadNetwork::index_of` maps
  each id to a dense position once, so graph algorithms work on `0..n-1`.
* **One pipeline.** `ev::PlanningRun` (benefits → placement → charger sites → router → plan every
  trip → KPIs) is used by the CLI, the tests, the benchmarks, the website and the desktop app, so
  they cannot disagree.
* **Determinism.** No wall-clock or `rand()` in results; generators use `ev::Rng` seeded explicitly
  and a portable `exp/log/sin/cos` (`datagen/portable_math.hpp`) so a seed produces the same files
  on Linux and Windows. Ties are broken by input order everywhere (stable merge sort, heap
  sequence numbers).

## Main data flow (one planning run)

1. `load_scenario(dir)` reads `scenario.cfg` and the CSV files, checks every field and every
   cross-reference, and builds the `RoadNetwork` index.
2. `ScenarioContext` builds what does not depend on the plan: the `EnergyModel` (two directed arcs
   per road with travel time and reference energy) and the candidate benefits.
3. `place_stations` chooses candidates under the budget (DP, branch and bound or greedy) and
   honours what-if locks and bans.
4. `make_charger_sites` merges existing stations (offline ones marked unusable) with the built
   candidates; what-if port counts are applied here.
5. A `Router` (Dijkstra, Floyd–Warshall or Bellman–Ford) answers road legs; the `LegPlanner`
   plans every trip on the charging-leg graph. The `ExactPlanner` answers single trips exactly and
   cross-checks the fleet planner (`ev33 verify`).
6. Operations analyses read the run: `analyze_capacity` (three max-flow methods that must agree),
   `analyze_sessions` (interval partitioning, activity selection, maximum-subarray surge) and
   `simulate_day` (event heap, FIFO queues, wait or divert policy).

## Front ends

* **`ev33`** (`src/app`) — command-line runner with a menu mode; exit code 0 success, 1 data or
  usage error, 2 internal error.
* **`ev33_web`** (`server/web.cpp`) — a small single-threaded HTTP server that renders every page
  as HTML + SVG (no JavaScript). The strategy comparison (up to 15 full runs per city) is computed
  at start-up by a worker thread with its own engine instance; the request thread only copies its
  finished rows under a mutex.
* **`ev33_gui`** (`gui/`) — the native desktop app *EV33 Charge Atlas* (Dear ImGui on GLFW +
  OpenGL 3, eight views). It is a separate deployment target from the website: an executable that
  runs on the user's computer, with no HTML, CSS or JavaScript.
* Both share `shared/engine.*` (adapter that flattens engine results into display records and owns
  caching), `shared/content.*` (the report sentences) and `shared/fmt.*` (number formatting).

### Desktop app structure

| File | Responsibility |
|---|---|
| `gui/main.cpp` | command line, GLFW window and OpenGL context, font loading and DPI scale, sidebar, status bar, screenshots, main loop |
| `gui/ui.h`, `gui/ui.cpp` | `App` state, the two palettes (light, charcoal), fonts, shared widgets (metric, tag, segmented control, panels, rich text), PNG writer |
| `gui/charts.cpp` | line (log–log), bar and battery charts drawn with `ImDrawList` |
| `gui/network.cpp` | the road-network canvas: projection, zoom/pan/fit, markers, route, stops, legend, day-simulation drawing |
| `gui/catalog.cpp` | the algorithm catalogue (purpose, input, output, time, space, role) |
| `gui/view_*.cpp` | one file per view: dashboard, trip, placement, network, algorithms, benchmarks, data, settings |

Data flow inside the app: a view reads `Engine::run()` (a cached `PlanningRun` for the current
plan) or calls a specific engine function — `Engine::planTrip` for the Trip Planner,
`Engine::placementWith` for the method comparison, `checkDataset` for Data Management — and draws
the returned records. Views never compute domain results themselves. Long work does not freeze the
window: the method comparison does one full planning run per frame (`Engine::stepCompare`), and
*Run quick benchmark* starts a separate `ev33_bench` process from a worker thread and loads its CSV
when it exits.

## Error handling

| Situation | Behaviour |
|---|---|
| malformed or inconsistent data | `DataError` with `file:line: reason`; nothing partially loaded is kept |
| invalid argument to an algorithm (negative Dijkstra weight, bad matrix sizes, empty Huffman input) | `std::invalid_argument` / `std::out_of_range` with a message |
| arithmetic that would overflow (path sums, knapsack costs, matrix-chain products) | `std::overflow_error`, saturation, or an explicit input limit — never a silent wrap-around |
| a trip that cannot be done | not an error: `TripPlan::status` is one of six reasons and `explanation` says why in words |
| internal invariant broken (for example a placement over budget) | `std::logic_error`; the CLI exits with code 2 |

## Build targets

| Target | Kind | Built when |
|---|---|---|
| `ev33_core` | static library | always |
| `ev33`, `ev33_gen` | programs | always |
| `ev33_unit_tests`, `ev33_integration_tests`, `ev33_adversarial_tests` | GoogleTest programs | `EV33_BUILD_TESTS=ON` (default) |
| `ev33_bench`, `benchmark_data_generator` | programs | `EV33_BUILD_BENCHMARKS=ON` (default) |
| `ev33_gbench` | Google Benchmark program | `EV33_BUILD_GBENCH=ON` (default) |
| `ev33_shared`, `ev33_web` | library, program | `EV33_BUILD_WEB=ON` (default) |
| `imgui`, `glfw` | static libraries | `EV33_GUI=ON` |
| `ev33_gui` | program | `EV33_GUI=ON` (Windows, or Linux with X11/GL headers) |

The root `Makefile` is a thin wrapper: every target (`make`, `make gui`, `make test`, …) runs the
corresponding `cmake` / `ctest` command, so CMake stays the single build description.

`EV33_SANITIZE=ON` adds AddressSanitizer and UndefinedBehaviorSanitizer to every target.
