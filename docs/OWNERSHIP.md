# Individual Algorithm Ownership

Each algorithm has exactly one owner who is responsible for its implementation, its tests,
its benchmark family, and its section of the complexity report. The split below is for a team
of four; replace `Member A–D` with names (and roll numbers) before submission. Owners of a
domain integration point also review the library code it calls.

| Area | Algorithms (file) | Tests (tests/test_main.c) | Bench family | EV decision | Owner |
|---|---|---|---|---|---|
| Sorting & search | merge sort, quick sort ×3 pivots, iterative + recursive binary search, lower/upper bound (`src/lib/ev_sort.c`) | `test_sorting`, `test_searching` | `sort`, `search` | dispatch queue, cost index, node-id index | Member A |
| Divide & conquer | max subarray, fast power + powmod, celebrity, classical + Strassen (`src/lib/ev_dnc.c`) | `test_dnc` | `dnc` | grid surge window, battery fade, overflow hub, demand propagation | Member A |
| Greedy | activity selection, interval partitioning, Huffman (`src/lib/ev_greedy.c`); Dijkstra, Prim, Kruskal, union-find (`src/lib/ev_graph.c`) | `test_greedy`, `test_graphs` | `greedy`, `graph` | charger sizing, log compression, routing, cable backbone | Member B |
| Dynamic programming | 0/1 knapsack, matrix chain, LCS, optimal BST, Held-Karp (`src/lib/ev_dp.c`); Bellman-Ford, Floyd-Warshall (`src/lib/ev_graph.c`) | `test_dp`, `test_graphs` | `knapsack`, `dp`, `graph` | site selection, projections, route adherence, lookup index, maintenance tour, energy routing | Member C |
| Backtracking & BnB | N-Queens, subset sum, knapsack BnB FIFO/LIFO/LC (`src/lib/ev_search.c`) | `test_search` | `backtrack`, `knapsack` | exact site selection, exact budget spend | Member C |
| Flow & matching | Ford-Fulkerson, Edmonds-Karp, min-cut, bipartite matching (`src/lib/ev_flow.c`) | `test_flow` | `flow` | capacity allocation, slot assignment | Member D |
| Number theory & strings | gcd, extended gcd, mulmod, CRT, naive, Rabin-Karp, finite automaton, KMP (`src/lib/ev_nt.c`) | `test_nt_strings` | `strings` | maintenance alignment, fault-pattern detection | Member D |
| Domain: model & validation | loader, validation, index (`src/domain/ev_model.c`) | `test_tiny_scenario`, `test_invalid_inputs` | — | data quality | Member B |
| Domain: planner | values, selection, router, charging-leg graph, capacity, sessions (`src/domain/ev_plan.c`) | `test_dataset` | `pipeline` | end-to-end service KPIs | Member C (planner), Member D (capacity) |
| Reports & CLI | exports, compare, analyze (`src/domain/ev_report.c`), CLI/menu (`src/app/main.c`) | manual + demo | — | operator interface | Member A |
| Day simulation | event heap, FIFO station queues, timelines, divert policy (`src/domain/ev_sim.c`) | `test_sim` | — | queueing, delay, live day, driver guidance | Member D |
| What-if constraints | locked / banned sites in selection (`ev_select_ex`, `src/domain/ev_plan.c`), edit parsing (`src/domain/ev_api.c`) | `test_constraints` | — | planner what-if | Member C |
| Desktop app | C++17 wrapper over the library (`gui/engine.cpp`), city map (`gui/map.cpp`), seven tabs (`gui/views.cpp`), window and theme (`gui/main.cpp`, `gui/ui.cpp`) | every tab on every city under ASan/UBSan, screenshots via `--shot` | — | operator interface | Member A |
| Tooling | generator (`tools/ev33_gen.c`), benchmark runner (`src/app/bench.c`), build, CI | regression fixtures | all | reproducibility | Member B |

## Owner checklist (per algorithm)

1. Interface documented in its header (inputs, outputs, complexity, edge cases).
2. Oracle test (brute force, textbook example or sibling algorithm) plus edge cases.
3. Benchmark rows on small / medium / large / adversarial inputs with a `check` column of 1.
4. Recurrence or loop analysis and measured scaling in `docs/COMPLEXITY_REPORT.md`.
5. One line in `ev33 analyze` showing the algorithm on scenario data.
6. Can explain the adversarial input for the algorithm during the viva.
