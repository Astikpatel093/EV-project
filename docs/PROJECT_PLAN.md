# 8-Week Plan

| Week | Milestone | Deliverables | Exit check |
|---|---|---|---|
| 1 | Foundations | data model and CSV formats, loader + validation, tracked allocator, generator (small/medium), Makefile/CMake | `ev33 validate` on small + invalid fixtures |
| 2 | Sorting, searching, D&C | merge/quick/binary search, node index, max subarray, fast power, celebrity, Strassen | oracle tests green; `ev33_bench --only sort`, `--only dnc` |
| 3 | Greedy + graphs | activity selection, interval partitioning, Huffman, Dijkstra, Prim, Kruskal | Dijkstra-based station values; sessions analysis |
| 4 | Dynamic programming | knapsack, matrix chain, LCS, optimal BST, Bellman-Ford, Floyd-Warshall, Held-Karp | DP selection + three routing strategies end to end |
| 5 | Planner | charging-leg graph planner, KPIs, exports, `run` / `plan` commands | hand-checked tiny scenario; reserve invariant |
| 6 | Search, flow, number theory, strings | N-Queens, subset sum, BnB FIFO/LIFO/LC, Ford-Fulkerson, Edmonds-Karp, matching, CRT, 4 matchers | FF = EK = matching on every dataset |
| 7 | Scale + evaluation | shock/large/adversarial profiles, benchmark runner, `compare`, `analyze`, sanitizer runs | full `bench.csv`, 0 failed checks |
| 8 | Report + release | design/user guides, complexity report, demo script, regression fixtures, release tag | `make release TAG=v1.0.0`, rehearsed demo |
