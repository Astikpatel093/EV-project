# Testing

All automated tests use **GoogleTest** (vendored, `third_party/googletest`, v1.15.2) and are
registered with CTest under three labels:

| Label | Program | Source | Tests |
|---|---|---|---|
| `unit` | `ev33_unit_tests` | `tests/unit/*.cpp` | 154 tests in 40 suites |
| `integration` | `ev33_integration_tests` | `tests/integration/integration_tests.cpp` | IT01–IT08 + regression check |
| `adversarial` | `ev33_adversarial_tests` | `tests/adversarial/adversarial_tests.cpp` | AD01–AD25 |
| `frontend` | `ev33_frontend_tests` | `tests/frontend/frontend_tests.cpp` | FE01–FE09 (the engine calls the desktop app makes) |

```
ctest --test-dir build --output-on-failure            # everything
ctest --test-dir build -L unit                        # one label
ctest --test-dir build -L integration -V              # shows the ACTUAL line of each test
build/tests/ev33_unit_tests --gtest_filter='Dijkstra.*'
```

When run through CTest each program writes a JUnit-style XML report to `build/tests/<program>.xml`. The integration
tests also store their observed outcome in that report with `RecordProperty("actual", …)` and
print it as an `ACTUAL:` line.

**Status at the time of writing (gcc 13.3, Release and Debug + ASan/UBSan, Linux; llvm-mingw
clang 19, Windows x64):** 154/154 unit, 9/9 integration, 25/25 adversarial tests pass. The
"Actual" column below is copied from a real run (`ctest -L integration -V`), not written by hand.

## Unit tests (what each file covers)

| File | Covers |
|---|---|
| `test_sorting_searching.cpp` | empty, single element, sorted / reverse / duplicates / negatives / all-equal, 10⁵-element random arrays vs `std::sort`, quick-sort worst case without stack overflow, merge-sort stability on station records, custom comparator; binary search success/failure, iterative = recursive on every key, key projection |
| `test_divide_conquer.cpp` | max subarray (textbook, all negative, ties, D&C = Kadane = brute force), fast power, modular power, celebrity, classical and Strassen (known product, rectangular, invalid sizes, equality on many sizes) |
| `test_greedy.cpp` | activity selection (textbook, touching, all overlapping, exhaustive oracle, invalid sessions), interval partitioning (= depth, valid assignment), Huffman (224-bit textbook, prefix-free, round trip, single symbol, invalid input) |
| `test_graph.cpp` | graph validation; Dijkstra (known answer, disconnected, single node, source = destination, invalid source, duplicate edges, self-loops, zero weights, negative weight rejected, very large weights, deterministic ties); Bellman–Ford (negative arcs, negative cycle); Dijkstra = Bellman–Ford = Floyd–Warshall on random graphs |
| `test_spanning_tree_flow.cpp` | Prim / Kruskal (cycles, equal weights, forest on disconnected graphs, single node, self-loops and duplicates, directed graph rejected, agreement on random graphs, MST ≠ shortest-path tree, dense version), union–find, max flow (textbook network, invalid input, DFS = BFS = matching on random bipartite networks) |
| `test_dynamic_programming.cpp` | knapsack (known optimum, zero budget, no candidates, all affordable, none affordable, cost = budget, equal benefits/costs, invalid input, DP = brute force), matrix chain, LCS, optimal BST, Held–Karp vs brute force, N-Queens counts, subset sum (found / impossible / node limit), branch and bound (all frontiers = DP) |
| `test_number_theory_strings.cpp` | gcd, extended Euclid, modular inverse, CRT (coprime, non-coprime consistent/inconsistent, invalid, overflow, random systems), four string matchers (known patterns, empty pattern rejected, agreement, KMP linear on a fault burst) |
| `test_io_models.cpp` | exact rupee parsing, malformed amounts, Indian grouping; CSV by column name, comments, quotes, `file:line` errors; road network validation, components, distances; vehicle and station validation |
| `test_planner.cpp` | energy model = hand calculation; enough battery; shortest route not energy-feasible; insufficient battery without chargers; trip requiring charging; stations present but unusable/unreachable; infeasible even with a station elsewhere; battery equal to the reserve; charge above capacity rejected; zero-length trip; no road connection; leg planner sound but incomplete (falls back to the energy-minimal path on the sample; a three-road network where it misses a trip the exact planner finds); built candidate makes a trip feasible; offline station never used; discretisation conservative |
| `test_placement.cpp` | known optimum on the sample, greedy not optimal, zero budget, no candidates, all / none affordable, equal benefits and costs, zero-benefit sites never built, invalid input, locked and banned sites, budget never exceeded on random instances, exact with rupee/paise costs, large table falls back to branch and bound, benefits estimated from trips |
| `test_operations_datagen.cpp` | booked sessions on the sample; capacity methods agree; session invariants; pipeline respects budget and reserve; simulation invariants for both policies; unlimited ports remove all waiting; scenario generator reproducible and labelled SYNTHETIC; benchmark generator sizes, connectivity, no duplicates, reproducible |

## Integration tests (IT01–IT08)

Input for IT01–IT05 and IT08: the hand-made sample in `data/sample/` (its README works out every
expected number by hand). IT06 and IT07 use the generated `medium` and `large` scenarios.

| ID | Input | Procedure | Expected | Actual (from a run) |
|---|---|---|---|---|
| IT01 basic trip planning | sample; trip 6 (SV1, node 4 → 5, 12 kWh); trip 1 (SV1, 1 → 3) | load network and vehicles, plan with the exact planner and the existing stations | trip 6 `direct`, 20 km, starts at 4, ends at 5; trip 1 takes the flat 40 km road 1-2-3, not the 30 km hill pass | `direct, route 4-5, 20.000000 km` (trip 1: route 1-2-3, 40 km) |
| IT02 trip requiring charging | sample; trip 2 (SV1, 1 → 5, 80 km, 10 kWh usable) | plan with the exact planner | `with-charging`, one stop at station 101 (node 3, on the route), arrives above reserve, leaves with 16 kWh, arrives with 8 kWh | `stop at station 101 arriving with 4.000000 kWh, leaving with 16.000000 kWh; 80.000000 km, arrive 8.000000 kWh` |
| IT03 no feasible route | sample; trip 3 (to the island), trip 4 (8 kWh micro car), trip 7 (low charge, only a CHAdeMO charger in reach) | plan each | `no-road-connection`, `charging-insufficient`, `out-of-range`; not feasible, no stops, explanation not empty | `no-road-connection \| charging-insufficient \| out-of-range` |
| IT04 station placement | sample; 6 candidates, budget 100 lakh | DP placement; compare with all 64 subsets | {501, 502, 506}, benefit 155, cost ≤ budget, equal to the exhaustive optimum | `chosen 501 502 506 benefit 155, cost INR 1,00,00,000` |
| IT05 dataset validation | 7 malformed copies of the sample written to disk (missing column, bad number, duplicate id, latitude 95, negative distance, unknown node, charge > capacity) | load each through the normal file loader while a valid scenario is held | each rejected with `file:line: reason`; the held scenario is unchanged | all 7 rejected, e.g. `roads.csv:3: 'distance_km' must be a number, got 'twenty'`; `nodes.csv:8: node 6 has invalid coordinates …`; valid state kept (10 nodes, 8 trips) |
| IT06 Prim vs Kruskal | `medium` road network (connected); sample (two components) | run both on the same `Graph` object | equal total weight, V − 1 edges; on the sample both report a forest of 2 trees | `Prim 1198.997000 km, Kruskal 1198.997000 km, 255 edges` |
| IT07 sorting and searching | `large` candidates (220) and node ids | quick sort by id, merge sort by cost; binary search for 650 and 4999; upper bound for 60 lakh | 650 found, 4999 → −1, count = linear count, cost order stable, every trip origin found iteratively and recursively | `candidate 650 at index 150, 4999 -> -1, 42 candidates cost <= 60 lakh` |
| IT08 end-to-end | sample: network, vehicles, existing stations, candidates, budget | full `PlanningRun` (DP + Dijkstra legs), capacity, sessions, report, exact plan of trip 2, CSV export | benefit 155 within budget, 3 existing + 3 new sites, trip 2 `with-charging` 80 km, capacity methods consistent, island trip infeasible, exports written | `placement benefit 155, 7/8 trips feasible; trip 2: with-charging, 80.000000 km` |
| Regression | the five generated scenarios | DP + Dijkstra run; compare with `tests/fixtures/regression/*.txt` | identical feasible count, benefit and stations built | `small 55/60, 65; medium 1195/1200, 3335; shock 1869/1920, 4748; large 5951/6000, 26447; adversarial 0/1500, 129064` |

## Adversarial tests (AD01–AD25)

Behaviour classes: **REJECT** (refused with a precise message), **EMPTY** (valid empty result),
**UNREACHABLE** (valid "no route" with an explanation), **VALID** (normal correct result),
**CONTROLLED** (controlled error instead of overflow or a misleading value).

| ID | Input | Expected | Result |
|---|---|---|---|
| AD01 empty input files | empty `nodes.csv`; `roads.csv` with blank lines; missing `nodes.csv`; header-only `trips.csv` | REJECT "file is empty" / "file not found"; header-only table is valid | pass |
| AD02 missing fields | row with 3 of 4 fields; empty `distance_km`; vehicles without `consumption_kwh_per_km` | REJECT `roads.csv:2: expected 4 fields, found 3`, "missing value", "missing required column" | pass |
| AD03 duplicate node ids | node 2 twice | REJECT `nodes.csv:4: duplicate node id 2` | pass |
| AD04 duplicate station ids | station 7 twice; candidate reusing station id 7 | REJECT "duplicate station id 7", "already used" | pass |
| AD05 edge to non-existent node | road 1 → 99 | REJECT "unknown node 99" | pass |
| AD06 negative distance | road of −5 km; graph edge −5 given to Dijkstra | REJECT in the loader and `invalid_argument` from Dijkstra | pass |
| AD07 negative installation cost | cost −500000, cost 0, budget −10 | REJECT ("must not be negative", "cost must be positive", `budget_inr`) | pass |
| AD08 invalid latitude/longitude | lat 91, lon 181, lat without lon | REJECT "invalid coordinates", "both lat and lon" | pass |
| AD09 charge above capacity | vehicle 12 of 10 kWh; trip start charge 10.5 of 10 | REJECT "exceeds the battery capacity" | pass |
| AD10 zero battery capacity | battery 0 kWh | REJECT "battery capacity must be positive" | pass |
| AD11 zero energy consumption | 0 kWh/km (would make every trip trivially feasible) | REJECT "energy consumption must be positive" | pass |
| AD12 extremely large weights | road of 10³⁰⁰ km; two 10³⁰⁸ edges; two 4·10¹⁵ edges | REJECT "unrealistically long"; `overflow_error` (not "unreachable"); exact 8·10¹⁵ | pass |
| AD13 arithmetic overflow risks | amount of 20 digits; six locked sites of ~10¹⁵ rupees; mulmod near 2⁶¹; matrix chain near 2⁶⁴; benefits 2·10¹² | REJECT / `over_budget` with a positive (non-wrapped) total / exact / saturated / REJECT | pass |
| AD14 disconnected components | trip from the mainland to the island | UNREACHABLE `no-road-connection`; spanning forest of 2 | pass |
| AD15 multiple equal-cost paths | 4-5 and 4-8-5 both 20 km | VALID 20 km, same route on every call | pass |
| AD16 extremely long inputs | 10⁶ random keys; 2·10⁶-character fault log | VALID merge = quick sort; KMP finds the one match; Huffman round trip | pass |
| AD17 repeated edges | two different roads 1–2 (10 and 4 km); same road id twice | VALID uses 4 km (14 km trip); duplicate id REJECT | pass |
| AD18 self-loops | road 1 → 1 in data; self-loop in a raw graph | REJECT "self-loop" in data; ignored by algorithms | pass |
| AD19 no station within budget | budget ₹100 | EMPTY placement, cost 0, benefit 0 | pass |
| AD20 station present but unreachable | SV1 with 9.5 kWh, station 40 km (8 kWh) away, 7.5 kWh usable | UNREACHABLE `out-of-range`; no reachable sites | pass |
| AD21 invalid source or destination | trip to node 42; unknown vehicle; position 7 in a 3-node network; Dijkstra source 3 of 3 | REJECT in loader; `out_of_range` from planner and Dijkstra | pass |
| AD22 empty station and candidate lists | no `stations.csv`, no `candidates.csv` | VALID empty placement, no sites, the short trip is `direct` | pass |
| AD23 invalid matrix dimensions | 0×0 matrices; 0×4 matrix; chain with a 0 dimension | REJECT `invalid_argument` | pass |
| AD24 matrix dimension mismatch | 2×3 · 2×3; 5×1 · 2×5 | REJECT `invalid_argument` | pass |
| AD25 invalid Huffman input | empty text; encoding an unknown symbol; a bit stream that ends mid-code | REJECT `invalid_argument` | pass |

## Sanitizers

```
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEV33_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

AddressSanitizer + UndefinedBehaviorSanitizer with `-fno-sanitize-recover=all`, so any invalid
memory access, leak or undefined arithmetic fails the run. Result: all four suites (including FE01–FE09) pass with no
report (Debug, gcc 13.3). The website server was also run under the sanitizers and every page of
the small, medium, shock and adversarial cities requested, including what-if edits; no report.

## Desktop app tests (FE01–FE09)

The views of *EV33 Charge Atlas* only draw what the shared engine adapter returns, so the adapter
calls are tested without opening a window (`ctest --test-dir build -L frontend`).

| ID | Input | Expected | Actual |
|---|---|---|---|
| FE01 Trip Planner answer | sample, SV1 typed in (20 kWh, 12 kWh start, 0.2 kWh/km, 2 kWh reserve), node 1 → 5, each of the 4 methods | feasible, 80 km, 1 stop, path from 1 to 5, battery never below the reserve | passed for all 4 methods |
| FE02 infeasible trip | node 1 → 9 (other component) | not feasible, `no-road-connection`, explanation filled, no error | passed |
| FE03 invalid form input | unknown source, destination −1, start 25 kWh on a 20 kWh battery, reserve above battery, zero consumption | each rejected with a message; nothing planned | passed |
| FE04 Floyd–Warshall limit | a 1,700-junction chain written to disk and loaded | Floyd refused with the 1,600-junction reason; Dijkstra legs still answer (19 km) | passed |
| FE05 placement comparison | sample, budget 1 crore | DP and branch and bound 155 (optimal), greedy 140, all within budget; the Dashboard run shows 155 | passed |
| FE06 dataset check, valid | `data/sample` | ok; 10 nodes, 11 roads, 3 stations (1 offline), 6 candidates, 3 vehicles, 8 trips, 2 components, synthetic | passed |
| FE07 dataset check, invalid | 7 corrupted copies: negative distance, non-numeric distance, duplicate node id, unknown node, negative cost, zero battery, missing column | each rejected naming the file and line; *Load as a new city* refuses it and the current city stays | passed |
| FE08 missing file | sample without `roads.csv`; a folder that does not exist | rejected, naming `roads.csv` | passed |
| FE09 benchmark file | committed `benchmarks/results.csv`; a missing file | rows load with reps > 0 and min ≤ median ≤ max; missing file → no rows and an error (the view then shows the "No benchmark results available" message) | passed |

The window itself (layout, drawing, interaction) was checked by hand on Windows x64: every view was
opened at 1920×1040 and 150 % scale in both themes, with screenshots saved through `--shot`.

## Manual checks

* `ev33 verify --data DIR` compares the fleet planner with the exact planner on a sample of trips
  (medium: 200/200 agree; large: 298 both, 1 leg only, 1 neither, 0 exact only).
* `ev33 algorithms` runs every algorithm on its textbook example and prints PASS/FAIL.
* Website: every page for every city returns HTTP 200; screenshots were checked for the planner,
  live day, capacity, algorithms, data model and benchmark pages.
