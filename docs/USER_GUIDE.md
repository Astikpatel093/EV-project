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

## Website and desktop app

* `build/ev33_web` then open <http://localhost:8033>. Options: `--port N` (or `PORT`),
  `--data DIR` (default `data/scenarios`), `--results DIR` (default `benchmarks`), `--assets DIR`.
* `build/ev33_gui` opens the desktop app. `--help` lists the options; for example
  `build/ev33_gui --city shock --page live --time 19:10 --policy divert --shot shock.png` saves
  that view to a PNG and exits.

In both, the sidebar holds the plan (city, placement method, routing method, budget). On the
planner map, click a site to force it in, rule it out or change its ports; existing stations
(green squares; red outline when offline) can only have their ports changed. "Pin as baseline"
keeps the current figures so later changes show their difference.
