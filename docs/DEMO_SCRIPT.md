# EV33 Demonstration Script (≈ 15 minutes)

Prepare: `make && make test` (show the final line: `59288 checks passed, 0 failed`).
Everything below is also run by `./build/ev33 demo` (or `make demo`), which saves a transcript to
`results/demo/transcript.txt` — keep it open as a fallback.

## 0. Framing (1 min)
"A city must decide where to put chargers with a fixed budget, and every EV trip must be routed
so the battery never drops below its reserve. We compare at least two algorithmic strategies on
the same data for three situations: normal, shock and stress."

## 1. Data and validation (1 min)
```
./build/ev33 validate --data data/medium
./build/ev33 validate --data build/test_tmp/inv_unknown_node     # created by make test
```
Point out: typed CSV records, strict validation with `file:line: reason`, binary-search node index.

## 2. Normal operation (4 min)
```
./build/ev33 run --data data/medium --select dp --route dijkstra
./build/ev33 run --data data/medium --select greedy --route bellman
```
Talking points (numbers from the transcript):
* DP selects 10 sites, value 2,480 (optimal); greedy 2,417 = 97.5 %, and 4 fewer trips feasible.
* 1,181 / 1,200 trips feasible; 446 needed a charge.
* Capacity line: FF-DFS = Edmonds-Karp = matching = 240 → three independent algorithms agree.
* Bellman-Ford routes on energy (regeneration makes negative arcs): 0.4–0.6 % less energy, slightly longer.

```
./build/ev33 compare --data data/medium
```
Explain the columns: exact strategies (dp, bnb-*) all reach the same value; BnB node counts
LC 84 < FIFO 128 < LIFO 285; Floyd gives identical plans at ~2.4× the routing time.

## 3. Capacity / constraint shock (3 min)
```
./build/ev33 run --data data/shock
```
What changed: budget −45 %, 30 % of sites reduced to one port, river bridges closed except two,
+60 % evening demand, grid −40 %.
* Still 96.7 % of trips routable — coverage holds.
* But max-flow: only 144 of 751 charge-needing trips fit the ports in a 2-hour window;
  bottleneck station reported; 5 stations need more chargers than installed (interval
  partitioning); worst grid surge 17:00–20:00, ≈ 4.6 MWh above supply (max subarray).
* Conclusion: invest in ports and grid at the bottleneck, not in more sites.

## 4. Large-data stress (2 min)
```
time ./build/ev33 run --data data/large
./build/ev33 compare --data data/large
./build/ev33 plan --data data/large --trip 22
```
* 1,024 junctions, 220 sites, 6,000 trips in under a second.
* Floyd-Warshall 730 ms vs lazy Dijkstra 132 ms with identical plans → Θ(V³) vs sparse SSSP.
* Trip 22: two charging stops, arrival energy and charging minutes per stop.

## 5. Syllabus coverage on real data (2 min)
```
./build/ev33 analyze --data data/medium
```
Walk the seven sections quickly: each line shows an algorithm, its sibling and "agree"
(merge vs quick, D&C vs Kadane, Prim vs Kruskal, Held-Karp vs brute force, BnB vs DP, FF vs EK
vs matching, four matchers). Mention N-Queens(8) = 92 validating the backtracking framework, and
CRT showing the top stations' maintenance windows never coincide.

## 6. Benchmarks and the adversarial case (2 min)
Open `results/bench.csv` (or `./build/ev33_bench --quick --only sort` live, 1 s):
* quick sort with first pivot on sorted input: 2 × 10⁸ comparisons = n²/2;
* naive matching on a fault burst: (n−m+1)·m comparisons vs KMP ≤ 2n;
* Dijkstra returns a wrong energy on negative arcs;
* FIFO branch-and-bound frontier explodes on strongly correlated knapsacks.
Close with the corridor: `./build/ev33 run --data data/adversarial` → 0 % feasible, because the
additive knapsack cannot see spacing — the max-coverage reduction in the complexity report, and
the reason the recommendation adds a coverage constraint for corridors.

## 7. Desktop app what-if (2 min, optional)
`make run-gui` (or `./build/gui/ev33_gui`). Constraint shock city: open the *Live day* tab, play at
1 h/s and watch queues build in the evening; choose *Drivers switch after 20 min* (waiting falls
about 75 %). Normal city, on the *Planner* tab: *Pin as baseline*, click station 522 on the map, + its ports to 10: every queue
disappears and the deltas show it was ports, not sites. When asked how the system is represented,
open the *Data model* tab: each typed record with a real example from the city, and the arrays,
trees and weighted or capacitated graphs built from them, with the file that defines each.

## 8. Recommendation (30 s)
DP (or least-cost BnB) for selection, re-scored by the planner; lazy Dijkstra routing with
Bellman-Ford for energy; Edmonds-Karp for capacity; KMP for log monitoring; live port availability so drivers divert from long queues.
