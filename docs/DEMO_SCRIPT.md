# Demonstration script (about 15 minutes)

Before: build Release and run the tests (`ctest --test-dir build`, last line
`100% tests passed, 0 tests failed out of 3`). `build/ev33 demo --data data/scenarios --out out/demo`
runs the whole script below and saves `out/demo/transcript.txt`; keep it open as a fallback.
All data is synthetic — say so when you start.

## 0. The problem (1 min)
A city has a budget for new charging stations and thousands of EV trips. Each trip must keep its
battery above the vehicle's reserve. EV33 chooses the stations, plans every trip, and explains
every trip it cannot plan.

## 1. Data and validation (1 min)
```
build/ev33 validate --data data/sample
build/ev33 validate --data data/scenarios/medium
```
Typed records (nodes with lat/lon and elevation, roads, existing stations with status and
connector, candidate sites with exact costs, vehicles with their own battery and consumption,
trips). Show one `file:line: reason` error from `docs/testing.md` (IT05).

## 2. The hand-checked sample (3 min)
```
build/ev33 plan --data data/sample --trip 1 --method exact     # shortest route is NOT energy-feasible
build/ev33 plan --data data/sample --trip 2 --method both      # one charging stop
build/ev33 place --data data/sample --place greedy             # 140: greedy is not optimal
build/ev33 place --data data/sample --place dp                 # 155: the optimum
```
Point to `data/sample/README.md`, where every number was worked out by hand.

## 3. Normal operation (3 min)
```
build/ev33 run --data data/scenarios/medium --place dp --route dijkstra
build/ev33 run --data data/scenarios/medium --place greedy --route bellman
```
* DP: 9 new stations, benefit 3,335 (optimal); greedy 3,305.
* 1,195 of 1,200 trips feasible.
* Capacity: Ford–Fulkerson = Edmonds–Karp = min cut = matching = 250 — four independent answers agree.
* Bellman–Ford routes on energy (regenerative braking makes negative arcs) and uses slightly less energy per trip.

## 4. Constraint shock (2 min)
```
build/ev33 run --data data/scenarios/shock
```
Budget −45 %, closed bridges, port outages, an offline station, evening demand +60 %:
1,869 of 1,920 feasible, but only 128 port slots for 817 charge-needing trips, and a grid surge
from 16:00 to 20:00.

## 5. Scale and the model's blind spot (2 min)
```
build/ev33 run --data data/scenarios/large          # 6,000 trips in about 3 s
build/ev33 run --data data/scenarios/adversarial    # 0 of 1,500 feasible
build/ev33 verify --data data/scenarios/large --samples 300
```
The corridor: the optimal knapsack clusters stations where benefit scores are high and leaves the
ends out of range — the placement objective is additive, not coverage-based (stated limitation).
`verify` shows the fast planner agrees with the exact planner.

## 6. Website or desktop app (2 min)
Open the live site or `build/ev33_web`: Planner (force or forbid a site, watch the figures
change), Live day (queues at 18:00), Strategies (15 pairings), Benchmarks (log–log curves and
worst cases).

## 7. Evidence (1 min)
`ctest -L integration -V` for the ACTUAL lines; `benchmarks/results.csv` (228 validated rows);
`docs/algorithms.md` for the algorithm → file → test table.
