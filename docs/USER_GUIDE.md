# User guide

All examples run from the project folder after a build (`README.md`, sections 5–6). On Windows
write `build\ev33.exe` instead of `build/ev33`. Every command takes `--data DIR`, a scenario folder
(`docs/data.md`). Output examples below are copied from real runs on the bundled synthetic data.

## validate — load and check a scenario

```
$ build/ev33 validate --data data/sample
ok: 'sample' (synthetic data) 10 nodes, 11 roads, 3 existing stations (1 unavailable), 6 candidate sites,
    3 vehicles, 8 trips, 8 booked sessions, budget INR 1,00,00,000
    source: hand-written example for tests and the viva (data/sample/README.md)
    note: the road network has 2 separate components
```

A bad file stops with one precise message and exit code 1, for example
`error: it05_case_1/roads.csv:3: 'distance_km' must be a number, got 'twenty'`.

## run — placement and every trip

```
build/ev33 run --data data/scenarios/medium [--place dp] [--route dijkstra] [--budget 90000000] [--out out/medium]
```

* `--place` greedy | dp | bnb-lc | bnb-fifo | bnb-lifo (default `dp`)
* `--route` dijkstra | floyd | bellman (default `dijkstra`; Floyd–Warshall is refused above 1,600 junctions)
* `--budget` in rupees (default: `budget_inr` from `scenario.cfg`)

It prints the placement, the trip-planning summary, the capacity check (three max-flow methods),
the sessions and the worst grid surge. With `--out` it writes:

| File | Content |
|---|---|
| `placement.csv` | `candidate_id, name, node_id, install_cost_inr, benefit, benefit_estimated, ports, power_kw, connector` |
| `plans.csv` | one row per trip: `trip_id, vehicle, from, to, status, feasible, needs_charge, distance_km, energy_kwh, shortest_km, depart_min, arrive_min, charge_min, stops, explanation` |
| `summary.json` | scenario size, strategy, placement, KPIs, capacity, sessions |

## plan — explain one trip

```
build/ev33 plan --data data/sample --trip 2 --method both
build/ev33 plan --data data/scenarios/large --from 1000 --to 8161 --vehicle V3 --charge 12 --depart 07:30
```

`--method exact` uses the battery-state planner, `leg` the fleet planner, `both` prints both.
The answer shows the status, the reason, the plain shortest route and whether it is drivable, and
each stop with arrival time, arrival charge, charging time and departure charge:

```
Trip 2 (vehicle SV1): with-charging
  Needs 1 charging stop (Junction A site): 80.0 km, 10 min charging; the shortest road route (70.0 km)
  needs 17.60 kWh but only 10.00 kWh is usable at departure.
  depart 09:00 with 12.00 kWh
  stop at Junction A site (node 2): arrive 09:24 with 8.00 kWh, charge 10 min to 16.00 kWh
  arrive 10:46 with 4.00 kWh (lowest 4.00 kWh); 80.0 km, 5 nodes
```

## place — placement only

```
build/ev33 place --data data/scenarios/adversarial --place bnb-lc --budget 60000000 --out out/adv
```

Lists the chosen candidates with cost, benefit and ports, the total, and the money left.

## schedule — booked sessions at a station

```
$ build/ev33 schedule --data data/sample
Station 101: 8 booked sessions, 2 bays
  one bay (activity selection, earliest finish first): 4 sessions can be served: ...
  every session (interval partitioning): 3 bays needed (maximum overlap 3)
  with the 2 installed bays: 6 of 8 sessions served (activity selection repeated per bay)
  -> 1 more bay(s) needed to serve every booking
```

## compare — every placement × routing pairing

`build/ev33 compare --data data/scenarios/medium --out out/cmp` prints one row per pairing
(stations, cost, benefit, optimal, placement time, search nodes, trips feasible, average km and
kWh, routing time) and writes `compare.csv`.

## analyze — every algorithm on the scenario

`build/ev33 analyze --data data/scenarios/medium --out out/an` runs sorting, searching, divide and
conquer, greedy, dynamic programming, backtracking, branch and bound, flow, number theory and
string matching on the scenario's own data, each with an independent check, and writes
`analysis.json`. Items marked "illustrative" are teaching uses outside the core workflow.

## verify — fleet planner against the exact planner

```
$ build/ev33 verify --data data/scenarios/large --samples 300
Planner cross-check on 'large' (300 trips, dijkstra legs, battery step 0.25 kWh)
  both feasible 298, exact only 0, leg only 1, neither 1
  leg plan longer than the exact plan: 0 trips (0.0 km extra in total)
  leg plan with more charging stops than the exact plan: 0 trips
```

## algorithms, demo, menu

* `build/ev33 algorithms` — every algorithm on a textbook example, with the expected answer.
* `build/ev33 demo --data data/scenarios --out out/demo` — the demonstration: medium, shock and
  large with two strategies each and the full strategy matrix, the analysis report, booked
  sessions and a two-stop trip; writes `transcript.txt` and the CSV/JSON exports (≈ 30 s).
* `build/ev33 menu --data data/scenarios/small` — interactive menu for the same commands.

## Generators

```
build/ev33_gen --profile large --seed 7 --out data/my_city      # one synthetic city
build/ev33_gen --all --seed 33 --out data/scenarios             # the five bundled cities
build/benchmarks/benchmark_data_generator --seed 12345 --size all --out data/generated
```

## Website

Start `build/ev33_web` (Windows: `build\ev33_web.exe`) from the project folder and leave it running,
then open `http://localhost:8033` in a browser on the same computer (stop the server with Ctrl+C).
The hosted copy needs no build: <https://ev-project-84u5.onrender.com>.
Options: `--port N` (or `PORT`), `--data DIR` (default `data/scenarios`), `--results DIR` (default
`benchmarks`), `--assets DIR`. The website has the same eight views as the desktop app below and works
the same way, with links and forms instead of mouse input: choose the dataset in the sidebar and press
**Open**; in the Trip Planner fill the form and press **Calculate route**; in Station Placement press
**Apply** after changing the method or budget; on the Road Network use + / − / arrows / Fit and
**Apply layers**. Everything you choose is kept in the address, so a view can be bookmarked or shared.
The website and the desktop app are deployed separately; neither replaces the other.

## Desktop app — EV33 Charge Atlas

Start `build/ev33_gui` (Windows: double-click `ev33_gui.exe`, or `make run`). It finds `data/`,
`benchmarks/` and `assets/` in the working folder, next to the program or one or two folders up.

**Sidebar.** The dataset list (the five generated cities, plus any folder you load), the
SYNTHETIC DATASET tag, the eight views (Ctrl+1 … Ctrl+8), the current plan and the
Light / Charcoal switch. The status bar shows what the engine last did and has a
**Save screenshot** button (PNG in the working folder).

**Dashboard (Ctrl+1).** Dataset figures (junctions, roads, existing stations, candidate sites,
trips) and the placement result (stations selected, budget, total cost, money left), then trip
results, the capacity check, charging energy by hour against grid supply and the busiest stations.

**Trip Planner (Ctrl+2).**
1. Optionally pick a trip from the dataset to fill the form.
2. Enter source and destination junction ids (the names appear beside them), choose an EV model or
   *Custom* and set battery (kWh), starting charge (kWh), consumption (kWh/km), reserve (kWh) and
   departure time.
3. Choose the method: *Exact (battery states)* finds the best plan the energy model allows;
   *Charging legs with Dijkstra / Floyd–Warshall / Bellman–Ford* is the fleet planner used for
   every trip in the city.
4. Press **Calculate route**. The answer says *Feasible* or *Not feasible* with the reason, then
   distance, energy, driving and charging time, stops, the lowest charge on the way, a battery
   chart, the stop table and the full junction sequence. Invalid input (unknown junction,
   starting charge above the battery, reserve above the battery, …) is reported, not corrected.

**Station Placement (Ctrl+3).** Choose the method and budget. The candidate table can be sorted
and filtered; each row can be set to *Auto*, *Always* (force in) or *Never* (rule out), and ports
can be changed with − / +. *Compare methods* runs every method on the same data (one run per
frame, so the window stays responsive) and shows which reached the optimum. *Why these sites* explains the choice; with the
greedy method it also shows how far it is from the exact optimum.

**Road Network (Ctrl+4).** Mouse wheel zooms, dragging pans, double-click zooms in, *Fit* shows
everything. Click a station for its details, ports and build decision. Layers switch terrain,
junctions, names, station kinds, the Trip Planner route and traffic. *Day simulation* replays the
day: Play / Pause, ±1 h, the time slider, speed, and the policy (*drivers wait* or *switch after
20 min*); the panel shows vehicles on the road, charging, waiting, arrived, grid draw and energy.

**Algorithms (Ctrl+5).** The catalogue of core and educational algorithms. *Run the analysis on
this city* executes each one on the loaded data and shows its independent check.

**Benchmarks (Ctrl+6).** Charts and the measurement table from `benchmarks/results.csv`.
*Run quick benchmark* starts `ev33_bench --quick` (it must be next to `ev33_gui`, in a
`benchmarks` folder beside it, or in `build/benchmarks`) in the background and writes
`results_local.csv` beside `results.csv`, which is not changed; *Show the committed results*
switches back.

**Data Management (Ctrl+7).** Type a dataset folder (or click one of the folders found) and press
**Check**: either a summary of the records or the first problem as `file:line: reason`. A folder
that passes can be opened with **Load as a new city**; a folder that fails is never loaded.

**Settings (Ctrl+8).** Theme, UI scale (automatic from the monitor or chosen), the default trip
method, simulation policy and map layers, the folders in use and the versions of Dear ImGui, GLFW,
OpenGL and the compiler.

Command-line options (`README.md`, section 9.1) open a given view directly; for example
`build/ev33_gui --city shock --view network --day --time 19:10 --policy divert --shot shock.png`
saves that view to a PNG and exits.
