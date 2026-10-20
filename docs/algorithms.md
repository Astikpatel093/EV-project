# Algorithms

Every algorithm below is implemented by hand in `include/ev/algorithms`, `include/ev/graph` and
their `src/` counterparts. The engine never calls `std::sort`, `qsort`, `std::priority_queue`,
`std::lower_bound` or any graph library for the work being assessed; `std::sort` appears only in
the tests, as an independent oracle. Complexities use n = items, V = vertices, E = edges.

## 1. Mapping: algorithm → file → tests → where EV33 uses it

| # | Algorithm | Implementation | Tests | Used for |
|---|---|---|---|---|
| 1 | Merge sort (stable) | `algorithms/sorting.hpp` `merge_sort` | `Sorting.*`, IT07, AD16 | Kruskal's edge order, cost index, dispatch queue, benchmark medians |
| 2 | Quick sort (median-of-3 / first / random pivot, explicit stack) | `algorithms/sorting.hpp` `quick_sort` | `Sorting.*`, `QuickSortWorstCaseDoesNotOverflowTheStack`, AD16 | id index in IT07; pivot-rule comparison in the benchmarks |
| 3 | Binary search (iterative, recursive, lower/upper bound) | `algorithms/searching.hpp` | `BinarySearch.*`, IT07 | junction ids, candidates under a cost ceiling |
| 4 | Maximum subarray (divide and conquer; Kadane and brute force as oracles) | `src/algorithms/divide_conquer.cpp` | `MaxSubarray.*` | worst grid-overload window (`analyze_sessions`) |
| 5 | Fast power, modular power, celebrity problem | `src/algorithms/divide_conquer.cpp` | `FastPower.*`, `Celebrity.*` | battery fade projection, overflow hub (analysis report, illustrative) |
| 6 | Classical and Strassen matrix multiplication | `src/algorithms/divide_conquer.cpp` | `Matrix.*`, AD23, AD24 | zone-to-zone demand propagation (analysis report, illustrative) |
| 7 | Activity selection | `src/algorithms/greedy.cpp` | `ActivitySelection.*` | sessions servable with the installed ports |
| 8 | Interval partitioning | `src/algorithms/greedy.cpp` | `IntervalPartitioning.*` | bays each station needs |
| 9 | Huffman coding | `src/algorithms/greedy.cpp` | `Huffman.*`, AD16, AD25 | station event-log compression |
| 10 | Dijkstra (own binary heap) | `src/graph/shortest_paths.cpp`, `algorithms/heap.hpp` | `Dijkstra.*`, `ShortestPaths.*`, AD06, AD12, AD14, AD15 | km-shortest road legs; the exact planner's state search; the leg graph |
| 11 | Bellman–Ford (negative arcs, cycle detection) | `src/graph/shortest_paths.cpp` | `BellmanFord.*`, `ShortestPaths.*` | energy-minimal legs (regenerative braking gives negative arcs) |
| 12 | Floyd–Warshall | `src/graph/shortest_paths.cpp` | `ShortestPaths.*` | all-pairs routing strategy (≤ 1,600 junctions) |
| 13 | Prim (heap) and Prim (dense O(V²)) | `src/graph/spanning_tree.cpp` | `SpanningTree.*`, IT06 | cable backbone between charger sites |
| 14 | Kruskal with union–find | `src/graph/spanning_tree.cpp` | `SpanningTree.*`, `DisjointSet.*`, IT06 | same backbone, cross-check of Prim |
| 15 | 0/1 knapsack (dynamic programming) | `src/algorithms/dynamic_programming.cpp`, `src/placement/placement.cpp` | `Knapsack.*`, `Placement.*`, IT04 | **station placement under the budget** |
| 16 | Branch and bound (FIFO, LIFO, least cost) | `src/algorithms/branch_and_bound.cpp` | `BranchAndBound.*`, `Placement.*` | exact placement when the DP table would be too large |
| 17 | Greedy benefit/cost ratio | `src/placement/placement.cpp` | `Placement.GreedyIsNotOptimalHere` | fast placement heuristic (not optimal) |
| 18 | Matrix-chain order, LCS, optimal BST, Held–Karp TSP | `src/algorithms/dynamic_programming.cpp` | `MatrixChain.*`, `Lcs.*`, `OptimalBst.*`, `Tsp.*` | projection pipeline, route adherence, station-id index, maintenance tour (analysis report) |
| 19 | N-Queens, subset sum (backtracking) | `src/algorithms/backtracking.cpp` | `NQueens.*`, `SubsetSum.*` | backtracking validation; spending the leftover budget exactly |
| 20 | Ford–Fulkerson (DFS), Edmonds–Karp (BFS), min cut, bipartite matching | `src/graph/max_flow.cpp` | `MaxFlow.*`, `BipartiteMatching.*` | **charge-needing trips vs. port slots** (three methods must agree) |
| 21 | GCD, extended Euclid, modular inverse, Chinese remainder theorem | `src/algorithms/number_theory.cpp` | `NumberTheory.*`, `Crt.*` | when maintenance windows of busy stations coincide |
| 22 | Naive, Rabin–Karp, finite automaton, KMP string matching | `src/algorithms/string_matching.cpp` | `StringMatching.*`, AD16 | fault patterns in the station event log |
| — | Energy-feasible trip planning (state-space Dijkstra) | `src/planner/exact_planner.cpp` | `SampleFixture.*`, IT01–IT03, IT08, AD14, AD15, AD20–AD22 | exact answer for one trip; verifies the fleet planner |
| — | Charging-leg graph planner | `src/planner/leg_planner.cpp` | `LegPlannerIsSoundButCanMissRoutes`, `CapacityMethodsAgree` | planning thousands of trips per run |
| — | Discrete-event simulation | `src/operations/simulation.cpp` | `SimulationInvariantsBothPolicies`, `UnlimitedPortsRemoveAllWaiting` | queues and delays with the real port counts |

Rows marked *illustrative* run on real scenario data in `ev33 analyze` to show the algorithm, but
the core placement and routing workflow does not depend on them.

## 2. Complexity and correctness

| Algorithm | Time | Extra space | Why it is correct (sketch) | Checked against |
|---|---|---|---|---|
| Merge sort | Θ(n log n); Θ(n) comparisons on sorted input (one check per merge) | Θ(n) | merging two sorted halves keeps order; `<=` takes the left element first, so it is stable | `std::sort`, stability on station records |
| Quick sort | expected Θ(n log n); worst Θ(n²) for the first-element pivot on sorted input | O(log n) stack (smaller part first) | partition invariant: left ≤ pivot ≤ right | `std::sort`, all four pivot/input combinations |
| Binary search | Θ(log n) | O(1) iterative, O(log n) recursive | interval `[lo, hi)` always contains the first match if any | linear scan on every key |
| Max subarray (D&C) | Θ(n log n) | O(log n) | best run is left, right, or crossing the middle | Kadane Θ(n), brute force Θ(n²) |
| Fast power | Θ(log e) multiplications | O(1) | xᵉ = (x^{e/2})² · x^{e mod 2} | repeated multiplication, `std::pow` |
| Strassen | Θ(n^{log₂7}) ≈ n^2.81, classical below the cutoff | Θ(n²) | seven products recombine to the four quadrants | classical Θ(n³) on many sizes |
| Celebrity | Θ(n) queries | O(1) | each "knows" query eliminates one candidate | Θ(n²) pairwise search |
| Activity selection | Θ(n log n) (sort by end) | Θ(n) | earliest finishing session can always start an optimal schedule | exhaustive search on small inputs |
| Interval partitioning | Θ(n log n) | Θ(n) | bays used = maximum overlap (depth), a lower bound for any schedule | depth computed separately |
| Huffman | Θ(k log k) for k symbols | Θ(k) | merging the two rarest symbols is always safe | textbook frequencies (224 bits), prefix-free check, round trip |
| Dijkstra (binary heap) | O((V + E) log V) | Θ(V) | with non-negative weights, the settled vertex has its final distance | optimality certificate (every edge relaxed, every tree edge tight), Bellman–Ford, Floyd |
| Bellman–Ford | O(V·E), stops early when a round changes nothing | Θ(V) | after k rounds, all paths with ≤ k edges are optimal; a change in round V means a negative cycle | Dijkstra on non-negative graphs, textbook negative arcs |
| Floyd–Warshall | Θ(V³) | Θ(V²) | after step k, paths using intermediates ≤ k are optimal | Dijkstra from every source |
| Prim (heap) / dense | O(E log V) / Θ(V²) | Θ(V) | cut property: the lightest edge leaving the tree is safe | Kruskal |
| Kruskal + union–find | O(E log E) | Θ(V) | cut property; union by rank + path compression | Prim |
| 0/1 knapsack DP | Θ(n · W) with W = budget / g, g = gcd of budget and costs | Θ(n · W) bits for the choices | optimal substructure over (item, remaining budget) | brute force ≤ 20 items; branch and bound |
| Branch and bound | exponential worst case; pruned by the fractional-knapsack bound | frontier size | a node is cut only if its upper bound cannot beat the incumbent | DP optimum on random instances |
| Greedy ratio | Θ(n log n) | Θ(n) | **not optimal** (sample: 140 vs 155) | DP |
| Matrix chain | Θ(n³) | Θ(n²) | optimal split for each sub-chain | left-to-right cost, textbook example |
| LCS | Θ(m · n) | Θ(m · n) | standard recurrence | textbook strings |
| Optimal BST | Θ(n³) | Θ(n²) | optimal root for each key range | balanced tree cost |
| Held–Karp | Θ(2ⁿ · n²), n ≤ 20 | Θ(2ⁿ · n) | best path for each (subset, last city) | brute force ≤ 11 cities |
| N-Queens / subset sum | exponential, pruned | O(n) | backtracking explores only consistent partial solutions | known counts (8 → 92); subset-sum DP |
| Ford–Fulkerson (DFS) | O(E · f) | Θ(V + E) | max-flow min-cut theorem | Edmonds–Karp, min-cut capacity, bipartite matching |
| Edmonds–Karp (BFS) | O(V · E²) | Θ(V + E) | shortest augmenting paths bound the number of rounds | Ford–Fulkerson, matching |
| Bipartite matching (Kuhn, iterative) | O(V · E) | Θ(V) | augmenting paths (Berge) | max flow |
| Extended Euclid, CRT | O(log m) per pair | O(1) | solutions combine pairwise with lcm; inconsistent systems are reported | random systems verified by substitution |
| KMP / automaton / Rabin–Karp / naive | Θ(n + m) / Θ(n + m·σ) / expected Θ(n + m) / Θ(n·m) | Θ(m) | prefix function / automaton states / hash then verify | all four agree on random logs |
| Exact planner | O(S log S), S = V × (capacity / step) states | Θ(S) | Dijkstra on a finite state graph with non-negative costs; consumption rounded up, regeneration down, so every returned plan is truly feasible | hand-computed sample trips; re-simulation in exact arithmetic |
| Leg planner | per trip O(K²) on K = sites + 2, plus cached legs (two candidate paths each) | Θ(K²) cache | every returned plan is re-simulated in exact arithmetic (sound); not complete | exact planner (`ev33 verify`) |

## 3. Energy model

For each directed road arc:

```
E_ref = flat + climb
  flat  = energy_kwh from roads.csv if given, else distance_km × reference_consumption
  climb = Δh × climb_kwh_per_m                     uphill  (Δh > 0)
        = Δh × climb_kwh_per_m × regen_efficiency  downhill (Δh < 0, recovered energy)
E_vehicle = E_ref × (vehicle consumption / reference_consumption)
```

* On flat roads this is exactly *distance × consumption* for each vehicle.
* A downhill arc can be negative; the battery is capped at capacity (a full battery cannot store
  more). Around any closed loop the climbs equal the descents, so with regen ≤ 1 there are no
  negative cycles and Bellman–Ford is valid on the energy graph; Dijkstra is not, and it refuses
  negative weights (test AD06).
* Not modelled: speed, temperature, payload, auxiliary loads, charging curves (a stop charges at
  the lower of station and vehicle power), and partial charging (a stop fills to `target_soc`).

## 4. Trip planning

A trip request is (vehicle, origin, destination, departure time, starting charge). A plan is
feasible only if the battery stays at or above the vehicle's reserve after every road. Charging is
possible only at a site that is **available** (existing stations can be offline or in maintenance)
and whose connector matches the vehicle (`CCS2|Type2` lists are intersected).

Two planners, both returning the same `TripPlan` type:

* **ExactPlanner** — Dijkstra over states (junction, battery level). Driving moves to
  (v, b − energy) if that stays above the reserve; charging at a usable site moves to
  (u, target level) and adds a stop. Cost = (total metres, stops) compared lexicographically, so the
  answer is the shortest feasible route with the fewest stops among equals. Battery levels are
  discretised (`battery_step_kwh`, default 0.25 kWh) conservatively, so the planner may reject a
  trip whose margin is below about one step per road, but never accepts an infeasible one.
* **LegPlanner** — builds the charging-leg graph (origin, usable sites, destination; an arc when
  the vehicle can drive between the two points) and runs Dijkstra on it. Each leg is tried along
  the router's path and, if the vehicle cannot drive that one, along the energy-minimal path
  (Bellman–Ford on the reference-energy graph). Legs are cached per run and their feasibility per
  vehicle, so 6,000 trips take about 1.3 s. With only two candidate paths per leg it can still
  miss a feasible trip (unit test `LegPlannerIsSoundButCanMissRoutes` builds such a network).
  Measured with `ev33 verify`: medium 200/200 trips agree; shock 194 both feasible, 1 only by the
  leg planner, 5 by neither; large 298 of 300 both feasible, 1 only by the leg planner (margin
  below one battery step), 1 by neither; in all three, 0 trips found only by the exact planner,
  0 leg plans longer and 0 with more stops than the exact plan.

Every plan carries one of these statuses and an `explanation` sentence:

| Status | Feasible | Meaning |
|---|---|---|
| `zero-length` | yes | origin = destination |
| `direct` | yes | no charging needed |
| `with-charging` | yes | one or more stops, each with arrival time, arrival charge, charging time, departure charge |
| `no-road-connection` | no | the destination is in another road component |
| `start-below-reserve` | no | the starting charge is already below the reserve |
| `out-of-range` | no | too far, and no usable charger within reach |
| `charging-insufficient` | no | chargers are reachable but cannot close the gaps |

The plain shortest-distance route is always reported too (`shortest_km`, `shortest_is_feasible`),
so the output shows when the shortest route is **not** energy-feasible (sample trip 1).

## 5. Station placement

* Objective: maximise the sum of candidate benefit scores; constraint: total installation cost ≤
  budget, in exact integer paise (never exceeded; enforced by a `logic_error` check).
* Benefits come from `candidates.csv` when given; otherwise each trip adds 1 (or 4 if its vehicle
  cannot drive the shortest route on its starting charge) to every junction on its shortest route,
  times the optional suitability factor.
* Sites with zero benefit are never built. Locked sites are paid first; banned sites are skipped.
* DP runs on costs divided by g = gcd(budget, all costs) — whole-lakh costs give a table of
  (candidates × budget in lakh). If the table would exceed 5 × 10⁷ cells, the exact least-cost
  branch and bound is used instead and the result says so.

**Known blind spot (shown by the adversarial corridor).** Benefits are additive: two neighbouring
sites that serve the same drivers are both credited. On the 600 km corridor the optimal knapsack
solution clusters 24 stations in the busy middle stretch and leaves gaps longer than any vehicle's
range near the ends, so **0 of 1,500 trips** are feasible even though the budget is fully used.
The trip planner re-evaluates every placement, which is how this shows up. A coverage-aware
objective (for example, maximum-coverage or set-cover placement) would fix it; it is listed under
future work and is **not** implemented.

## 6. Which algorithm when (selection guide)

| Question | Use | Avoid / note |
|---|---|---|
| Shortest road distance from one place | Dijkstra | Bellman–Ford is correct but slower |
| Least energy with downhill regeneration | Bellman–Ford | Dijkstra is wrong with negative arcs (and refuses them) |
| Many queries on a small or dense network | Floyd–Warshall (measured faster than Dijkstra-from-every-source on dense graphs: 110 ms vs 269 ms at 400 junctions) | Θ(V²) memory; not offered above 1,600 junctions |
| One trip, guaranteed answer | ExactPlanner | slower (≈ 10 ms per trip on the large city) |
| Thousands of trips | LegPlanner | sound but not complete (two candidate paths per leg); verify with `ev33 verify` |
| Placement with whole-lakh costs | 0/1 knapsack DP | table size n × (budget / g) |
| Placement with awkward costs or a huge budget | least-cost branch and bound | exponential worst case; FIFO and LIFO explore more nodes |
| A quick placement estimate | greedy ratio | not optimal (sample: 140 vs 155; 99.9 % on random instances) |
| Bays a station needs | interval partitioning | — |
| Sessions servable with k bays | activity selection per bay | optimal for one bay, a heuristic for several |
| Sorting records stably | merge sort | quick sort with a first-element pivot on sorted data is Θ(n²) |

## 7. Limitations

* The energy model is a documented simplification (section 3); results are model outputs, not
  measurements.
* Placement optimises additive benefit scores, not geographic coverage (section 5).
* The fleet planner can miss feasible trips; the exact planner can reject trips with a margin
  below one battery step.
* Charging always fills to `target_soc`; queueing is only modelled by the day simulation, not by
  the planners.
* Activity selection repeated per bay is optimal for one bay only.
* All bundled scenarios are synthetic (see `docs/data.md`).
