# EV33 User Guide

## 1. Build

| Platform | Commands |
|---|---|
| Linux / macOS (gcc or clang) | `make` → binaries in `build/` |
| Any platform with CMake ≥ 3.16 | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build` (also builds `ev33_gui`; add `-DEV33_GUI=OFF` to skip it) |
| Windows, MSVC | Developer Prompt: `cmake -S . -B build && cmake --build build --config Release` |
| Windows, MinGW/MSYS2 | `mingw32-make` or the CMake commands above |

Extra targets: `make test`, `make bench`, `make bench-quick`, `make data`, `make demo`,
`make sanitize` (ASan + UBSan test run), `make gui` and `make run-gui` (desktop app, through
CMake), `make release TAG=v1.0.0`, `make clean`. Every source file is C (engine, tools, tests) or
C++ (desktop app).

Run every command from the repository root so relative paths such as `data/medium` resolve.

## 2. Scenario format

A scenario is a directory. Lines starting with `#` and blank lines are ignored; every CSV has a
header row.

**scenario.cfg** (`key = value`; unknown keys are an error)

| key | default | meaning |
|---|---|---|
| name | unnamed | label in reports |
| budget | 500 | installation budget (same unit as station cost) |
| battery_kwh | 30 | usable pack size |
| consumption_kwh_per_km | 0.18 | flat-road consumption |
| reserve_frac | 0.10 | never plan below this state of charge |
| target_frac | 0.80 | charge-to level at a station |
| regen_eff | 0.60 | fraction of potential energy recovered downhill |
| elev_kwh_per_m | 0.0045 | energy per metre climbed |
| speed_kmh | 40 | average speed |
| max_detour_km | 15 | eligibility radius for capacity allocation |
| grid_kw | 400 | grid supply per hour for the surge analysis (kWh/h) |
| window_min, session_min | 120, 30 | slots per port = ⌊window/session⌋ |
| horizon_h | 24 | hours in the load profile |

**nodes.csv** `id,name,x_km,y_km,elev_m,demand`
**edges.csv** `u,v,km,capacity` (undirected; `u`,`v` are node ids)
**stations.csv** `id,node,cost,ports,power_kw[,maint_period_h,maint_offset_h]`
**trips.csv** `id,src,dst,depart_min,soc0`
**events.txt** optional; one character per station event (`C` charge start, `E` end, `I` idle,
`F` fault, `O` offline, `R` reset); whitespace ignored.

Invalid data stops the program with `error: <file>:<line>: <reason>` and exit code 1.

## 3. Commands

```
ev33 run      --data DIR [--select S] [--route R] [--out DIR]
ev33 compare  --data DIR [--out DIR]
ev33 analyze  --data DIR [--out DIR]
ev33 plan     --data DIR --trip ID [--select S] [--route R]
ev33 validate --data DIR
ev33 menu     [--data DIR]
ev33 demo     [--data ROOT] [--out DIR]
S = greedy | dp | bnb-lc | bnb-fifo | bnb-lifo     (default dp)
R = dijkstra | floyd | bellman                     (default dijkstra)
```

* **run** – one strategy end to end; prints selection, service KPIs, capacity (FF / EK /
  matching), sessions and grid surge. With `--out` writes `selection.csv`, `plans.csv`,
  `summary.json`.
* **compare** – all 15 strategy combinations on the same input; `--out` writes `compare.csv`
  (Floyd is skipped above 1,600 nodes).
* **analyze** – the syllabus report: every module applied to the scenario with its sibling
  algorithm and an agreement check; `--out` writes `analysis.json`.
* **plan** – explains one trip: direct route, stops, arrival energy, charging minutes, nodes.
* **validate** – load and validate only.
* **menu** – interactive: load, choose strategies, run, compare, plan, analyse, export.
* **demo** – the demonstration: for the normal, shock and large cities it validates, runs exact
  DP + Dijkstra and greedy + Bellman-Ford with exports, and the 15-strategy matrix; then the
  analysis of the normal city and a two-stop trip on the large one. Output goes to
  `results/demo/<city>/<strategy>/`, the console transcript to `results/demo/transcript.txt`
  (`make demo` runs it).

### Output files

`plans.csv`: `trip_id,src_id,dst_id,feasible,needs_charge,km,kwh,direct_km,depart_min,arrive_min,charge_min,stops`
(`stops` = station ids separated by `;`).
`selection.csv`: `station_id,node_id,node_name,cost,ports,power_kw,value`.
`summary.json`: scenario size, strategy, selection, service KPIs, performance, capacity,
sessions (per station) and hourly load. `compare.csv` and `bench.csv` are documented in their
header rows.

## 4. Desktop app

```
make gui                 # CMake build of build/gui/ev33_gui (C++17; OpenGL; X11 headers on Linux)
./build/gui/ev33_gui     # run from the project folder
```

The app finds `data/`, `results/` and `assets/fonts/` in the working directory, next to the
program, or in the source tree it was built from; `--data`, `--results` and `--assets` override
that. Linux needs the X11 development headers to build (`libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxi-dev libgl-dev`); Windows needs only CMake with Visual Studio or MinGW.

**Layout.** The sidebar on the left holds the plan: the city, the site-selection method, the
routing method, the installation budget (the plan recomputes while you drag), a site picker and the
list of what-if edits. The tabs across the top show that plan seven ways: Planner, Live day,
Strategies, Capacity, Algorithms, Data model and Benchmarks. The line at the bottom repeats the
current plan. *Dark theme* switches colours; *Save screenshot* writes the window to a PNG in the
working directory.

**Planner.** Five headline figures (trips completed, charge-needing trips served, stations built
and their cost, average trip, engine time), the map, and the trip inspector: choose a sample trip
to replay its route on the map, read its itinerary, and see its battery along the way against the
reserve and the charge target, with elevation behind.

**Live day.** The simulation replays the day with the real port counts (`ev_sim.c`): events come
off a binary min-heap, drivers who find every port busy join that station's FIFO queue, and the
rest of their trip shifts later. Play, pause, scrub, pick a speed (real time, 1 min/s, 10 min/s,
1 h/s) or jump to the current time of day; click the grid chart to jump to an hour. When queues
spill past midnight the clock continues into day 2. The right column shows vehicles on the road,
charging and waiting, arrivals, grid draw and energy delivered, the busiest stations, and a summary
of the day.

### What-if editing

* **Pick a site.** Click a circle on the planner map, or choose it from *Edit a site*. A panel
  shows its ports and power, cost, service value, planned sessions, chargers needed, and how long
  drivers queue there in the simulated day.
* **Change it.** *Always build* forces the site in (its cost comes off the budget before the
  engine chooses the rest); *Never build* rules it out; the − / + buttons change its ports. Every
  edit re-runs the engine and appears in the sidebar; × removes one, *Clear all* removes all of
  them. On the live day, *+2 ports* next to a busy station does the same.
* **Compare.** *Pin as baseline* stores the current headline figures; after any change each
  figure shows its difference (green better, red worse), and the live-day summary compares
  waiting with the baseline. Example on the normal city: always build station 501 and never build
  522 gives 1,183 feasible trips against 1,181.
* **Export.** *Copy built sites as CSV* puts id, junction, ports, power, cost, value, sessions
  and chargers needed for every built site on the clipboard.

### Map tools

`+` / `−` / *Fit* zoom the map; the mouse wheel zooms at the pointer, drag to pan, double-click to
zoom in. *Traffic* colours each road by how many planned trips use it. Hover a station for its
details.

### Driver policy in the live day

*Drivers wait* keeps every driver at their planned station. *Drivers switch after 20 min* lets a
driver who finds every port taken and an estimated wait above 20 minutes switch once to the built
station that minimises drive time plus expected wait, provided the battery reaches it and the
next leg is still possible after charging. The summary then reports the switches and the change.

### Command-line options

```
ev33_gui [--data DIR] [--results DIR] [--assets DIR] [--page NAME] [--city NAME]
         [--select S] [--route R] [--budget N] [--edit l<id>,b<id>,p<id>=n]
         [--station ID] [--trip ID] [--traffic] [--time HH:MM] [--play] [--policy wait|divert]
         [--dark|--light] [--size WxH] [--scale F] [--shot FILE.png [--frames N]]
```

`--shot` renders the requested view, saves it as a PNG and exits, so report figures can be
regenerated with one command, for example
`ev33_gui --city shock --page live --time 19:10 --policy divert --shot shock_live.png`.

## 5. Generating data

```
ev33_gen --profile small|medium|shock|large|adversarial [--seed N] --out DIR
```

| profile | junctions | sites | trips | purpose |
|---|---|---|---|---|
| small | 36 | 12 | 60 | hand-checkable |
| medium | 256 | 60 | 1,200 | normal operation |
| shock | 256 | 60 | 1,920 | budget −45 %, 30 % single-port outages, bridges closed, evening surge, grid −40 % |
| large | 1,024 | 220 | 6,000 | stress |
| adversarial | 400 (corridor) | 80 | 1,500 | equal costs, identical departure times, fault-burst log, 600 km corridor |

The same seed always yields the same files.

## 6. Benchmarks

```
ev33_bench [--quick] [--out results/bench.csv] [--data-root data] [--only FAMILY]
families: sort search dnc greedy graph knapsack dp backtrack flow strings pipeline
```

Each row: `family,algorithm,dataset,n,ms,peak_bytes,quality,check,note`. `quality` is
family-specific (comparisons, value / optimum, km, bits per symbol…); `check = 1` means the
result matched its oracle or sibling. The process exits with status 2 if any check fails. A full
run takes about 80 s; `--quick` about 5 s.

## 7. Tests

`make test` (or `./build/ev33_tests`). Groups: sorting, searching, divide & conquer, greedy,
graphs, DP, backtracking + BnB, flow, CRT + strings, hand-built 5-node scenario, 18 invalid
inputs, invariants on four shipped datasets, regression fixtures, tracked-memory balance.
After an intentional behaviour change, `./build/ev33_tests --record` rewrites
`tests/regression/*_dp_dijkstra.txt`; review the diff before committing.
