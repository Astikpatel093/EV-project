# EV33 Design Guide

## 1. Goals and constraints

* Select charging sites under an installation budget and compute **energy-feasible** trip plans.
* Every syllabus algorithm is a student implementation behind a consistent C interface, owned by
  one team member, tested against an oracle and benchmarked against a sibling algorithm.
* Portable C11 engine with no third-party libraries; the desktop app is C++17 on vendored C/C++
  libraries (Dear ImGui, ImPlot, GLFW). Deterministic results (seeded RNG, stable tie-breaks).
* Measurable service indicators: feasible-trip %, charge-needing trips served, energy, detour,
  door-to-door time, stations overloaded, grid surge window.

## 2. Architecture

```
            +-----------------------------------------------------------+
  desktop   |  gui/  C++17: engine.cpp (wrapper) map.cpp views.cpp main.cpp |
  CLI/menu  |  src/app/main.c          src/app/bench.c   tools/ev33_gen |
            +----------------------------+------------------------------+
                                         |
            +----------------------------v------------------------------+
  domain    |  ev_model   load + validate + node-id index + road graphs  |
            |  ev_plan    values -> selection -> router -> plans         |
            |             -> capacity (flow/matching) -> sessions        |
            |  ev_report  CSV / JSON export, compare, analyze            |
            +----------------------------+------------------------------+
                                         |
            +----------------------------v------------------------------+
  library   |  ev_sort  ev_dnc  ev_greedy  ev_graph  ev_dp  ev_search   |
            |  ev_flow  ev_nt   ev_mem (tracked allocator, timer, RNG)  |
            +-----------------------------------------------------------+
```

The library knows nothing about EVs: every function takes arrays, comparators or graphs. The
domain layer translates EV questions into those calls. This keeps each algorithm independently
testable and lets the benchmark runner exercise it on synthetic and adversarial inputs.

### Consistent interfaces

| Convention | Example |
|---|---|
| qsort-style generic records | `ev_merge_sort(void *base, size_t n, size_t size, ev_cmp_fn cmp)` |
| graphs in CSR with stable arc ids | `ev_graph_build(n, m, src, dst, w)`; arc id `k` = k-th input arc |
| results through out-parameters, status via return value | `int ev_bellman_ford(...)` returns `-1` on a negative cycle |
| optional instrumentation structs | `ev_bnb_stats`, `ev_match_stats`, comparison counter |
| caller owns memory, library allocates through `ev_malloc` | peak-heap measured per call |
| strategies selected by enum + name parser | `ev_sel_parse("bnb-lc")`, `ev_route_parse("bellman")` |

## 3. Data model (typed records)

| Record | Fields | Notes |
|---|---|---|
| `ev_node` | id, name, x_km, y_km, elev_m, demand | external ids arbitrary; index maps id → position |
| `ev_edge` | u, v (indices), km, capacity | undirected; expanded to 2 directed arcs |
| `ev_station` | id, node, cost, ports, power_kw, maint_period_h, maint_offset_h, value | `value` computed |
| `ev_trip` | id, src, dst, depart_min, soc0 | soc0 in (0, 1] |
| `ev_config` | budget, battery_kwh, consumption, reserve, target, regen, elevation factor, speed, detour, grid, window, session, horizon | strict validation |

Arrays hold the records; two CSR graphs share arc numbering: `g_km` (weights = km) and `g_kwh`
(weights = energy). A sorted `(id, position)` index built with the student merge sort supports
`O(log n)` binary-search lookups when parsing edges, stations and trips.

### Validation rules (rejected with `file:line: reason`)

Missing file, missing header, wrong field count, non-numeric field, duplicate node/station/trip
id, unknown node id, self-loop, `km <= 0`, negative capacity, `ports < 1`, `cost <= 0`,
`power <= 0`, maintenance offset outside `[0, period)`, `src == dst`, `soc0 ∉ (0,1]`, negative
departure, unknown config key, malformed config value, `reserve >= target`, `regen ∉ [0,1]`,
fewer than 2 nodes or 1 road. Eighteen of these are exercised by `tests/test_main.c`.

## 4. Energy model

For an arc u→v of length `d` km and elevation change `Δz` m:

```
E(u→v) = d · c  + Δz · k            if Δz > 0   (climb)
       = d · c  + Δz · k · η        if Δz ≤ 0   (descent, regenerative braking, η = regen_eff)
```

`E` can be **negative** on steep descents (the generated maps contain an escarpment with a single
low pass). Around any closed cycle, total climb equals total descent, so the cycle energy is
`Σd·c + k·climb·(1-η) ≥ 0` whenever `η ≤ 1`: there are **no negative cycles**, which makes
Bellman-Ford (and Floyd-Warshall) correct on the energy graph, while Dijkstra is not.

## 5. Station selection

1. **Service value.** One Dijkstra tree per distinct trip origin; every node on a trip's
   shortest path gains weight 1, or 4 if the trip cannot be completed on its initial charge.
   A candidate's value is the weight of its node.
2. **Selection** is a 0/1 knapsack: item = candidate, weight = cost, value = service value,
   capacity = budget. Strategies: greedy value/cost ratio; exact DP `Θ(n·W)`; exact
   branch-and-bound with fractional (Dantzig) bound, greedy incumbent and FIFO, LIFO or
   least-cost (best-bound max-heap) frontier. Zero-value sites are never built.
3. The additive value is a **proxy**. The real objective (number of trips made feasible) is a
   coverage problem with complementarities; the planner therefore re-evaluates every selection.
   The adversarial corridor dataset shows the proxy's blind spot (see the complexity report).

## 6. Trip planning: the charging-leg graph

For a trip with selected stations `S`, build a complete directed graph on `K = |S| + 2`
vertices: origin, each station, destination. Arc `a → b` exists iff the road leg `a → b`
(found by the routing strategy) uses no more energy than is usable at `a`:

* at the origin: `(soc0 − reserve) · B`
* at a station: `(target − reserve) · B` (vehicle is charged to `target`)

A dense `O(K²)` Dijkstra over this graph minimises total km (with a 0.001 km per-stop
tie-break that prefers fewer stops). The plan is then simulated leg by leg to compute arrival
energy, charging time `(target·B − arrival)/power`, and arrival time. Every arrival respects the
reserve by construction; `tests` verify it for every trip of every dataset.

Routing strategies (all produce legs for the leg graph):

| Strategy | Path per leg | Precomputation | Use when |
|---|---|---|---|
| `dijkstra` | km-shortest | lazy tree per source node (cached) | default; non-negative weights |
| `floyd` | km-shortest | all pairs `Θ(V³)` + `next` table | small dense maps; heavy reuse |
| `bellman` | energy-minimal (negative arcs allowed) | lazy tree per source (early-exit BF) | regeneration matters; energy objective |

Station-to-station legs are computed once per run (`S²`); per trip only origin→station and
station→destination legs are looked up from cached trees.

## 7. Capacity allocation

Charge-needing trips (plans with ≥1 stop, or infeasible trips that needed one) form the left
side; each selected station offers `ports · ⌊window/session⌋` slots. A trip may use a station
reachable on its initial charge whose detour is within `max_detour_km` (or its planned first
stop). Max-flow `s → trip (1) → station (1) → t (slots)` is solved by Ford-Fulkerson (DFS) and
Edmonds-Karp (BFS); the same instance expanded to individual port-slots is solved as bipartite
matching. All three values must agree (checked in tests and printed as `[consistent]`). A
saturated station with the largest eligible demand is reported as the bottleneck.

## 8. Sessions and grid surge

Each planned stop becomes a half-open interval `[arrival, arrival + max(charge, 5 min))` at its
station. Interval partitioning yields chargers needed (equal to the maximum overlap, verified);
repeated activity selection over the real port count gives sessions servable. Charging energy
is binned by hour; maximum subarray of `(load − grid supply)` gives the worst consecutive
overload window.

## 9. Operations analytics (`ev33 analyze`)

Prim and Kruskal on the station-to-station distance matrix (cable backbone), Held-Karp and brute
force for the maintenance van tour, CRT for simultaneous maintenance windows, celebrity for the
overflow hub (every operator forwards to the best-equipped station), optimal BST for station
lookup by id weighted by service value, Huffman on the event log, four string matchers for
fault patterns, LCS between planned route and a simulated GPS trace, Strassen vs classical for
zone-to-zone demand propagation, matrix-chain order for projection pipelines, fast power for
battery fade.

## 10. Day simulation and the desktop app

`ev_sim.c` is a discrete-event simulation of the planned day with the installed ports. Each
vehicle is a feasible plan; events (arrive at stop k, finish charging at stop k) sit in a binary
min-heap ordered by time with a sequence tie-break, and each station keeps a FIFO queue of
drivers waiting for a port. When a port frees, the head of the queue starts charging and its
remaining legs shift later. Output is a gap-free drive / wait / charge timeline per vehicle plus
per-station sessions, waits and longest queue: O(E log E) for E events (7,242 events, 7 ms on the
large city). Tests check that occupancy never exceeds ports, timelines are contiguous, waits add
up, and that unlimited ports remove every wait.

**Driver policy (`ev_simulate_ex`).** With `EV_POLICY_DIVERT`, a driver who arrives to find
every port busy estimates the wait as the time until the earliest port frees plus the queue
length × mean session. Above `divert_after_min` (20) it evaluates every built station s: the leg
from the current junction must be reachable with energy − kWh ≥ reserve, and the next leg from s
must fit in battery − reserve after charging. It picks the s minimising drive + estimated wait,
if that beats staying; at most one switch per planned stop, so the loop terminates. Legs come from
`ev_router_leg`, so the cost is O(S) leg look-ups per diversion check. Tests assert the same
invariants as for waiting plus `diverted ≤ stops`.

**What-if constraints (`ev_select_ex`).** `ev_constraints` carries locked and banned station
indices. Selection zeroes both sets' values for the solver (so it never picks them), lowers the
budget by the locked costs, solves, then merges the locked sites back in sorted order;
`over_budget` is set if the locked sites alone exceed the budget. Port edits are applied to the
scenario before the run and restored after, so a cached run is keyed by (city, strategy, budget,
normalised edit string).

**Desktop app (`gui/`, C++17).** `engine.cpp` wraps the C library in an `Engine` object that
holds the five cities, the current `Plan` (city, selection, routing, budget, `Edits`) and the
results drawn by the views: a `Run` (selection, router, plans, capacity, sessions, traffic per road,
sample trips), a `SimView` per driver policy, a replayed `TripDetail`, the strategy `Compare`, the
`Analysis` (the text and JSON that `ev_analyze_fp` writes, read by the small parser in `json.h`) and
the benchmark rows. The run is recomputed whenever the plan differs from the one it was computed
for; plan changes from the widgets are queued and applied at the start of the next frame, so every
pointer into the current run stays valid while a frame is drawn. The comparison runs one strategy
per frame and the report runs a frame after it is requested, so the window never freezes. The map
(`map.cpp`) draws an inverse-distance elevation grid and 50 m contours (marching squares), roads,
the traffic layer, stations with their edits, the replayed route, and in the live day every vehicle
and each station's ports in use and queue. `SimView::at(T)` turns the simulation's drive / wait /
charge segments into positions at time T by interpolating along each vehicle's path with a binary
search over cumulative distance. Everything runs on the UI thread; nothing else touches the C
library.

## 11. Cross-cutting concerns

* **Memory accounting.** `ev_malloc` stores the size in a header; `ev_mem_peak()` gives the peak
  live heap since `ev_mem_reset_peak()`. Tests assert that every tracked byte is released.
* **Determinism.** xorshift64* RNG with explicit seeds; comparators break ties by index.
* **Stack safety.** Quick sort loops on the larger partition (O(log n) stack even in its O(n²)
  case); Ford-Fulkerson's DFS is iterative; Bellman-Ford exits early.
* **Overflow safety.** CRT uses a portable binary `mulmod` and refuses lcm > 2⁶²; `powmod`
  requires m < 2³²; knapsack and BnB validate inputs.
* **Portability.** C11 only; `timespec_get` for timing; `_mkdir` on Windows; tested with gcc 13,
  clang, CMake + CTest, AddressSanitizer and UndefinedBehaviorSanitizer.
