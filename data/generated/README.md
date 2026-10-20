# Benchmark datasets (generated, not committed)

SYNTHETIC. These files are produced by the benchmark dataset generator and are not stored in Git
(about 59 MB). Only `metadata.json` is committed: it records, for every file, the seed, the
generator parameters, the record counts and an FNV-1a checksum, so a regenerated set can be
checked against it.

Regenerate (from the build folder):

    benchmark_data_generator --seed 12345 --size all --out ../data/generated
    benchmark_data_generator --out ../data/generated --verify     # regenerate in memory, compare checksums

| Set | Vertices | Use |
|---|---|---|
| B1 | 100 | shortest paths, spanning trees |
| B2 | 500 | shortest paths, spanning trees (also Floyd–Warshall) |
| B3 | 1,000 | shortest paths, spanning trees |
| B4 | 5,000 | shortest paths, spanning trees |
| B5 | 10,000 | Dijkstra, Prim, Kruskal |
| B6 | 50,000 | Dijkstra, Prim, Kruskal |

Each graph set has three variants: `connected-varied` (random weights), `connected-equal`
(all weights equal) and `disconnected` (four separate components). `D100/D200/D400-dense-small`
are dense graphs for Floyd–Warshall and dense Prim. The placement, session and key files are
described in `docs/benchmark_methodology.md`.
