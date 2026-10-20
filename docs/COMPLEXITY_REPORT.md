# EV33 Complexity Report

All measurements: `results/bench.csv` (full run, 242 rows, 0 failed checks) and
`results/demo/transcript.txt`, produced by this build on Linux x86-64, gcc 13.3 `-O2`,
single thread. Times are wall-clock milliseconds for one call; memory is the peak tracked heap
allocated by the call. Re-run with `make bench` to reproduce on your machine.

## 1. Recurrences and their solutions

| Algorithm | Recurrence | Solution (method) |
|---|---|---|
| Merge sort | T(n) = 2T(n/2) + Θ(n) | Θ(n log n) (Master, case 2) |
| Quick sort, balanced split | T(n) = 2T(n/2) + Θ(n) | Θ(n log n) |
| Quick sort, worst split (first pivot on sorted input) | T(n) = T(n−1) + Θ(n) | Θ(n²) (unrolling: Σk) |
| Randomised quick sort | E[C(n)] = (n−1) + (2/n)ΣE[C(k)] | ≈ 2n ln n ≈ 1.39 n log₂ n comparisons |
| Binary search (both) | T(n) = T(n/2) + Θ(1) | Θ(log n) (Master, case 2) |
| Maximum subarray (D&C) | T(n) = 2T(n/2) + Θ(n) | Θ(n log n) |
| Fast power | T(e) = T(e/2) + Θ(1) | Θ(log e) multiplications |
| Celebrity elimination | T(n) = T(n−1) + 1 query, then 2(n−1) to verify | ≤ 3(n−1) queries = Θ(n) |
| Classical block matmul | T(n) = 8T(n/2) + Θ(n²) | Θ(n³) (Master, case 1) |
| Strassen | T(n) = 7T(n/2) + Θ(n²) | Θ(n^log₂7) = Θ(n^2.807) (Master, case 1) |
| Matrix chain / optimal BST | m[i,j] = min over k of m[i,k] + m[k+1,j] + cost | Θ(n³) time, Θ(n²) space |
| LCS | L[i,j] = L[i−1,j−1]+1 or max(L[i−1,j], L[i,j−1]) | Θ(ab) time and space |
| 0/1 knapsack | B[i,c] = max(B[i−1,c], B[i−1,c−wᵢ] + vᵢ) | Θ(nW) (pseudo-polynomial) |
| Held-Karp | D(S,j) = min over i∈S of D(S−{j}, i) + d(i,j) | Θ(n² 2ⁿ) time, Θ(n 2ⁿ) space |
| Bellman-Ford | dₖ(v) = min(dₖ₋₁(v), min over (u,v) of dₖ₋₁(u) + w) | O(VE); O(kE) with early exit after k passes |
| Floyd-Warshall | dᵏ(i,j) = min(dᵏ⁻¹(i,j), dᵏ⁻¹(i,k) + dᵏ⁻¹(k,j)) | Θ(V³) time, Θ(V²) space |
| N-Queens / subset sum backtracking | T(n) ≤ b·T(n−1) + O(1) | O(n!) / O(2ⁿ) worst case; pruning governs practice |

## 2. Asymptotic cost against measurement

### Sorting and searching
| n / input | merge | quick median-3 | quick first-pivot | quick random | libc qsort |
|---|---|---|---|---|---|
| 1e5 random | 19.1 ms, 1.65 M cmp | 16.1 ms, 2.56 M | 17.1 ms | 16.4 ms | 12.9 ms |
| 1e6 random | 226.9 ms, 20.3 M cmp, 4 MB aux | 176.1 ms, 30.2 M, O(1) aux | — | 203.7 ms, 28.7 M | 168.6 ms |
| 2e4 pre-sorted (adversarial) | 0.17 ms, 19,999 cmp | 1.06 ms, 0.36 M | **502.6 ms, 200.03 M cmp** | 1.77 ms | 0.51 ms |

* Merge sort at 1e6: 20.3 M comparisons ≈ n log₂ n − 1.25n, as predicted; the "already ordered"
  check makes sorted input linear (19,999 comparisons).
* First-pivot quick sort on sorted input performs n²/2 = 2.0 × 10⁸ comparisons — the Θ(n²)
  recurrence exactly. Median-of-3 and random pivots avoid it. Thanks to looping on the larger
  partition its stack stays O(log n) even then.
* 1e5 queries on 1e6 keys: binary search 25–30 ms vs linear scan ≈ 33 s (extrapolated) — the
  log n vs n gap of five orders of magnitude.

### Divide and conquer
| | n | D&C | baseline |
|---|---|---|---|
| Max subarray | 1e5 → 1e6 | 3.8 → 47.2 ms (×12.3 ≈ 10·log ratio) | Kadane 0.1 → 1.2 ms (×12, linear) |
| Fast power | e = 1e7 | 46 µs for **1,000** calls | naive 326.7 ms for one call |
| Celebrity | 4,000 (planted) | 11,997 queries = 3(n−1) exactly | naive 9,996 (lucky early hit) |
| Celebrity | 4,000 (none) | 4,003 queries | naive 10,570 (worst case is n²) |
| Matmul | 256 / 512 | Strassen-16: 8.2 / 51.5 ms (×6.3) | classical 8.8 / 68.5 ms (×7.8) |
| Matmul (adversarial) | 257 | Strassen pads to 512: 52.8 ms, 12.6 MB | classical 8.3 ms, 0 extra |

Strassen's growth ratio per doubling (≈ 6.3–7) is below classical's (≈ 7.8 → 8), so it wins from
n ≈ 256 on this machine; below that, and for sizes just above a power of two, the constant
factor, the 9 temporary blocks per level and padding make classical faster. Errors stay below
5 × 10⁻¹² (numerical stability cost of the extra additions).

### Greedy
| | small | medium | large | adversarial |
|---|---|---|---|---|
| Activity selection (n = 1e3 / 1e5 / 1e6) | 0.11 ms | 19.0 ms | 243.6 ms | 1e5 identical intervals: 2.0 ms, picks 1 |
| Interval partitioning | 0.15 ms, 39 chargers | 23.7 ms, 49 | 339.9 ms, 52 | needs 100,000 chargers (= depth) |
| Huffman (build+encode+decode) | 1e4: 0.25 ms, 1.81 bits | 1e6: 24.3 ms, 1.80 bits | — | 1e6 uniform bytes: 8.0 bits (no gain) |
| Prim dense O(V²) vs Kruskal O(E log E), complete graph | V=100: 0.07 vs 0.90 ms | V=500: 1.3 vs 31.6 ms | V=1500: **11.0 vs 309.6 ms, 13.5 KB vs 36 MB** | all-equal weights: 4.0 vs 24.6 ms |

### Shortest paths (grid road graphs)
| V | Dijkstra (1 source) | Bellman-Ford (1 source) | Dijkstra × V | Floyd-Warshall |
|---|---|---|---|---|
| 100 | 0.007 ms | 0.003 ms (3 passes) | 0.55 ms | 0.77 ms, 120 KB |
| 900 | 0.058 ms | 0.032 ms (4 passes) | 49.6 ms | 444 ms, 9.7 MB |
| 3,600 | 0.238 ms | 0.190 ms (7 passes) | 944 ms | **32,946 ms, 156 MB** |
| 3,000 reverse-ordered path (adversarial) | 0.036 ms | **19.9 ms, 3,000 passes** | | |

* Floyd 900 → 3600 grows ×74 (theory ×64): Θ(V³) confirmed; it loses to V Dijkstra runs on sparse
  graphs by 9× at V = 900 and 35× at V = 3,600.
* Bellman-Ford's early exit makes it competitive on grids (passes ≈ hop diameter), but the
  adversarial ordering forces all V passes: O(VE) materialises.
* On energy weights with regeneration (`-neg` rows) Dijkstra returned a **wrong** distance at
  V = 3,600 (93.25 vs 92.44 kWh) — the reason Bellman-Ford is the energy router.

### Dynamic programming, backtracking, branch-and-bound
| | measurement |
|---|---|
| Knapsack DP n=200 | 1.6 ms, 184 KB (n·W bits table) |
| BnB n=200 uncorrelated | FIFO 708 nodes, LIFO 708, **LC 353**; all ≤ 0.07 ms |
| BnB n=40 strongly correlated (adversarial) | **FIFO 14,886 nodes, frontier 3,609, 721 KB**; LIFO 191; LC 252 |
| Greedy ratio | 99.2–100 % of optimum on benchmarks; **97.5 % (medium) and 96.4 % (shock) on real scenarios** |
| Matrix chain n=300 | 6.2 ms; left-to-right order costs 8.2× the optimum |
| LCS 8,000 × 8,000 | 285 ms, **256 MB** table (Θ(ab) space is the binding limit) |
| Optimal BST n=400 | 19.4 ms; balanced tree is 1.10× (random) to 3.15× (skewed) more probes |
| Held-Karp n=13 → 16 | 1.9 → 23.1 ms (×12.4; theory ×12.1); brute force at n=10: 1.3 ms vs HK 0.13 ms |
| N-Queens n=12 | 14,200 solutions, 856,189 nodes, 17.4 ms |
| Subset sum, impossible target (adversarial, n=26) | backtracking 6.36 M nodes, 15.5 ms vs DP 0.21 ms |

### Flow and matching
| | Kuhn matching | Ford-Fulkerson (DFS) | Edmonds-Karp (BFS) |
|---|---|---|---|
| unit capacity, 2,000 + 2,000 | 4.6 ms | 23.4 ms | 51.2 ms |
| capacities 1..1000, 600 + 600 (adversarial) | — | **170 ms, 9,207 augmentations** | **32 ms, 2,380 augmentations** |
| EV large scenario (3,594 trips, 532 slots) | 327 ms (slot-expanded) | 5.7 ms | 70.3 ms |

FF's bound O(E·|f*|) depends on capacities: harmless with unit capacities, costly with large
ones, where EK's capacity-independent O(VE²) wins.

### String matching
| | naive | Rabin-Karp | finite automaton | KMP |
|---|---|---|---|---|
| 1e7 random text, m=12 | 39.3 ms, 12.0 M cmp | 77.5 ms (hashing cost) | **27.5 ms**, 1e7 steps, 13 KB table | 39.0 ms, 11.7 M cmp |
| 1e6 × "F", pattern "F…FR", m=200 | **133.6 ms, 2.0 × 10⁸ cmp = (n−m+1)·m** | 7.3 ms | 2.8 ms, 51,200 preprocessing | **2.0 ms, 2.0 × 10⁶ cmp ≤ 2n** |

### End-to-end EV pipeline
| scenario | DP select | Dijkstra route+plan | Floyd | Bellman |
|---|---|---|---|---|
| medium (256 / 1,200 trips) | 0.07 ms | 8.2 ms, 1.26 MB | 19.5 ms | 7.7 ms, −0.6 % kWh |
| large (1,024 / 6,000 trips) | 1.0 ms | 131.7 ms, 14.8 MB | 729.8 ms, 15.0 MB | 264.2 ms, −0.1 % kWh |

## 3. Correctness evidence

`make test` runs **59,288 checks, 0 failures**, also clean under AddressSanitizer +
UndefinedBehaviorSanitizer and with clang.

| Module | Oracle / invariant |
|---|---|
| sorts | equal to libc `qsort` on 8 sizes × 4 distributions × 4 algorithms; merge stability |
| binary search | linear scan for every key in [−2, 140] incl. duplicates and empty array |
| max subarray | O(n²) brute force on 300 random arrays; reported range sums to the reported value |
| powering / powmod | `pow` and repeated multiplication |
| celebrity | planted celebrity, no-celebrity cases, D&C ≡ naive, ≤ 3n queries |
| Strassen | classical product on n ∈ {1,2,3,7,16,33,64} (non powers of two) |
| activity selection | exhaustive 2¹² subsets; selected sessions pairwise disjoint |
| interval partitioning | equals maximum overlap; no charger double-booked |
| Huffman | round trip on 34 inputs; prefix-free; H ≤ L < H+1 (Shannon bound) |
| Dijkstra / BF / Floyd | mutual agreement on 60 random graphs; Floyd paths sum to distances; negative-cycle detection; potential-reweighted negative arcs |
| Prim / Kruskal | equal weight on 40 random (possibly disconnected) graphs |
| knapsack DP | brute force on 100 instances; returned set sums to returned value within budget |
| BnB FIFO/LIFO/LC | equal to DP on 150 instances (uncorrelated, weakly and strongly correlated) |
| matrix chain / LCS / OBST | brute recursion; CLRS examples (15,125; length 4; 2.75 with root k₂) |
| Held-Karp | brute force on 40 asymmetric instances; tour is a permutation |
| N-Queens | known counts n = 1..10; first solution valid |
| subset sum | DP oracle on 100 instances |
| flow | CLRS network = 23; max-flow = min-cut capacity; conservation; DFS ≡ BFS ≡ matching |
| CRT | brute force on 300 systems incl. non-coprime and inconsistent |
| string matchers | brute force on 400 random texts (alphabets 1–3, overlaps, m = 0, m > n) |
| EV domain | hand-computed 5-node corridor (stops, kWh, charge minutes); every exact selection = DP; Floyd plans ≡ Dijkstra plans; Bellman legs never use more energy; no arrival below reserve; no plan shorter than shortest path; FF ≡ EK ≡ matching; chargers needed = overlap; zero budget; zero trips |
| inputs | 18 malformed scenarios rejected with the expected message |
| regression | KPI fingerprints of small, medium, shock and adversarial (`tests/regression/`) |
| memory | tracked live bytes return to the starting value |

## 4. Complexity-class analysis

| Problem in EV33 | Class | Consequence |
|---|---|---|
| Sorting, indexing, searching | P (Θ(n log n), Θ(log n)) | exact at any scale |
| Trip planning with charging stops (fixed stations) | **P**: shortest path in the charging-leg graph, O(S² + SSSP) | exact per trip |
| All-pairs road distances | P (Θ(V³) or V·Dijkstra) | choose by density |
| Capacity allocation, slot assignment | P (max-flow, bipartite matching) | exact |
| Charger count per station | P for interval graphs (interval partitioning = interval-graph colouring) | exact |
| MST backbone, LCS, matrix chain, OBST, Huffman, CRT, string matching | P | exact |
| **Station selection (budgeted, additive value)** | 0/1 knapsack: **weakly NP-hard**, pseudo-polynomial Θ(nW) | exact while n·W is moderate; BnB otherwise |
| **Station selection (true objective: trips made feasible)** | max-coverage with complementarities: **strongly NP-hard**; even plain max-coverage is not approximable beyond 1 − 1/e unless P = NP | proxy + planner validation; exact only via ILP/BnB at small scale |
| Spending the budget exactly | subset sum: NP-complete | backtracking with pruning; DP oracle |
| Maintenance van tour | TSP: NP-hard | Held-Karp exact to n ≈ 18 |
| N-Queens counting | exponential search (counting solutions is #P-style) | framework validation only |

## 5. Reduction mapping

1. **Subset Sum ≤ₚ 0/1 Knapsack.** Given (a₁..aₙ, t), set wᵢ = vᵢ = aᵢ, W = t; a subset sums
   to t iff the optimum value equals t. Used in the tests: `ev_subset_sum` is checked against a DP
   that is itself the knapsack recurrence.
2. **0/1 Knapsack ≤ₚ Station Selection.** For each item create a candidate on its own isolated
   junction with cost wᵢ and one trip whose path visits only that junction with weight vᵢ;
   budget W. The best selection's value equals the knapsack optimum ⇒ station selection is
   NP-hard (weakly), which justifies DP / branch-and-bound rather than a polynomial claim.
3. **Max Coverage (Set Cover) ≤ₚ coverage-based placement.** Elements = trips, sets = trips a
   site makes feasible, unit costs, budget k. Choosing k sites to maximise feasible trips is max
   coverage ⇒ the real objective is strongly NP-hard; the additive knapsack is a tractable proxy
   (see the adversarial finding below).
4. **Hamiltonian Cycle ≤ₚ Maintenance TSP.** Distance 1 for road links, 2 otherwise; a tour of
   length n exists iff a Hamiltonian cycle exists ⇒ exact tours only for small depots (Held-Karp).
5. **Bipartite matching ≤ Max-flow.** Unit-capacity network s → trips → slots → t; the
   implementation computes both and asserts equality on every run.
6. **Range-constrained EV routing ≤ shortest path.** The charging-leg graph turns "path that
   never violates battery limits with recharges at selected sites" into an ordinary shortest
   path on S + 2 vertices — a polynomial reduction that keeps planning exact.
7. **Charger sizing ≡ interval-graph colouring.** Polynomial (greedy by start time is optimal);
   the general graph-colouring version is NP-hard, which is why the time-interval structure
   matters.

## 6. Findings that drive the recommendation

* **Selection quality matters more than routing speed.** Greedy ratio reaches 97.5 % / 96.4 % of
  the optimal knapsack value on the normal and shock scenarios and serves 4 / 23 fewer trips;
  DP solves 220 candidates × budget 2,500 in 1 ms.
* **Least-cost BnB is the robust exact search.** It generated the fewest nodes on uncorrelated
  instances (353 vs 708) and stayed small on strongly correlated ones (252) where FIFO exploded
  to 14,886 nodes and a 3,609-node frontier.
* **Lazy per-source Dijkstra beats Floyd-Warshall** on road networks (5.5× faster on 1,024
  junctions, identical plans) because only origins and stations need trees.
* **Bellman-Ford is needed for energy.** Regeneration creates negative arcs where Dijkstra
  returns wrong energies; energy-optimal legs saved 0.1–0.6 % kWh at ≈ 2× routing time.
* **Shock exposes capacity, not coverage.** Under the shock, 96.7 % of trips remain routable, but
  max-flow shows only 144 of 751 charge-needing trips fit the ports in a 2-hour window, five
  stations need more chargers than they have and the grid is short for four evening hours
  (≈ 4.6 MWh). Ports and grid, not more sites, are the binding constraint.
* **Adversarial corridor: the proxy's blind spot.** With equal costs and near-equal values along a
  600 km corridor, every knapsack strategy picks 24 sites packed into km 30–381, leaving the last
  ≈ 216 km uncovered (usable range from a station is 117 km): 0 of 1,500 end-to-end trips are
  feasible although a spaced-out selection of the same size would serve them. Additive value cannot express spacing; this is the max-coverage structure of
  reduction 3.

* **Driver behaviour matters as much as ports.** In the shock city (budget 495) with drivers
  who wait, 475 of 1,856 vehicles queue for 3,340 h in all and the last one arrives at 50:17
  (day 3). Letting drivers divert after a 20-minute estimated wait (169 switches) cuts waiting by
  75 % to 838 h, the longest wait from 27 h to 10 h, and ends the day at 32:27. On the large city
  (budget 2,500) diversion (680 switches) cuts waiting by 71 %. The normal city has no queue long
  enough for a switch to pay off. Cost: 10.9 ms instead of 6.0 ms for 7.9 k events on the large
  city.
* **Ports beat sites in the normal city.** 38 drivers queue there, all at one station (522);
  raising it from 4 to 10 ports removes every wait without building anything.

## 7. Recommended production strategy

| Decision | Recommendation | Why |
|---|---|---|
| Site selection | **DP knapsack** when n·W ≤ ~10⁸ cells; otherwise **least-cost BnB** with greedy incumbent and a node limit; always **re-score with the planner** | optimal on the proxy, ms-scale; LC is the most stable search |
| Coverage-critical networks (corridors, rural) | add a coverage/spacing constraint (max gap ≤ usable range) via ILP or BnB on feasible-trip count | fixes the adversarial failure mode |
| Routing | **lazy per-source Dijkstra** on km; switch to **Bellman-Ford** on the energy graph where regeneration is material | fastest exact router; BF correct with negative arcs |
| All-pairs tables | Floyd-Warshall only for small dense maps (< ~300 nodes) or heavy reuse | Θ(V³) and V² memory |
| Capacity allocation | **Edmonds-Karp** (capacity-independent bound); FF-DFS acceptable for unit-capacity models; matching when slot-level assignment is needed | adversarial capacities: EK 5× faster |
| Driver guidance | publish live port availability and route drivers away from queues above ~20 min | 71–75 % less waiting under stress at no build cost |
| Charger sizing | interval partitioning per station; activity selection for per-port schedules | exact and Θ(n log n) |
| Event-log monitoring | **KMP** (linear worst case, O(m) memory, streaming); automaton for many scans of a small alphabet | naive degrades to Θ(nm) on fault bursts |
| Large matrix workloads | classical below n ≈ 256, Strassen with cutoff 16–64 above | measured crossover |
| Memory-bound DP (LCS on long traces) | Hirschberg linear-space LCS | 256 MB at 8,000 × 8,000 |
