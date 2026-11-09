# Testing

- Correctness: for a fixed small dataset, the parallel index must equal the sequential index (same postings, same document counts) for every thread count from 1 to N.
- Concurrency: run the index build and concurrent searches under a thread sanitizer for the chosen language/toolchain. Any reported race blocks the task.
- Search: keyword queries on a known test corpus return the expected documents in the JSON format from `docs/specification/06-api-specification.md`.
- API: contract tests for every endpoint, including the health response with `ollama: true|false`.
- Benchmarks live in `benchmarks/`. They must be reproducible: fixed dataset, fixed seed, number of repeats recorded, output written to `benchmarks/results/`. Raw output is gitignored; aggregated tables and plots are committed.
- Speedup is computed as S(p) = T(1) / T(p) and efficiency as E(p) = S(p) / p. Report both.
