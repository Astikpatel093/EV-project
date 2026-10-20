# Algorithm ownership

Each area has one owner who is responsible for its implementation, its tests, its benchmark rows
and its section of `docs/algorithms.md`. The split is for a team of four; replace
`Member A–D` with names (and roll numbers) before submission.

| Area | Implementation | Tests | Benchmark family | Owner |
|---|---|---|---|---|
| Sorting and searching | `include/ev/algorithms/sorting.hpp`, `searching.hpp` | `test_sorting_searching.cpp`, IT07 | `sorting`, `searching` | Member A |
| Divide and conquer | `src/algorithms/divide_conquer.cpp` | `test_divide_conquer.cpp`, AD23, AD24 | `matrix` | Member A |
| Greedy | `src/algorithms/greedy.cpp` | `test_greedy.cpp`, AD25 | `scheduling` | Member B |
| Graphs: shortest paths, spanning trees, heap | `src/graph/shortest_paths.cpp`, `spanning_tree.cpp`, `include/ev/algorithms/heap.hpp` | `test_graph.cpp`, `test_spanning_tree_flow.cpp`, IT06 | `shortest_path`, `spanning_tree`, `dense_graph` | Member B |
| Dynamic programming | `src/algorithms/dynamic_programming.cpp` | `test_dynamic_programming.cpp` | `placement` | Member C |
| Backtracking and branch and bound | `src/algorithms/backtracking.cpp`, `branch_and_bound.cpp` | `test_dynamic_programming.cpp` (`NQueens`, `SubsetSum`, `BranchAndBound`) | `placement` | Member C |
| Flow and matching | `src/graph/max_flow.cpp` | `test_spanning_tree_flow.cpp` | — | Member D |
| Number theory and strings | `src/algorithms/number_theory.cpp`, `string_matching.cpp` | `test_number_theory_strings.cpp` | — | Member D |
| Models, CSV and validation | `src/models/*`, `src/io/*`, `src/core/money.cpp` | `test_io_models.cpp`, IT05, AD01–AD13, AD17, AD18, AD21 | — | Member B |
| Energy model and trip planners | `src/planner/*` | `test_planner.cpp`, IT01–IT03, AD14, AD15, AD20, AD22 | `trip_planning` | Member C |
| Placement | `src/placement/placement.cpp` | `test_placement.cpp`, IT04, AD19 | `placement` | Member C |
| Capacity, sessions, simulation | `src/operations/*` | `test_operations_datagen.cpp` | — | Member D |
| Pipeline, reports, command line | `src/analysis/*`, `src/app/*` | IT08, regression | — | Member A |
| Generators and benchmark harness | `src/datagen/*`, `tools/`, `benchmarks/` | `test_operations_datagen.cpp` | all | Member B |
| Website and desktop app | `shared/`, `server/`, `gui/` | manual (every page, every city), sanitizer run | — | Member A |

## Owner checklist (per algorithm)

1. Interface documented in its header: inputs, outputs, complexity, edge cases, errors.
2. Oracle test (brute force, textbook example or a second algorithm) plus edge and invalid cases.
3. Benchmark rows with `validation = pass`.
4. Complexity and correctness argument in `docs/algorithms.md`, compared with the measured scaling.
5. Its use on scenario data in `ev33 analyze` or in the planning pipeline.
6. Can explain its worst-case input during the viva.
